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

    static void renderSchematic(const std::vector<Equipotential*>& equipotentials);
    static void renderTable(const std::vector<Equipotential*>& equipotentials);
    static void zoomIn();
    static void zoomOut();
    static void fitView();
    static void clearNets();
    // Returns true (and resets the flag) if the canvas right-click requested a clear.
    static bool takePendingClear();
    // Clears persistent layout state immediately (no deferred flag).
    // Use this when the equipotentials vector is also being cleared synchronously.
    static void resetLayout();

    // Called once during app setup so the view can send expansion requests.
    static void setProvider(INetlistProvider* provider);

    // Called by AppLogic when an expanded_instance_terms response arrives.
    static void applyInstanceExpansion(const std::string& pathKey,
                                       const std::vector<ExpandedPort>& ports);
};
