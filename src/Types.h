#pragma once

#include <string>
#include <optional>
#include <vector>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

//
// --- API / JSON types (kept first) ---
//

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
    return name + (bit.has_value() ? ("[" + std::to_string(bit.value()) + "]") : "");
  }

  std::string getDebugString() const {
    return name + " (child_id: " + std::to_string(child_id) +
           ", direction: " + toString(direction) +
           (bit.has_value() ? ", bit: " + std::to_string(bit.value()) : "") + ")";
  }
};

using Path = std::vector<std::string>;

struct InstTermOccurrence {
  Path path;                       // instance names (display)
  std::vector<unsigned> pathIds;   // instance child_ids (used to send load_equipotential)
  BitTerm term;
  DesignRef designRef;             // model of the tail instance — used to fetch its full interface
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

//
// --- Renderer / UI types (kept separate from the JSON / API types above) ---
//

#include <imgui.h>

struct Port {
    int id = 0;
    std::string name;
    float lx = 0.0f;    // normalized local x (-0.5..0.5)
    float ly = 0.0f;    // normalized local y (-0.5..0.5)
    Direction direction = Direction::Inout; // logical direction for geometry
    bool isInput = false; // used by renderer to pick red/green
    ImU32 color = 0;      // optional explicit color override (0 == no override)
};

struct InstanceShape {
    int id = 0;
    std::string name;
    float x = 0.0f;
    float y = 0.0f;  // world coords (top-left)
    float w = 100.0f;
    float h = 50.0f;  // size in world units
    ImU32 color = IM_COL32(120,120,120,255);
    // When true, only a subset of ports is shown (e.g. only those on the
    // current net).  The renderer draws a dashed border so the user knows
    // the instance can be expanded to reveal its full interface.
    bool partialInterface = false;
    std::vector<Port> ports;
};

struct NetWire {
    int id = 0;
    int srcInstance = 0;
    int srcPortId = 0;
    int dstInstance = 0;
    int dstPortId = 0;
    ImU32 color = IM_COL32(200,200,100,255);
};

// Renderer-side term/occurrence types (used only by the UI renderer)
struct Term {
    Direction direction = Direction::Inout;
    std::string name;
    std::string getString() const { return name; }
};

struct Occurrence {
    std::vector<std::string> path; // instance path, last element is instance name
    Term term;
};

// Renderer-side equipotential (keeps renderer expectations separate from API types)
struct RenderEquipotential {
    std::vector<Term> terms;
    std::vector<Occurrence> occurrences;
};

/* JSON deserializers (declarations kept for project) */
void from_json(const json& j, DesignRef& d);
void from_json(const json& j, InstanceResponseJson& r);
void from_json(const json& j, InstancesResponseJson& r);
void from_json(const json& j, TermsResponseJson& r);
void from_json(const json& j, Equipotential& e);
