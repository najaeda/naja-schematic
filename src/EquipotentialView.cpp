// EquipotentialView.cpp
#include "EquipotentialView.h"

#include <imgui.h>

#include "Types.h"
#include "SchematicView.h"

static SchematicView g_schematic;

namespace {

ImColor getTopTermColor(Direction direction) {
    switch (direction) {
        case Direction::Input:  return ImColor(255, 0, 0);   // Red
        case Direction::Output: return ImColor(0, 255, 0);   // Green
        case Direction::Inout:  return ImColor(255, 255, 0); // Yellow
        default:                return ImColor(255, 255, 255); // White
    }
}

ImColor getInstTermOccurrenceColor(Direction direction) {
    switch (direction) {
        case Direction::Input:  return ImColor(0, 255, 0);   // Green
        case Direction::Output: return ImColor(255, 0, 0);   // Red
        case Direction::Inout:  return ImColor(255, 255, 0); // Yellow
        default:                return ImColor(255, 255, 255); // White
    }
}

} // anonymous namespace

void EquipotentialView::render(Equipotential* equipotential) {
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 10.0f) canvasSize.x = 10.0f;
    if (canvasSize.y < 10.0f) canvasSize.y = 10.0f;

    ImGui::InvisibleButton("net_canvas", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetItemRectMin();

    g_schematic.handleInteraction(canvasPos, canvasSize);

    // --- Populate renderer instances/nets from equipotential terms + occurrences ---
    // Rebuild renderer contents each frame from the current equipotential.
    // For each term and each occurrence we create one InstanceShape (square) and one Port associated with it.
    g_schematic.instances.clear();
    g_schematic.nets.clear();

    // Quick debug overlay: show counts so we can see if equipotential is present
    if (!equipotential) {
        dl->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 8.0f), IM_COL32(255, 100, 100, 255), "");
    } else {
        std::string dbg = "terms: " + std::to_string(equipotential->terms.size()) + " occ: " + std::to_string(equipotential->occurrences.size());
        dl->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 8.0f), IM_COL32(200, 200, 200, 255), dbg.c_str());
    }

    if (equipotential) {
        // Layout parameters
        const float leftMargin = 20.0f;
        const float columnGap = 120.0f;
        const float rowSpacing = 24.0f;
        const float instW = 180.0f;
        const float instH = 70.0f;

        int nextInstanceId = 1;
        int nextPortId = 1;

        // Keep a list of (instanceId, portId) for all created ports so we can connect them to the same net
        struct PortRef { int instanceId; int portId; };
        std::vector<PortRef> allPortRefs;
        allPortRefs.reserve(equipotential->terms.size() + equipotential->occurrences.size());

        struct Item {
            std::string label;
            std::string fullName;
            Direction direction;
            bool isTerm;
        };

        std::vector<Item> drivers;
        std::vector<Item> receivers;

        for (const auto& bitTerm : equipotential->terms) {
            Item item;
            item.label = bitTerm.getString();
            item.fullName = bitTerm.name;
            item.direction = bitTerm.direction;
            item.isTerm = true;
            if (bitTerm.direction == Direction::Input) {
                drivers.push_back(std::move(item));
            } else {
                receivers.push_back(std::move(item));
            }
        }

        for (const auto& occ : equipotential->occurrences) {
            Item item;
            item.label = occ.term.getString();
            std::string concatedPaths;
            bool first = true;
            for (const auto& name : occ.path) {
                if (!first) {
                    concatedPaths += "/";
                }
                concatedPaths += name;
                first = false;
            }
            item.fullName = concatedPaths;
            item.direction = occ.term.direction;
            item.isTerm = false;
            if (occ.term.direction == Direction::Output) {
                drivers.push_back(std::move(item));
            } else {
                receivers.push_back(std::move(item));
            }
        }

        const size_t totalItems = drivers.size() + receivers.size();
        if (totalItems < 64 /*Error out otherwise*/) {
            const float columnWidth = instW;
            const float rightColumnX = leftMargin + columnWidth + columnGap;
            const float leftColumnX = leftMargin;
            const float maxRows = static_cast<float>(std::max(drivers.size(), receivers.size()));
            const float totalHeight = (maxRows * instH) + ((maxRows > 0 ? (maxRows - 1) : 0) * rowSpacing);
            const float startY = (canvasSize.y - totalHeight) * 0.5f;

            auto addItem = [&](const Item& item, float x, float y) {
                InstanceShape inst;
                inst.id = nextInstanceId++;
                inst.name = item.isTerm ? std::string() : (item.fullName.empty() ? item.label : item.fullName);
                inst.x = x;
                inst.y = y;
                inst.w = item.isTerm ? 0.0f : instW;
                inst.h = item.isTerm ? 0.0f : instH;
                if (item.isTerm) {
                    inst.color = IM_COL32(0, 0, 0, 0);
                } else {
                    inst.color = IM_COL32(100, 140, 200, 255);
                }

                Port p;
                p.id = nextPortId++;
                p.name = item.label;
                bool portOnRight = (item.direction == Direction::Output);
                p.lx = portOnRight ? 0.5f : -0.5f;
                p.ly = 0.0f;
                p.direction = item.direction;

                bool isDriving = (item.isTerm && item.direction == Direction::Input) ||
                                 (!item.isTerm && item.direction == Direction::Output);
                p.isInput = isDriving;

                inst.ports.push_back(p);
                g_schematic.instances.push_back(inst);
                allPortRefs.push_back({ inst.id, p.id });
            };

            for (size_t i = 0; i < drivers.size(); ++i) {
                float y = startY + i * (instH + rowSpacing);
                addItem(drivers[i], leftColumnX, y);
            }

            for (size_t i = 0; i < receivers.size(); ++i) {
                float y = startY + i * (instH + rowSpacing);
                addItem(receivers[i], rightColumnX, y);
            }
        }

        // --- Connect all ports to the same net (star topology) ---
        // If there are at least two ports, choose the first port as the hub and create nets from hub to every other port.
        if (allPortRefs.size() >= 2) {
            const PortRef hub = allPortRefs.front();
            for (size_t i = 1; i < allPortRefs.size(); ++i) {
                const PortRef& other = allPortRefs[i];
                NetWire n;
                n.id = static_cast<int>(g_schematic.nets.size()) + 1;
                // connect hub -> other (direction is visual only)
                n.srcInstance = hub.instanceId;
                n.srcPortId = hub.portId;
                n.dstInstance = other.instanceId;
                n.dstPortId = other.portId;
                n.color = IM_COL32(200, 200, 100, 255);
                g_schematic.nets.push_back(n);
            }
        }
    } // end if equipotential

    static int lastTotalItems = -1;
    int currentTotalItems = equipotential
        ? static_cast<int>(equipotential->terms.size() + equipotential->occurrences.size())
        : 0;
    if (currentTotalItems != lastTotalItems) {
        g_schematic.requestFit(true);
        lastTotalItems = currentTotalItems;
    }
    g_schematic.updateFitIfNeeded(canvasPos, canvasSize, 60.0f);

    // Render the netlist canvas (instances + ports + nets)
    g_schematic.render(dl, canvasPos, canvasSize);

    // --- Table: show top-level terms first, then occurrences ---
    if (ImGui::BeginTable("equipotential_table", 4,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("Index");
        ImGui::TableSetupColumn("Instance/Term");
        ImGui::TableSetupColumn("Port");
        ImGui::TableSetupColumn("Direction");
        ImGui::TableHeadersRow();

        if (equipotential) {
            // Terms
            for (size_t t = 0; t < equipotential->terms.size(); ++t) {
                const auto& term = equipotential->terms[t];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("T%zu", t + 1);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", term.name.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", term.getString().c_str());
                ImGui::TableSetColumnIndex(3);
                ImColor c = getTopTermColor(term.direction);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.Value.x, c.Value.y, c.Value.z, c.Value.w));
                ImGui::Text("%s", toString(term.direction));
                ImGui::PopStyleColor();
            }

            // Occurrences
            for (size_t idx = 0; idx < equipotential->occurrences.size(); ++idx) {
                const auto& occ = equipotential->occurrences[idx];
                const auto& term = occ.term;

                std::string instanceName = "(unknown)";
                if (!occ.path.empty()) instanceName = occ.path.back();

                std::string portName = term.getString();

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%zu", idx + 1);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", instanceName.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", portName.c_str());
                ImGui::TableSetColumnIndex(3);
                ImColor c = getInstTermOccurrenceColor(term.direction);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.Value.x, c.Value.y, c.Value.z, c.Value.w));
                ImGui::Text("%s", toString(term.direction));
                ImGui::PopStyleColor();
            }
        }

        ImGui::EndTable();
    }
}
