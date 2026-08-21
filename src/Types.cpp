#include "Types.h"

#include "Console.h"

Direction intToDirection(int dirInt) {
  switch (dirInt) {
    case 0:
      return Direction::Input;
    case 1:
      return Direction::Output;
    case 2:
      return Direction::Inout;
    default:
      Console::Error("Invalid direction: " + std::to_string(dirInt) + ", defaulting to Input");
      return Direction::Input;
  }
}

void from_json(const json& j, DesignRef& d) {
  j.at("db_id").get_to(d.db_id);
  j.at("library_id").get_to(d.library_id);
  j.at("design_id").get_to(d.design_id);
}

void from_json(const json& j, InstanceResponseJson& r) {
  if (j.contains("name") && !j["name"].is_null())
    r.name = j["name"].get<std::string>();
  if (j.contains("model_name") && !j["model_name"].is_null())
    r.model_name = j["model_name"].get<std::string>();

  r.child_id = j.value("child_id", 0u);
  j.at("design_ref").get_to(r.design_ref);
  j.at("has_primitives").get_to(r.has_primitives);
  j.at("has_instances").get_to(r.has_instances);
  j.at("has_terms").get_to(r.has_terms);
}

void from_json(const json& j, InstancesResponseJson& instances) {
  j.at("gui_id").get_to(instances.gui_id);

  if (j.contains("children") && j["children"].is_array()) {
    for (const auto& child : j["children"]) {
      instances.children.push_back(child.get<InstanceResponseJson>());
    }
  }  
}

void from_json(const json& j, TermsResponseJson& t) {
  j.at("gui_id").get_to(t.gui_id);

  if (j.contains("children") && j["children"].is_array()) {
    for (const auto& child : j["children"]) {
      TermResponseJson term;

      if (child.contains("name") && !child["name"].is_null()) {
        term.name = child["name"].get<std::string>();
      }
      child.at("child_id").get_to(term.child_id);

      if (child.contains("direction") && !child["direction"].is_null()) {
        term.direction = intToDirection(child["direction"].get<int>());
      }

      if (child.contains("msb") && !child["msb"].is_null()) {
        term.msb = child["msb"].get<int>();
      } else {
        term.msb = std::nullopt;
      }

      if (child.contains("lsb") && !child["lsb"].is_null()) {
        term.lsb = child["lsb"].get<int>();
      } else {
        term.lsb = std::nullopt;
      }

      t.children.push_back(std::move(term));
    }
  }
}

void from_json(const json& j, Equipotential& e) {
  if (j.contains("terms") && j["terms"].is_array()) {
    for (const auto& termJson : j["terms"]) {
      BitTerm term;
      if (termJson.contains("name") && !termJson["name"].is_null()) {
        term.name = termJson["name"].get<std::string>();
      }
      if (termJson.contains("direction") && !termJson["direction"].is_null()) {
        term.direction = intToDirection(termJson["direction"].get<int>());
      }
      if (termJson.contains("bit") && !termJson["bit"].is_null()) {
        term.bit = termJson["bit"].get<int>();
      } else {
        term.bit = std::nullopt;
      }

      e.terms.push_back(std::move(term));
    }

    for (const auto& occJson : j["occurrences"]) {
      InstTermOccurrence occurrence;

      if (occJson.contains("path") && occJson["path"].is_array()) {
        for (const auto& pathElem : occJson["path"]) {
          //pathElem is a table of 2 elements: {name:..., child_id:...}
          if (!pathElem.is_array()) {
            continue;
          }
          const auto& name     = pathElem[0].get<std::string>();
          const auto& child_id = pathElem[1].get<unsigned>();
          occurrence.path.push_back(name);
          occurrence.pathIds.push_back(child_id);
        }
      }

      BitTerm term;
      if (occJson.contains("name") && !occJson["name"].is_null()) {
        term.name = occJson["name"].get<std::string>();
      }
      if (occJson.contains("child_id") && !occJson["child_id"].is_null()) {
        term.child_id = occJson["child_id"].get<unsigned>();
      }
      if (occJson.contains("direction") && !occJson["direction"].is_null()) {
        term.direction = intToDirection(occJson["direction"].get<int>());
      }
      if (occJson.contains("bit") && !occJson["bit"].is_null()) {
        term.bit = occJson["bit"].get<int>();
      } else {
        term.bit = std::nullopt;
      }

      Console::Log("Parsed term in occurrence: " + term.getString());

      occurrence.term = std::move(term);

      if (occJson.contains("design_ref") && occJson["design_ref"].is_object())
        occurrence.designRef = occJson["design_ref"].get<DesignRef>();

      e.occurrences.push_back(std::move(occurrence));
    }
  }
}

static DiagnosisKind diagnosisKindFromString(const std::string& s) {
  if (s == "net") return DiagnosisKind::Net;
  return DiagnosisKind::Instance;
}

static DiagnosisSeverity diagnosisSeverityFromString(const std::string& s) {
  if (s == "error")   return DiagnosisSeverity::Error;
  if (s == "warning") return DiagnosisSeverity::Warning;
  return DiagnosisSeverity::Info;
}

void from_json(const json& j, DiagnosisItem& d) {
  d.kind = diagnosisKindFromString(j.value("kind", std::string("instance")));

  d.path.clear();
  if (j.contains("path") && j["path"].is_array()) {
    for (const auto& seg : j["path"]) {
      if (seg.is_string()) d.path.push_back(seg.get<std::string>());
    }
  }

  d.terminal = j.value("terminal", std::string(""));
  d.severity = diagnosisSeverityFromString(j.value("severity", std::string("info")));
  d.message  = j.value("message", std::string(""));
  d.source   = j.value("source", std::string(""));
}