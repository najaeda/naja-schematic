#ifndef __EMSCRIPTEN__

#include "LocalSNLProvider.h"
#include "Console.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <set>

// naja core
#include "NLUniverse.h"
#include "NLDB.h"
#include "NLLibrary.h"
// naja SNL
#include "SNLDesign.h"
#include "SNLInstance.h"
#include "SNLInstTerm.h"
#include "SNLTerm.h"
#include "SNLBusTerm.h"
#include "SNLBusTermBit.h"
#include "SNLScalarTerm.h"
#include "SNLNetComponent.h"
#include "SNLPath.h"
#include "SNLOccurrence.h"
#include "SNLEquipotential.h"
// naja formats
#include "SNLVRLConstructor.h"
#include "SNLSVConstructor.h"
#include "SNLLibertyConstructor.h"
// naja serialization
#include "SNLCapnP.h"

using json = nlohmann::json;
using namespace naja::NL;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int snlDirToInt(const SNLNetComponent::Direction& dir) {
  using E = SNLNetComponent::Direction::DirectionEnum;
  switch (static_cast<E>(dir)) {
    case E::Input:  return 0;
    case E::Output: return 1;
    case E::InOut:  return 2;
    default:        return 0;
  }
}

static std::string designName(const SNLDesign* d) {
  return d->getString();
}

static std::string instanceName(const SNLInstance* i) {
  return i->getString();
}

static std::string termName(const SNLBitTerm* t) {
  auto s = t->getString();
  // getString() returns "<term:id>" for unnamed ports — show direction instead
  if (!s.empty() && s[0] == '<') {
    switch (static_cast<SNLNetComponent::Direction::DirectionEnum>(t->getDirection())) {
      case SNLNetComponent::Direction::DirectionEnum::Input:  return "(in)";
      case SNLNetComponent::Direction::DirectionEnum::Output: return "(out)";
      case SNLNetComponent::Direction::DirectionEnum::InOut:  return "(inout)";
      default: return s;
    }
  }
  return s;
}

// Build a bit-term JSON node (shared by terms and occurrence entries).
static json bitTermJson(const SNLBitTerm* bt) {
  json t = {
    {"name",      termName(bt)},
    {"child_id",  static_cast<unsigned>(bt->getID())},
    {"direction", snlDirToInt(bt->getDirection())}
  };
  if (auto* btb = dynamic_cast<const SNLBusTermBit*>(bt))
    t["bit"] = static_cast<int>(btb->getBit());
  return t;
}

// Resolve the DB for a request's db_id (falls back to DB0 if not found in user DBs).
static NLDB* resolveDB(unsigned dbId) {
  auto* universe = NLUniverse::get();
  if (auto* db = universe->getDB(static_cast<NLID::DBID>(dbId))) return db;
  return nullptr;
}

// Recursively search all libraries in a DB for a design matching {libId, designId}.
static SNLDesign* findDesign(NLDB* db, NLID::LibraryID libId, NLID::DesignID designId) {
  std::function<SNLDesign*(NLLibrary*)> search = [&](NLLibrary* lib) -> SNLDesign* {
    if (lib->getID() == libId) {
      auto* d = lib->getSNLDesign(designId);
      if (d) return d;
    }
    for (auto* sub : lib->getLibraries())
      if (auto* d = search(sub)) return d;
    return nullptr;
  };
  for (auto* lib : db->getLibraries())
    if (auto* d = search(lib)) return d;
  return nullptr;
}

static void dumpDB(NLDB* db) {
  std::function<void(NLLibrary*, int)> dumpLib = [&](NLLibrary* lib, int indent) {
    std::string pad(indent * 2, ' ');
    Console::Log(pad + "lib id=" + std::to_string(lib->getID()) +
                 " name=" + lib->getString());
    for (auto* d : lib->getSNLDesigns())
      Console::Log(pad + "  design id=" + std::to_string(d->getID()) +
                   " name=" + d->getString() +
                   " terms=" + std::to_string(d->getTerms().size()));
    for (auto* sub : lib->getLibraries()) dumpLib(sub, indent + 1);
  };
  for (auto* db : NLUniverse::get()->getDBs()) {
    Console::Log("DB id=" + std::to_string(db->getID()));
    for (auto* lib : db->getLibraries()) dumpLib(lib, 1);
  }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

LocalSNLProvider::LocalSNLProvider() {
  if (!NLUniverse::get()) NLUniverse::create();
}

LocalSNLProvider::~LocalSNLProvider() {
  // SNL does not expose a public API to remove individual DBs;
  // the universe and its DBs are released when the process exits.
}

void LocalSNLProvider::on_open(std::function<void()> cb)                    { openCb_  = std::move(cb); }
void LocalSNLProvider::on_message(std::function<void(const std::string&)> cb){ msgCb_   = std::move(cb); }
void LocalSNLProvider::on_close(std::function<void()> cb)                   { closeCb_ = std::move(cb); }
void LocalSNLProvider::on_error(std::function<void(const std::string&)> cb)  { errCb_   = std::move(cb); }

void LocalSNLProvider::start() {
  Console::Log("LocalSNLProvider: starting in standalone mode");
  if (openCb_) openCb_();
}

void LocalSNLProvider::send(const std::string& msg) { handleRequest(msg); }

// ---------------------------------------------------------------------------
// DB management
// ---------------------------------------------------------------------------

NLDB* LocalSNLProvider::freshDB() {
  db_ = nullptr; // old DB remains in universe; SNL has no public single-DB removal API
  return NLDB::create(NLUniverse::get());
}

void LocalSNLProvider::findAndSetTop() {
  // Collect designs referenced by non-primitive instances
  std::set<SNLDesign*> instantiated;
  for (auto* lib : db_->getLibraries()) {
    for (auto* design : lib->getSNLDesigns()) {
      if (design->isPrimitive()) continue;
      for (auto* inst : design->getNonPrimitiveInstances())
        instantiated.insert(inst->getModel());
    }
  }

  // First non-primitive design not instantiated by anything else is the top
  for (auto* lib : db_->getLibraries()) {
    if (lib->isPrimitives()) continue;
    for (auto* design : lib->getSNLDesigns()) {
      if (design->isPrimitive()) continue;
      if (!instantiated.count(design)) {
        db_->setTopDesign(design);
        Console::Log("Top design: " + designName(design));
        return;
      }
    }
  }
  Console::Error("LocalSNLProvider: could not determine top design");
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

void LocalSNLProvider::loadSNL(const std::string& path) {
  Console::Log("LocalSNLProvider: loading SNL from " + path);
  try {
    db_ = nullptr; // old DB remains in universe (no public removal API)
    db_ = SNLCapnP::load(std::filesystem::path(path));
    if (!db_->getTopDesign())
      findAndSetTop();
    Console::Log("LocalSNLProvider: SNL loaded");
  } catch (const std::exception& e) {
    Console::Error("loadSNL failed: " + std::string(e.what()));
    db_ = nullptr;
  }
}

void LocalSNLProvider::loadVerilog(
    const std::vector<std::string>& verilogFiles,
    const std::vector<std::string>& libertyFiles)
{
  Console::Log("LocalSNLProvider: loading Verilog (" +
               std::to_string(verilogFiles.size()) + " files, " +
               std::to_string(libertyFiles.size()) + " liberty)");
  try {
    db_ = freshDB();

    if (!libertyFiles.empty()) {
      auto* primLib = NLLibrary::create(db_, NLLibrary::Type::Primitives, NLName("primitives"));
      SNLLibertyConstructor libCtor(primLib);
      std::vector<std::filesystem::path> paths(libertyFiles.begin(), libertyFiles.end());
      libCtor.construct(paths);
    }

    auto* userLib = NLLibrary::create(db_, NLName("work"));
    SNLVRLConstructor::Config cfg;
    cfg.allowUnknownDesigns_ = !libertyFiles.empty();
    SNLVRLConstructor vrlCtor(userLib);
    vrlCtor.config_ = cfg;
    std::vector<std::filesystem::path> paths(verilogFiles.begin(), verilogFiles.end());
    vrlCtor.construct(paths);

    findAndSetTop();
    Console::Log("LocalSNLProvider: Verilog loaded");
  } catch (const std::exception& e) {
    Console::Error("loadVerilog failed: " + std::string(e.what()));
    db_ = nullptr;
  }
}

static void expandFlist(const std::filesystem::path& flist,
                        std::vector<std::filesystem::path>& out) {
  std::ifstream f(flist);
  if (!f) { Console::Error("Cannot open flist: " + flist.string()); return; }
  auto baseDir = flist.parent_path();
  std::string line;
  while (std::getline(f, line)) {
    auto s = line.find_first_not_of(" \t");
    if (s == std::string::npos) continue;
    line = line.substr(s);
    auto e = line.find_last_not_of(" \t\r\n");
    if (e != std::string::npos) line.resize(e + 1);
    if (line.empty() || line[0] == '#' || line.substr(0, 2) == "//") continue;
    if (line[0] == '-') {
      // recurse into -f sublist
      if (line.size() > 3 && line[1] == 'f' && (line[2] == ' ' || line[2] == '\t')) {
        auto sub = std::filesystem::path(line.substr(3));
        expandFlist(sub.is_relative() ? baseDir / sub : sub, out);
      }
      continue; // skip -I, -D, and other flags
    }
    auto p = std::filesystem::path(line);
    out.push_back(p.is_relative() ? baseDir / p : p);
  }
}

void LocalSNLProvider::loadSystemVerilog(const std::vector<std::string>& sources) {
  Console::Log("LocalSNLProvider: loading SystemVerilog (" +
               std::to_string(sources.size()) + " source entries)");
  try {
    db_ = freshDB();

    // Expand Flist files; pass .sv/.v files through directly
    std::vector<std::filesystem::path> paths;
    for (const auto& s : sources) {
      std::filesystem::path p(s);
      auto ext = p.extension();
      if (ext == ".f" || ext == ".flist") {
        Console::Log("Expanding flist: " + p.string());
        expandFlist(p, paths);
      } else {
        paths.push_back(p);
      }
    }

    auto* userLib = NLLibrary::create(db_, NLName("work"));
    SNLSVConstructor svCtor(userLib);
    svCtor.construct(paths);

    findAndSetTop();
    Console::Log("LocalSNLProvider: SystemVerilog loaded (" +
                 std::to_string(paths.size()) + " files)");
  } catch (const std::exception& e) {
    Console::Error("loadSystemVerilog failed: " + std::string(e.what()));
    db_ = nullptr;
  }
}

// ---------------------------------------------------------------------------
// Request dispatch
// ---------------------------------------------------------------------------

void LocalSNLProvider::handleRequest(const std::string& jsonRequest) {
  if (!msgCb_) return;

  json req;
  try { req = json::parse(jsonRequest); }
  catch (...) {
    Console::Error("LocalSNLProvider: bad JSON: " + jsonRequest);
    return;
  }

  std::string request = req.value("request", "");

  if (request == "load_root") {
    msgCb_(buildRootResponse());
  } else if (request == "load_instances" || request == "load_primitives") {
    unsigned guiId = req.value("gui_id", 0u);
    unsigned dbId = 0, libId = 0, designId = 0;
    if (req.contains("design_ref")) {
      dbId     = req["design_ref"].value("db_id",      0u);
      libId    = req["design_ref"].value("library_id", 0u);
      designId = req["design_ref"].value("design_id",  0u);
    }
    msgCb_(buildInstancesResponse(guiId, dbId, libId, designId,
                                  request == "load_primitives"));
  } else if (request == "load_terms") {
    unsigned guiId = req.value("gui_id", 0u);
    unsigned dbId = 0, libId = 0, designId = 0;
    if (req.contains("design_ref")) {
      dbId     = req["design_ref"].value("db_id",      0u);
      libId    = req["design_ref"].value("library_id", 0u);
      designId = req["design_ref"].value("design_id",  0u);
    }
    msgCb_(buildTermsResponse(guiId, dbId, libId, designId));
  } else if (request == "load_equipotential") {
    msgCb_(buildEquipotentialResponse(req));
  } else {
    Console::Error("LocalSNLProvider: unknown request: " + request);
  }
}

// ---------------------------------------------------------------------------
// Response builders
// ---------------------------------------------------------------------------

std::string LocalSNLProvider::buildRootResponse() const {
  json root;
  auto* top = db_ ? db_->getTopDesign() : nullptr;
  if (!top) {
    root = {
      {"name", db_ ? "(failed to find top design)" : "(no netlist — use File > Open)"},
      {"design_ref", {{"db_id", 0}, {"library_id", 0}, {"design_id", 0}}},
      {"has_terms", false}, {"has_primitives", false}, {"has_instances", false}
    };
  } else {
    root = {
      {"name", designName(top)},
      {"design_ref", {
        {"db_id",      static_cast<unsigned>(db_->getID())},
        {"library_id", static_cast<unsigned>(top->getLibrary()->getID())},
        {"design_id",  static_cast<unsigned>(top->getID())}
      }},
      {"has_terms",      !top->getTerms().empty()},
      {"has_primitives", !top->getPrimitiveInstances().empty()},
      {"has_instances",  !top->getNonPrimitiveInstances().empty()}
    };
  }
  return json{{"response", "root_response"}, {"root", root}}.dump();
}

std::string LocalSNLProvider::buildInstancesResponse(
    unsigned guiId, unsigned dbId, unsigned libId, unsigned designId, bool primitives) const
{
  json children = json::array();

  auto* targetDB = resolveDB(dbId);
  if (targetDB) {
    auto* design = findDesign(targetDB, static_cast<NLID::LibraryID>(libId),
                                        static_cast<NLID::DesignID>(designId));
    if (!design) {
      Console::Error("buildInstancesResponse: design not found db=" + std::to_string(dbId) +
                     " lib=" + std::to_string(libId) + " design=" + std::to_string(designId));
      dumpDB(targetDB);
    }

    if (design) {
      auto instances = primitives
        ? design->getPrimitiveInstances()
        : design->getNonPrimitiveInstances();

      for (auto* inst : instances) {
        auto* model = inst->getModel();
        children.push_back({
          {"name",       instanceName(inst)},
          {"model_name", model ? designName(model) : ""},
          {"child_id",   static_cast<unsigned>(inst->getID())},
          {"design_ref", {
            {"db_id",      model ? static_cast<unsigned>(model->getDB()->getID()) : 0u},
            {"library_id", model ? static_cast<unsigned>(model->getLibrary()->getID()) : 0u},
            {"design_id",  model ? static_cast<unsigned>(model->getID()) : 0u}
          }},
          {"has_terms",      model && !model->getTerms().empty()},
          {"has_primitives", model && !model->getPrimitiveInstances().empty()},
          {"has_instances",  model && !model->getNonPrimitiveInstances().empty()}
        });
      }
    }
  }

  return json{
    {"response", primitives ? "primitives_response" : "instances_response"},
    {"gui_id",   guiId},
    {"children", children}
  }.dump();
}

std::string LocalSNLProvider::buildTermsResponse(
    unsigned guiId, unsigned dbId, unsigned libId, unsigned designId) const
{
  json children = json::array();

  auto* targetDB = resolveDB(dbId);
  if (targetDB) {
    auto* design = findDesign(targetDB, static_cast<NLID::LibraryID>(libId),
                                        static_cast<NLID::DesignID>(designId));
    if (!design) {
      Console::Error("buildTermsResponse: design not found db=" + std::to_string(dbId) +
                     " lib=" + std::to_string(libId) + " design=" + std::to_string(designId));
      dumpDB(targetDB);
    }

    if (design) {
      for (auto* term : design->getTerms()) {
        if (auto* bt = dynamic_cast<SNLBusTerm*>(term)) {
          // Bus term: emit as a single entry with msb/lsb
          json t = {
            {"name",      bt->getString()},
            {"child_id",  static_cast<unsigned>(bt->getID())},
            {"direction", snlDirToInt(bt->getDirection())},
            {"msb",       static_cast<int>(bt->getMSB())},
            {"lsb",       static_cast<int>(bt->getLSB())}
          };
          children.push_back(std::move(t));
        } else if (auto* st = dynamic_cast<SNLBitTerm*>(term)) {
          children.push_back(bitTermJson(st));
        }
      }
    }
  }

  return json{
    {"response", "terms_response"},
    {"gui_id",   guiId},
    {"children", children}
  }.dump();
}

std::string LocalSNLProvider::buildEquipotentialResponse(const json& req) const {
  static const json empty = {
    {"response",    "equipotential_response"},
    {"terms",       json::array()},
    {"occurrences", json::array()}
  };

  auto* topDesign = db_ ? db_->getTopDesign() : nullptr;
  if (!topDesign) return empty.dump();

  // Parse path (instance child IDs from top to the containing instance)
  SNLPath::PathIDDescriptor pathIDs;
  if (req.contains("path") && req["path"].is_array())
    for (auto& pid : req["path"])
      pathIDs.push_back(static_cast<NLID::DesignObjectID>(pid.get<unsigned>()));

  unsigned termId = req.value("term_id", 0u);
  bool hasBit = req.contains("bit");
  NLID::Bit bitVal = hasBit ? static_cast<NLID::Bit>(req["bit"].get<int>()) : 0;

  // Resolve the bit term
  auto resolveBitTerm = [&](SNLDesign* design) -> SNLBitTerm* {
    auto* term = design->getTerm(static_cast<NLID::DesignObjectID>(termId));
    if (!term) return nullptr;
    if (!hasBit) return dynamic_cast<SNLBitTerm*>(term);
    auto* bus = dynamic_cast<SNLBusTerm*>(term);
    return bus ? bus->getBit(bitVal) : nullptr;
  };

  try {
    if (pathIDs.empty()) {
      // Top-level term — compute flat equipotential from the bit term directly
      auto* bitTerm = resolveBitTerm(topDesign);
      if (!bitTerm) return empty.dump();

      SNLEquipotential equi(bitTerm);

      json terms = json::array();
      for (auto* bt : equi.getTermsSet()) terms.push_back(bitTermJson(bt));

      json occs = json::array();
      for (const auto& occ : equi.getInstTermOccurrencesSet()) {
        auto* it = occ.getInstTerm();
        auto* bt = it->getBitTerm();
        json pathArr = json::array();
        for (auto* inst : occ.getPath().getInstances())
          pathArr.push_back(json::array({inst->getString(),
                                         static_cast<unsigned>(inst->getID())}));
        auto entry = bitTermJson(bt);
        entry["path"] = std::move(pathArr);
        occs.push_back(std::move(entry));
      }
      return json{{"response","equipotential_response"},{"terms",terms},{"occurrences",occs}}.dump();

    } else {
      // Instance term — build occurrence and compute hierarchical equipotential
      SNLPath snlPath(topDesign, pathIDs);
      auto* model = snlPath.getModel();
      if (!model) return empty.dump();

      auto* bitTerm = resolveBitTerm(model);
      if (!bitTerm) return empty.dump();

      auto* tailInst = snlPath.getTailInstance();
      auto* instTerm = tailInst->getInstTerm(bitTerm);
      if (!instTerm) return empty.dump();

      SNLEquipotential equi(SNLOccurrence(snlPath.getHeadPath(), instTerm));

      json terms = json::array();
      for (auto* bt : equi.getTermsSet()) terms.push_back(bitTermJson(bt));

      json occs = json::array();
      for (const auto& occ : equi.getInstTermOccurrencesSet()) {
        auto* it = occ.getInstTerm();
        auto* bt = it->getBitTerm();
        json pathArr = json::array();
        for (auto* inst : occ.getPath().getInstances())
          pathArr.push_back(json::array({inst->getString(),
                                         static_cast<unsigned>(inst->getID())}));
        auto entry = bitTermJson(bt);
        entry["path"] = std::move(pathArr);
        occs.push_back(std::move(entry));
      }
      return json{{"response","equipotential_response"},{"terms",terms},{"occurrences",occs}}.dump();
    }
  } catch (const std::exception& e) {
    Console::Error("buildEquipotentialResponse: " + std::string(e.what()));
    return empty.dump();
  }
}

#endif // __EMSCRIPTEN__
