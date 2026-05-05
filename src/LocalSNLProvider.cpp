#ifndef __EMSCRIPTEN__

#include "LocalSNLProvider.h"
#include "Console.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void LocalSNLProvider::on_open(std::function<void()> callback) {
  openCb_ = std::move(callback);
}
void LocalSNLProvider::on_message(std::function<void(const std::string&)> callback) {
  msgCb_ = std::move(callback);
}
void LocalSNLProvider::on_close(std::function<void()> callback) {
  closeCb_ = std::move(callback);
}
void LocalSNLProvider::on_error(std::function<void(const std::string&)> callback) {
  errCb_ = std::move(callback);
}

void LocalSNLProvider::start() {
  Console::Log("LocalSNLProvider: starting in standalone mode");
  if (openCb_) openCb_();
}

// ---------------------------------------------------------------------------
// File loading — runs yosys to produce JSON, then parses it
// ---------------------------------------------------------------------------

static Direction yosysDir(const std::string& d) {
  if (d == "output") return Direction::Output;
  if (d == "inout")  return Direction::Inout;
  return Direction::Input;
}

void LocalSNLProvider::parseYosysJson(const std::string& jsonPath) {
  std::ifstream f(jsonPath);
  if (!f) {
    Console::Error("Cannot open yosys JSON: " + jsonPath);
    return;
  }
  json j;
  try { j = json::parse(f); }
  catch (const std::exception& e) {
    Console::Error("JSON parse error: " + std::string(e.what()));
    return;
  }

  modules_.clear();
  moduleIndex_.clear();

  if (!j.contains("modules")) return;
  const auto& jmods = j["modules"];

  // First pass: register all module names and assign designIds.
  unsigned idx = 0;
  for (auto& [modName, _] : jmods.items()) {
    VModule m;
    m.name     = modName;
    m.designId = idx + 1; // 0 reserved for "no module"
    moduleIndex_[modName] = idx;
    modules_.push_back(std::move(m));
    ++idx;
  }

  // Second pass: parse ports and cells.
  for (auto& [modName, jmod] : jmods.items()) {
    VModule& mod = modules_[moduleIndex_[modName]];

    // Ports
    if (jmod.contains("ports")) {
      unsigned portIdx = 0;
      for (auto& [portName, jp] : jmod["ports"].items()) {
        VPort p;
        p.name      = portName;
        p.direction = yosysDir(jp.value("direction", "input"));
        int width = jp.contains("bits") ? (int)jp["bits"].size() : 1;
        if (width > 1) { p.msb = width - 1; p.lsb = 0; }
        mod.ports.push_back(std::move(p));
        ++portIdx;
      }
    }

    // Cells → instances or primitives
    if (jmod.contains("cells")) {
      unsigned instIdx = 0;
      unsigned primIdx = 0;
      for (auto& [cellName, jcell] : jmod["cells"].items()) {
        std::string type = jcell.value("type", "");
        if (moduleIndex_.count(type)) {
          VInstance inst;
          inst.instanceName = cellName;
          inst.moduleName   = type;
          inst.childId      = instIdx++;
          mod.instances.push_back(std::move(inst));
        } else {
          VInstance prim;
          prim.instanceName = cellName;
          prim.moduleName   = type;
          prim.childId      = primIdx++;
          mod.primitives.push_back(std::move(prim));
        }
      }
    }
  }

  // Find top module: the one marked with attribute "top" or the one not
  // instantiated by any other module.
  std::unordered_map<std::string, bool> isInstantiated;
  for (auto& mod : modules_) {
    for (auto& inst : mod.instances)
      isInstantiated[inst.moduleName] = true;
  }

  topModuleIdx_ = 0;
  // Prefer yosys "top" attribute
  for (auto& [modName, jmod] : jmods.items()) {
    if (jmod.contains("attributes")) {
      const auto& attrs = jmod["attributes"];
      if (attrs.contains("top")) {
        topModuleIdx_ = moduleIndex_[modName];
        goto found_top;
      }
    }
  }
  // Fall back to first module not instantiated by any other
  for (unsigned i = 0; i < modules_.size(); ++i) {
    if (!isInstantiated.count(modules_[i].name)) {
      topModuleIdx_ = i;
      break;
    }
  }
  found_top:;

  Console::Log("Parsed " + std::to_string(modules_.size()) +
               " modules, top = " + modules_[topModuleIdx_].name);
}

void LocalSNLProvider::loadFile(const std::string& path) {
  netlibPath_ = path;
  Console::Log("LocalSNLProvider: loading " + path);

  bool isSV = path.size() >= 3 &&
              path.substr(path.size() - 3) == ".sv";
  std::string readCmd = isSV ? "read_verilog -sv" : "read_verilog";

  // Build temp path for yosys JSON output
  std::string tmpJson = "/tmp/naja_netlist_" + std::to_string(getpid()) + ".json";
  std::string logFile = "/tmp/naja_yosys_"   + std::to_string(getpid()) + ".log";

  // Single-quote the path and escape any embedded single quotes
  auto shellQuote = [](const std::string& s) {
    std::string r = "'";
    for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; }
    return r + "'";
  };

  std::string cmd =
    "yosys -q -p " +
    shellQuote(readCmd + " " + shellQuote(path) +
               "; hierarchy -auto-top; write_json " + shellQuote(tmpJson)) +
    " >" + shellQuote(logFile) + " 2>&1";

  Console::Log("Running: " + cmd);
  int ret = std::system(cmd.c_str());
  if (ret != 0) {
    Console::Error("yosys failed (exit " + std::to_string(ret) +
                   "). See " + logFile);
    return;
  }

  parseYosysJson(tmpJson);
  std::remove(tmpJson.c_str());
}

// ---------------------------------------------------------------------------
// Request dispatch
// ---------------------------------------------------------------------------

void LocalSNLProvider::send(const std::string& msg) {
  handleRequest(msg);
}

void LocalSNLProvider::handleRequest(const std::string& jsonRequest) {
  if (!msgCb_) return;

  json req;
  try { req = json::parse(jsonRequest); }
  catch (...) {
    Console::Error("LocalSNLProvider: failed to parse request: " + jsonRequest);
    return;
  }

  std::string request = req.value("request", "");

  if (request == "load_root") {
    msgCb_(buildRootResponse());
  } else if (request == "load_instances" || request == "load_primitives") {
    unsigned guiId    = req.value("gui_id", 0u);
    unsigned designId = 0;
    if (req.contains("design_ref"))
      designId = req["design_ref"].value("design_id", 0u);
    msgCb_(buildInstancesResponse(guiId, designId, request == "load_primitives"));
  } else if (request == "load_terms") {
    unsigned guiId    = req.value("gui_id", 0u);
    unsigned designId = 0;
    if (req.contains("design_ref"))
      designId = req["design_ref"].value("design_id", 0u);
    msgCb_(buildTermsResponse(guiId, designId));
  } else if (request == "load_equipotential") {
    json resp = {
      {"response", "equipotential_response"},
      {"terms", json::array()},
      {"occurrences", json::array()}
    };
    msgCb_(resp.dump());
  } else {
    Console::Error("LocalSNLProvider: unknown request: " + request);
  }
}

// ---------------------------------------------------------------------------
// Response builders
// ---------------------------------------------------------------------------

std::string LocalSNLProvider::buildRootResponse() const {
  json root;
  if (modules_.empty()) {
    root = {
      {"name", netlibPath_.empty()
               ? "(no netlist — use File > Open to load)"
               : "(failed to parse " + netlibPath_ + ")"},
      {"design_ref", {{"db_id", 0}, {"library_id", 0}, {"design_id", 0}}},
      {"has_terms",      false},
      {"has_primitives", false},
      {"has_instances",  false}
    };
  } else {
    const VModule& top = modules_[topModuleIdx_];
    root = {
      {"name", top.name},
      {"design_ref", {{"db_id", 0}, {"library_id", 0}, {"design_id", top.designId}}},
      {"has_terms",      !top.ports.empty()},
      {"has_primitives", !top.primitives.empty()},
      {"has_instances",  !top.instances.empty()}
    };
  }
  return json{{"response", "root_response"}, {"root", root}}.dump();
}

std::string LocalSNLProvider::buildInstancesResponse(
    unsigned guiId, unsigned designId, bool primitives) const
{
  json children = json::array();

  if (designId > 0 && designId <= modules_.size()) {
    const VModule& mod = modules_[designId - 1];
    const auto& list = primitives ? mod.primitives : mod.instances;

    for (const auto& inst : list) {
      bool childHasTerms = false, childHasPrims = false, childHasInsts = false;
      unsigned childDesignId = 0;

      if (!primitives && moduleIndex_.count(inst.moduleName)) {
        unsigned ci = moduleIndex_.at(inst.moduleName);
        const VModule& cm = modules_[ci];
        childHasTerms = !cm.ports.empty();
        childHasPrims = !cm.primitives.empty();
        childHasInsts = !cm.instances.empty();
        childDesignId = cm.designId;
      }

      children.push_back({
        {"name",       inst.instanceName},
        {"model_name", inst.moduleName},
        {"child_id",   inst.childId},
        {"design_ref", {{"db_id", 0}, {"library_id", 0}, {"design_id", childDesignId}}},
        {"has_terms",      childHasTerms},
        {"has_primitives", childHasPrims},
        {"has_instances",  childHasInsts}
      });
    }
  }

  return json{
    {"response", primitives ? "primitives_response" : "instances_response"},
    {"gui_id",   guiId},
    {"children", children}
  }.dump();
}

std::string LocalSNLProvider::buildTermsResponse(
    unsigned guiId, unsigned designId) const
{
  json children = json::array();

  if (designId > 0 && designId <= modules_.size()) {
    const VModule& mod = modules_[designId - 1];
    unsigned portIdx = 0;
    for (const auto& port : mod.ports) {
      json term = {
        {"name",      port.name},
        {"child_id",  portIdx},
        {"direction", static_cast<int>(port.direction)}
      };
      if (port.msb >= 0) {
        term["msb"] = port.msb;
        term["lsb"] = port.lsb;
      }
      children.push_back(std::move(term));
      ++portIdx;
    }
  }

  return json{
    {"response", "terms_response"},
    {"gui_id",   guiId},
    {"children", children}
  }.dump();
}

#endif // __EMSCRIPTEN__
