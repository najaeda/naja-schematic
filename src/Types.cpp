#include "Types.h"

#include "Console.h"

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

  j.at("child_id").get_to(r.child_id);
  j.at("design_ref").get_to(r.design_ref);
  j.at("has_primitives").get_to(r.has_primitives);
  j.at("has_instances").get_to(r.has_instances);
  j.at("has_terms").get_to(r.has_terms);
}

void from_json(const json& j, InstancesResponseJson& instances) {
  j.at("found").get_to(instances.found);
  j.at("gui_id").get_to(instances.gui_id);

  if (j.contains("children") && j["children"].is_array()) {
    for (const auto& child : j["children"]) {
      instances.children.push_back(child.get<InstanceResponseJson>());
    }
  }  
}

void from_json(const json& j, TermsResponseJson& t) {
  j.at("found").get_to(t.found);
  j.at("gui_id").get_to(t.gui_id);

  if (j.contains("children") && j["children"].is_array()) {
    for (const auto& child : j["children"]) {
      TermResponseJson term;

      if (child.contains("name") && !child["name"].is_null()) {
        term.name = child["name"].get<std::string>();
      }
      child.at("child_id").get_to(term.child_id);
      child.at("direction").get_to(term.direction);

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