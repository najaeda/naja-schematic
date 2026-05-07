#pragma once

#include <string>
#include <vector>

class NetlistTree;
struct Equipotential;

struct GUIData {
  public:
    std::string getString() const;

    void addEquipotential(Equipotential* eq);
    void clearEquipotentials();

    NetlistTree*                  netlist_       {nullptr};
    std::vector<Equipotential*>   equipotentials_;
};
