#pragma once

#include <string>

class NetlistTree;
class Equipotential;

struct GUIData {
  public:
    std::string getString() const;
    NetlistTree*    netlist_;
    Equipotential*  equipotential_; 
};