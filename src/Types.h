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

inline const char* toString(Direction dir) {
  switch (dir) {
    case Direction::Input:  return "Input";
    case Direction::Output: return "Output";
    case Direction::Inout:  return "Inout";
    default:                return "Unknown";
  }
}

struct BitTerm {
  std::string name;
  unsigned child_id;
  Direction direction;
  std::optional<int> bit;

  std::string getString() const {
    return name + (bit.has_value() ? ("[" + std::to_string(bit.value()) + "]") : "") ;
  }

  std::string getDebugString() const {
    return name + " (child_id: " + std::to_string(child_id) +
           ", direction: " + toString(direction) +
           (bit.has_value() ? ", bit: " + std::to_string(bit.value()) : "") + ")";
  }
};

using Path = std::vector<std::string>;

struct InstTermOccurrence {
  Path path;
  BitTerm term;
};

struct TermResponseJson {
  std::string name;
  unsigned child_id;
  Direction direction;
  std::optional<int> msb;
  std::optional<int> lsb;
};

struct TermsResponseJson {
  bool found;
  int gui_id;
  std::vector<TermResponseJson> children;
};

struct Equipotential {
  bool found;
  std::vector<BitTerm> terms;
  std::vector<InstTermOccurrence> occurrences;
};

void from_json(const json& j, DesignRef& d);
void from_json(const json& j, InstanceResponseJson& r);
void from_json(const json& j, InstancesResponseJson& r);
void from_json(const json& j, TermsResponseJson& r);
void from_json(const json& j, Equipotential& e);