#pragma once

#include <string>
#include <vector>
#include "Types.h"

class Equipotential;
class INetlistProvider;

class EquipotentialView {
  public:
    // A single port from a fully-loaded instance interface.
    struct ExpandedPort {
      std::string          name;
      Direction            direction = Direction::Inout;
      unsigned             childId   = 0;      // term child_id on the model
      std::optional<int>   bit;                // set for bus bits
    };

    static void renderSchematic(Equipotential* equipotential);
    static void renderTable(Equipotential* equipotential);
    static void zoomIn();
    static void zoomOut();
    static void fitView();

    // Called once during app setup so the view can send expansion requests.
    static void setProvider(INetlistProvider* provider);

    // Called by AppLogic when an expanded_instance_terms response arrives.
    static void applyInstanceExpansion(const std::string& pathKey,
                                       const std::vector<ExpandedPort>& ports);
};
