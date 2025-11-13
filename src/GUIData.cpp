#include "GUIData.h"

std::string GUIData::getString() const {
  std::string result = "GUIData:\n";
  if (netlist_) {
    result += "  NetlistTree: loaded\n";
  } else {
    result += "  NetlistTree: null\n";
  }
  if (equipotential_) {
    result += "  Equipotential: loaded\n";
  } else {
    result += "  Equipotential: null\n";
  }
  return result;
}