#pragma once

#include <string>
#include <optional>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

struct DesignRef {
    int db_id;
    int library_id;
    int design_id;
};

struct InstanceResponseJson {
    std::optional<std::string> name;
    int child_id;
    std::optional<std::string> model_name;
    DesignRef design_ref;
    bool has_primitives;
    bool has_instances;
    bool has_terms;
};

void from_json(const json& j, DesignRef& d);
void from_json(const json& j, InstanceResponseJson& r);