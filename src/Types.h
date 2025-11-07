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
  std::string name;
  unsigned child_id;
  std::string model_name;
  DesignRef design_ref;
  bool has_primitives;
  bool has_instances;
  bool has_terms;
};

struct InstancesResponseJson {
  bool found;
  int gui_id;
  std::vector<InstanceResponseJson> children;
};

enum class Direction {
  Input,
  Output,
  Inout
};

struct TermResponseJson {
  std::string name;
  unsigned child_id;
  char direction;
  std::optional<int> msb;
  std::optional<int> lsb;
};

struct TermsResponseJson {
  bool found;
  int gui_id;
  std::vector<TermResponseJson> children;
};

void from_json(const json& j, DesignRef& d);
void from_json(const json& j, InstanceResponseJson& r);
void from_json(const json& j, InstancesResponseJson& r);
void from_json(const json& j, TermsResponseJson& r);