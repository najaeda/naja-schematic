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