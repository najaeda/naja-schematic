// EquipotentialView.cpp
#include "EquipotentialView.h"

#include <algorithm>
#include <map>
#include <set>
#include <imgui.h>

#include "Types.h"
#include "SchematicView.h"
#include "INetlistProvider.h"

// ---------------------------------------------------------------------------
// Static view state
// ---------------------------------------------------------------------------

static SchematicView      g_schematic;
static int                g_pendingZoomSteps = 0;
static bool               g_pendingFit       = false;
static INetlistProvider*  g_provider         = nullptr;

// Per-frame mapping: InstanceShape.id → occurrence info needed for expansion.
struct OccurrenceInfo {
    std::string pathKey;
    DesignRef   designRef;
};
static std::map<int, OccurrenceInfo> g_occInfoByShapeId;

// Per-frame mapping: port internal id → data needed to send load_equipotential.
struct PortEquiRequest {
    std::vector<unsigned> pathIds;   // instance child_ids (empty for top-level terms)
    unsigned              termId  = 0;
    std::optional<int>    bit;
};
static std::map<int, PortEquiRequest> g_portEquiByPortId;

// Persistent expansion data: pathKey → all ports of that instance's model.
static std::map<std::string, std::vector<EquipotentialView::ExpandedPort>> g_expandedInstances;

// Requests in-flight (avoid duplicate requests).
static std::set<std::string> g_pendingExpansions;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void EquipotentialView::zoomIn()  { g_pendingZoomSteps += 1; }
void EquipotentialView::zoomOut() { g_pendingZoomSteps -= 1; }
void EquipotentialView::fitView() { g_pendingFit = true; }

void EquipotentialView::setProvider(INetlistProvider* provider) {
    g_provider = provider;
}

void EquipotentialView::applyInstanceExpansion(
        const std::string& pathKey,
        const std::vector<ExpandedPort>& ports) {
    g_expandedInstances[pathKey] = ports;
    g_pendingExpansions.erase(pathKey);
}

// ---------------------------------------------------------------------------
// Table color helpers
// ---------------------------------------------------------------------------

namespace {

ImColor getTopTermColor(Direction direction) {
    switch (direction) {
        case Direction::Input:  return ImColor(255, 0, 0);
        case Direction::Output: return ImColor(0, 255, 0);
        case Direction::Inout:  return ImColor(255, 255, 0);
        default:                return ImColor(255, 255, 255);
    }
}

ImColor getInstTermOccurrenceColor(Direction direction) {
    switch (direction) {
        case Direction::Input:  return ImColor(0, 255, 0);
        case Direction::Output: return ImColor(255, 0, 0);
        case Direction::Inout:  return ImColor(255, 255, 0);
        default:                return ImColor(255, 255, 255);
    }
}

// Distribute n items evenly in [-0.4, +0.4].
float portLy(int i, int n) {
    return n > 1 ? -0.4f + 0.8f * static_cast<float>(i) / static_cast<float>(n - 1) : 0.0f;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Schematic render
// ---------------------------------------------------------------------------

void EquipotentialView::renderSchematic(Equipotential* equipotential) {
    const float scrollSz = ImGui::GetFrameHeight();
    ImVec2 availSize = ImGui::GetContentRegionAvail();
    ImVec2 canvasSize = ImVec2(std::max(10.0f, availSize.x - scrollSz),
                               std::max(10.0f, availSize.y - scrollSz));
    ImVec2 cursorStart = ImGui::GetCursorPos();

    ImGui::BeginChild("##SchematicCanvas", canvasSize, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 innerSize = ImGui::GetContentRegionAvail();

    ImGui::InvisibleButton("net_canvas", innerSize,
                           ImGuiButtonFlags_MouseButtonLeft  |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    ImDrawList* dl      = ImGui::GetWindowDrawList();
    ImVec2      canvasPos = ImGui::GetItemRectMin();

    g_schematic.handleInteraction(canvasPos, innerSize);

    if (g_pendingZoomSteps != 0) {
        int   steps  = g_pendingZoomSteps;
        g_pendingZoomSteps = 0;
        float factor = steps > 0 ? 1.1f : 0.9f;
        for (int i = 0; i < std::abs(steps); ++i)
            g_schematic.zoomBy(factor);
    }
    if (g_pendingFit) {
        g_pendingFit = false;
        g_schematic.requestFit(true);
    }

    // -----------------------------------------------------------------------
    // Double-click handling (port dot → load equipotential;
    //                        instance box → expand interface)
    // -----------------------------------------------------------------------
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
        g_provider) {

        // Convert mouse screen position to world coordinates.
        ImVec2 mouse = ImGui::GetMousePos();
        float  s     = g_schematic.transform.scale;
        float  wx    = (mouse.x - canvasPos.x) / s
                       + g_schematic.transform.offset.x
                       - g_schematic.transform.screenOrigin.x / s;
        float  wy    = (mouse.y - canvasPos.y) / s
                       + g_schematic.transform.offset.y
                       - g_schematic.transform.screenOrigin.y / s;

        // --- Pass 1: port-dot hit test → load equipotential ---
        // Use a hit radius equivalent to ~10 screen pixels in world space.
        const float hitR  = 10.0f / std::max(0.01f, s);
        const float hitR2 = hitR * hitR;
        bool portHit = false;

        for (const auto& inst : g_schematic.instances) {
            for (const auto& port : inst.ports) {
                ImVec2 pw = g_schematic.portWorldPos(inst, port);
                float dx = wx - pw.x, dy = wy - pw.y;
                if (dx * dx + dy * dy > hitR2) continue;

                auto dataIt = g_portEquiByPortId.find(port.id);
                if (dataIt == g_portEquiByPortId.end()) break;

                const PortEquiRequest& req = dataIt->second;
                json j;
                j["request"]  = "load_equipotential";
                j["path"]     = req.pathIds;
                j["term_id"]  = req.termId;
                if (req.bit.has_value())
                    j["bit"] = req.bit.value();
                g_provider->send(j.dump());
                portHit = true;
                break;
            }
            if (portHit) break;
        }

        // --- Pass 2: instance-box hit test → expand interface (if no port hit) ---
        if (!portHit) {
            for (const auto& inst : g_schematic.instances) {
                if (!inst.partialInterface) continue;
                if (wx < inst.x || wx > inst.x + inst.w) continue;
                if (wy < inst.y || wy > inst.y + inst.h) continue;

                auto infoIt = g_occInfoByShapeId.find(inst.id);
                if (infoIt == g_occInfoByShapeId.end()) break;

                const OccurrenceInfo& info = infoIt->second;
                if (g_pendingExpansions.count(info.pathKey)) break;

                g_pendingExpansions.insert(info.pathKey);
                json req;
                req["request"]                  = "expand_instance_terms";
                req["path_key"]                 = info.pathKey;
                req["design_ref"]["db_id"]      = info.designRef.db_id;
                req["design_ref"]["library_id"] = info.designRef.library_id;
                req["design_ref"]["design_id"]  = info.designRef.design_id;
                g_provider->send(req.dump());
                break;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Rebuild schematic geometry from equipotential data
    // -----------------------------------------------------------------------
    g_schematic.instances.clear();
    g_schematic.nets.clear();
    g_occInfoByShapeId.clear();
    g_portEquiByPortId.clear();

    if (!equipotential) {
        dl->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 8.0f),
                    IM_COL32(255, 100, 100, 255), "");
    } else {
        std::string dbg = "terms: " + std::to_string(equipotential->terms.size())
                        + "  occ: " + std::to_string(equipotential->occurrences.size());
        dl->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 8.0f),
                    IM_COL32(200, 200, 200, 255), dbg.c_str());
    }

    if (equipotential) {
        const float leftMargin  = 20.0f;
        const float columnGap   = 120.0f;
        const float rowSpacing  = 24.0f;
        const float instW       = 180.0f;
        const float instH       = 70.0f;
        const float termH       = 30.0f;
        const float portSpacing = 18.0f; // world units per port row when expanded

        int nextInstanceId = 1;
        int nextPortId     = 1;

        struct PortRef { int instanceId; int portId; };
        std::vector<PortRef> allPortRefs;
        allPortRefs.reserve(equipotential->terms.size() + equipotential->occurrences.size());

        struct Item {
            std::string            label;
            std::string            fullName;
            Direction              direction  = Direction::Inout;
            bool                   isTerm     = false;
            DesignRef              designRef{};
            // Data needed to send load_equipotential when a port is clicked.
            unsigned               termChildId = 0;
            std::optional<int>     termBit;
            std::vector<unsigned>  pathIds;   // empty for top-level terms
        };

        std::vector<Item> drivers;
        std::vector<Item> receivers;

        for (const auto& bitTerm : equipotential->terms) {
            Item item;
            item.label       = bitTerm.getString();
            item.fullName    = bitTerm.name;
            item.direction   = bitTerm.direction;
            item.isTerm      = true;
            item.termChildId = bitTerm.child_id;
            item.termBit     = bitTerm.bit;
            // pathIds stays empty — top-level terms have no instance path
            if (bitTerm.direction == Direction::Input)
                drivers.push_back(std::move(item));
            else
                receivers.push_back(std::move(item));
        }

        for (const auto& occ : equipotential->occurrences) {
            Item item;
            item.label       = occ.term.getString();
            item.isTerm      = false;
            item.designRef   = occ.designRef;
            item.termChildId = occ.term.child_id;
            item.termBit     = occ.term.bit;
            item.pathIds     = occ.pathIds;
            std::string joined;
            bool first = true;
            for (const auto& name : occ.path) {
                if (!first) joined += '/';
                joined += name;
                first = false;
            }
            item.fullName  = joined;
            item.direction = occ.term.direction;
            if (occ.term.direction == Direction::Output)
                drivers.push_back(std::move(item));
            else
                receivers.push_back(std::move(item));
        }

        const size_t totalItems = drivers.size() + receivers.size();
        if (totalItems < 64) {
            const float leftColumnX  = leftMargin;
            const float rightColumnX = leftMargin + instW + columnGap;

            // Helper: how tall does an item need to be (accounts for expansion).
            auto itemHeight = [&](const Item& item) -> float {
                if (item.isTerm) return termH;
                const std::string& key = item.fullName.empty() ? item.label : item.fullName;
                auto it = g_expandedInstances.find(key);
                if (it == g_expandedInstances.end() || it->second.empty())
                    return instH;
                // Separate into left (input) and right (output) groups.
                int nLeft = 0, nRight = 0;
                for (const auto& ep : it->second) {
                    if (ep.direction == Direction::Input) ++nLeft; else ++nRight;
                }
                int maxSide = std::max(nLeft, nRight);
                return std::max(instH, static_cast<float>(maxSide) * portSpacing + 10.0f);
            };

            // Compute column heights for vertical centering.
            auto colHeight = [&](const std::vector<Item>& col) -> float {
                float h = 0;
                for (size_t i = 0; i < col.size(); ++i) {
                    h += itemHeight(col[i]);
                    if (i + 1 < col.size()) h += rowSpacing;
                }
                return h;
            };
            float totalH   = std::max(colHeight(drivers), colHeight(receivers));
            float startY   = (innerSize.y - totalH) * 0.5f;

            // addItem: create InstanceShape + ports, register occurrence info.
            auto addItem = [&](const Item& item, float x, float y) {
                // Signal-flow rule (matches column assignment above):
                //   Top-level term,  Input  direction  → drives the internal net   (driver)
                //   Top-level term,  Output direction  → receives from internal net (receiver)
                //   Instance port,   Output direction  → drives the net             (driver)
                //   Instance port,   Input  direction  → receives from the net      (receiver)
                // The apparent inversion for top terms is intentional: an Input port
                // of the module boundary is a SOURCE inside the module.
                bool isDriving = (item.isTerm  && item.direction == Direction::Input) ||
                                 (!item.isTerm && item.direction == Direction::Output);

                InstanceShape inst;
                inst.id = nextInstanceId++;
                inst.x  = x;

                if (item.isTerm) {
                    // Term: no box, just a dot + label rendered via the port.
                    inst.name  = "";
                    inst.y     = y;
                    inst.w     = 0.0f;
                    inst.h     = 0.0f;
                    inst.color = IM_COL32(0, 0, 0, 0);
                    inst.partialInterface = false;
                } else {
                    inst.name  = item.fullName.empty() ? item.label : item.fullName;
                    inst.y     = y;
                    inst.w     = instW;
                    inst.color = IM_COL32(100, 140, 200, 255);
                }

                const std::string pathKey =
                    item.isTerm ? std::string()
                                : (item.fullName.empty() ? item.label : item.fullName);

                // Determine if we have full expansion data for this instance.
                const std::vector<ExpandedPort>* expPorts = nullptr;
                if (!item.isTerm) {
                    auto expIt = g_expandedInstances.find(pathKey);
                    if (expIt != g_expandedInstances.end() && !expIt->second.empty())
                        expPorts = &expIt->second;
                }

                // Reserve an ID for the connected port up-front and advance the
                // counter immediately.  Without the pre-advance the first
                // non-connected expanded port would collide with connectedPortId.
                int connectedPortId = nextPortId++;

                if (expPorts) {
                    // -------------------------------------------------------
                    // Fully expanded: distribute all ports by side / direction.
                    // -------------------------------------------------------
                    inst.partialInterface = false;

                    // Count each side.
                    int nLeft = 0, nRight = 0;
                    for (const auto& ep : *expPorts) {
                        if (ep.direction == Direction::Input) ++nLeft; else ++nRight;
                    }
                    int maxSide = std::max(nLeft, nRight);
                    inst.h = std::max(instH,
                                      static_cast<float>(maxSide) * portSpacing + 10.0f);

                    int li = 0, ri = 0;
                    for (const auto& ep : *expPorts) {
                        bool isInput     = (ep.direction == Direction::Input);
                        bool isConnected = (ep.name == item.label);

                        Port p;
                        p.id        = isConnected ? connectedPortId : nextPortId++;
                        p.name      = ep.name;
                        p.lx        = isInput ? -0.5f : 0.5f;
                        p.ly        = isInput ? portLy(li++, nLeft) : portLy(ri++, nRight);
                        p.direction = ep.direction;
                        p.isInput   = !isInput; // driving role: output=driver=red
                        inst.ports.push_back(p);

                        // Each expanded port can load its own equipotential.
                        g_portEquiByPortId[p.id] = { item.pathIds, ep.childId, ep.bit };
                    }
                } else {
                    // -------------------------------------------------------
                    // Partial: only the single connected port is known.
                    // -------------------------------------------------------
                    if (!item.isTerm) {
                        inst.partialInterface = true;
                        inst.h = instH;
                    }

                    Port p;
                    p.id        = connectedPortId;
                    p.name      = item.label;
                    p.lx        = isDriving ? 0.5f : -0.5f;
                    p.ly        = 0.0f;
                    p.direction = item.direction;
                    p.isInput   = isDriving; // driving role (true = driver = red dot)
                    inst.ports.push_back(p);

                    g_portEquiByPortId[connectedPortId] = {
                        item.pathIds, item.termChildId, item.termBit
                    };
                }

                g_schematic.instances.push_back(inst);
                allPortRefs.push_back({ inst.id, connectedPortId });

                // Register occurrence info for instance-box expansion.
                if (!item.isTerm)
                    g_occInfoByShapeId[inst.id] = { pathKey, item.designRef };
            };

            // Layout: variable row heights, centred vertically.
            float dy = startY;
            for (size_t i = 0; i < drivers.size(); ++i) {
                addItem(drivers[i], leftColumnX, dy);
                dy += itemHeight(drivers[i]) + rowSpacing;
            }
            dy = startY;
            for (size_t i = 0; i < receivers.size(); ++i) {
                addItem(receivers[i], rightColumnX, dy);
                dy += itemHeight(receivers[i]) + rowSpacing;
            }
        }

        // Build net wires: hub (first) → every other port.
        if (allPortRefs.size() >= 2) {
            const PortRef hub = allPortRefs.front();
            for (size_t i = 1; i < allPortRefs.size(); ++i) {
                NetWire n;
                n.id          = static_cast<int>(g_schematic.nets.size()) + 1;
                n.srcInstance = hub.instanceId;
                n.srcPortId   = hub.portId;
                n.dstInstance = allPortRefs[i].instanceId;
                n.dstPortId   = allPortRefs[i].portId;
                n.color       = IM_COL32(200, 200, 100, 255);
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
    g_schematic.updateFitIfNeeded(canvasPos, innerSize, 60.0f);
    g_schematic.render(dl, canvasPos, innerSize);

    ImGui::EndChild(); // ##SchematicCanvas

    // Scrollbar sliders in the reserved strips around the canvas child.
    ImVec2 wMin(-500.0f, -500.0f), wMax(500.0f, 500.0f);
    const float pad = 120.0f;
    if (g_schematic.computeWorldBounds(wMin, wMax)) {
        wMin.x -= pad; wMin.y -= pad;
        wMax.x += pad; wMax.y += pad;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 24.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,         ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,       ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));

    ImGui::SetCursorPos(ImVec2(cursorStart.x + canvasSize.x, cursorStart.y));
    ImGui::VSliderFloat("##VScroll", ImVec2(scrollSz, canvasSize.y),
                        &g_schematic.transform.offset.y, wMax.y, wMin.y, "");

    ImGui::SetCursorPos(ImVec2(cursorStart.x, cursorStart.y + canvasSize.y));
    ImGui::SetNextItemWidth(canvasSize.x);
    ImGui::SliderFloat("##HScroll", &g_schematic.transform.offset.x, wMin.x, wMax.x, "");

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// Table render
// ---------------------------------------------------------------------------

void EquipotentialView::renderTable(Equipotential* equipotential) {
    if (ImGui::BeginTable("equipotential_table", 2,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("Element",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Direction", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();

        if (equipotential) {
            for (const auto& term : equipotential->terms) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", term.getString().c_str());
                ImGui::TableSetColumnIndex(1);
                ImColor c = getTopTermColor(term.direction);
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(c.Value.x, c.Value.y, c.Value.z, c.Value.w));
                ImGui::Text("%s", toString(term.direction));
                ImGui::PopStyleColor();
            }

            for (const auto& occ : equipotential->occurrences) {
                std::string label;
                for (const auto& inst : occ.path) { label += inst; label += '/'; }
                label += occ.term.getString();

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImColor c = getInstTermOccurrenceColor(occ.term.direction);
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(c.Value.x, c.Value.y, c.Value.z, c.Value.w));
                ImGui::Text("%s", toString(occ.term.direction));
                ImGui::PopStyleColor();
            }
        }

        ImGui::EndTable();
    }
}
