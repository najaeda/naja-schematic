// EquipotentialView.cpp
#include "EquipotentialView.h"

#include <algorithm>
#include <imgui.h>

#include "Types.h"
#include "SchematicView.h"

static SchematicView g_schematic;
static int g_pendingZoomSteps = 0;
static bool g_pendingFit = false;

void EquipotentialView::zoomIn() {
    g_pendingZoomSteps += 1;
}

void EquipotentialView::zoomOut() {
    g_pendingZoomSteps -= 1;
}

void EquipotentialView::fitView() {
    g_pendingFit = true;
}

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

void EquipotentialView::renderSchematic(Equipotential* equipotential) {
    const float scrollSz = ImGui::GetFrameHeight();
    ImVec2 availSize = ImGui::GetContentRegionAvail();
    ImVec2 canvasSize = ImVec2(std::max(10.0f, availSize.x - scrollSz),
                               std::max(10.0f, availSize.y - scrollSz));

    // cursorStart is in SchematicPanel's coordinate space.
    // We need it for placing the slider strips after the canvas child ends.
    ImVec2 cursorStart = ImGui::GetCursorPos();

    // ---------------------------------------------------------------
    // Canvas in its own child window.
    // ImGui manages this child's clip rect independently of the shared
    // parent draw list, so zoomed/panned draw-list operations can never
    // bleed into the table panel below.
    // ---------------------------------------------------------------
    ImGui::BeginChild("##SchematicCanvas", canvasSize, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Use the content size reported from *inside* the child (handles any
    // implicit padding differences between ImGui versions).
    ImVec2 innerSize = ImGui::GetContentRegionAvail();

    ImGui::InvisibleButton("net_canvas", innerSize,
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetItemRectMin();

    g_schematic.handleInteraction(canvasPos, innerSize);
    if (g_pendingZoomSteps != 0) {
        int steps = g_pendingZoomSteps;
        g_pendingZoomSteps = 0;
        float factor = (steps > 0) ? 1.1f : 0.9f;
        for (int i = 0; i < std::abs(steps); ++i) {
            g_schematic.zoomBy(factor);
        }
    }
    if (g_pendingFit) {
        g_pendingFit = false;
        g_schematic.requestFit(true);
    }

    g_schematic.instances.clear();
    g_schematic.nets.clear();

    if (!equipotential) {
        dl->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 8.0f), IM_COL32(255, 100, 100, 255), "");
    } else {
        std::string dbg = "terms: " + std::to_string(equipotential->terms.size()) + " occ: " + std::to_string(equipotential->occurrences.size());
        dl->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 8.0f), IM_COL32(200, 200, 200, 255), dbg.c_str());
    }

    if (equipotential) {
        const float leftMargin = 20.0f;
        const float columnGap = 120.0f;
        const float rowSpacing = 24.0f;
        const float instW = 180.0f;
        const float instH = 70.0f;

        int nextInstanceId = 1;
        int nextPortId = 1;

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
        if (totalItems < 64) {
            const float columnWidth = instW;
            const float rightColumnX = leftMargin + columnWidth + columnGap;
            const float leftColumnX = leftMargin;
            const float maxRows = static_cast<float>(std::max(drivers.size(), receivers.size()));
            const float totalHeight = (maxRows * instH) + ((maxRows > 0 ? (maxRows - 1) : 0) * rowSpacing);
            const float startY = (innerSize.y - totalHeight) * 0.5f;

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

        if (allPortRefs.size() >= 2) {
            const PortRef hub = allPortRefs.front();
            for (size_t i = 1; i < allPortRefs.size(); ++i) {
                const PortRef& other = allPortRefs[i];
                NetWire n;
                n.id = static_cast<int>(g_schematic.nets.size()) + 1;
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
    g_schematic.updateFitIfNeeded(canvasPos, innerSize, 60.0f);
    g_schematic.render(dl, canvasPos, innerSize);

    ImGui::EndChild(); // ##SchematicCanvas
    // ---------------------------------------------------------------
    // Back in SchematicPanel. Place the scrollbar sliders in the strips
    // that were reserved around the canvas child.
    // ---------------------------------------------------------------

    ImVec2 wMin(-500.0f, -500.0f), wMax(500.0f, 500.0f);
    const float pad = 120.0f;
    if (g_schematic.computeWorldBounds(wMin, wMax)) {
        wMin.x -= pad; wMin.y -= pad;
        wMax.x += pad; wMax.y += pad;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 24.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,          ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,        ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,  ImVec4(0.65f, 0.65f, 0.65f, 1.0f));

    // Vertical slider — right strip
    ImGui::SetCursorPos(ImVec2(cursorStart.x + canvasSize.x, cursorStart.y));
    ImGui::VSliderFloat("##VScroll", ImVec2(scrollSz, canvasSize.y),
                        &g_schematic.transform.offset.y, wMax.y, wMin.y, "");

    // Horizontal slider — bottom strip
    ImGui::SetCursorPos(ImVec2(cursorStart.x, cursorStart.y + canvasSize.y));
    ImGui::SetNextItemWidth(canvasSize.x);
    ImGui::SliderFloat("##HScroll", &g_schematic.transform.offset.x, wMin.x, wMax.x, "");

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

void EquipotentialView::renderTable(Equipotential* equipotential) {
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
