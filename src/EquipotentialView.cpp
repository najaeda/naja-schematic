// EquipotentialView.cpp
#include "EquipotentialView.h"

#include <imgui.h>

#include "Types.h"
#include "NetRenderer.h"

static NetRenderer g_renderer;

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

    ImGui::InvisibleButton("net_canvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetItemRectMin();

    // Initialize a default transform if first frame
    static bool transformInitialized = false;
    if (!transformInitialized) {
        g_renderer.transform.scale = 1.0f;
        g_renderer.transform.offset = ImVec2(0, 0);
        g_renderer.transform.screenOrigin = ImVec2(0, 0);
        transformInitialized = true;
    }

    // Middle mouse drag to pan (also allow right mouse drag)
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        g_renderer.transform.offset.x -= delta.x / g_renderer.transform.scale;
        g_renderer.transform.offset.y -= delta.y / g_renderer.transform.scale;
    }

    // Zoom with wheel when hovered
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float oldScale = g_renderer.transform.scale;
            float zoomFactor = (wheel > 0.0f) ? 1.1f : 0.9f;
            g_renderer.transform.scale = std::max(0.1f, g_renderer.transform.scale * zoomFactor);

            // Zoom to mouse position (keeps mouse world point stable)
            ImVec2 mousePos = ImGui::GetIO().MousePos;
            ImVec2 mouseWorldBefore = ImVec2(
                (mousePos.x - canvasPos.x) / oldScale + g_renderer.transform.offset.x - g_renderer.transform.screenOrigin.x / oldScale,
                (mousePos.y - canvasPos.y) / oldScale + g_renderer.transform.offset.y - g_renderer.transform.screenOrigin.y / oldScale
            );
            ImVec2 mouseWorldAfter = ImVec2(
                (mousePos.x - canvasPos.x) / g_renderer.transform.scale + g_renderer.transform.offset.x - g_renderer.transform.screenOrigin.x / g_renderer.transform.scale,
                (mousePos.y - canvasPos.y) / g_renderer.transform.scale + g_renderer.transform.offset.y - g_renderer.transform.screenOrigin.y / g_renderer.transform.scale
            );
            g_renderer.transform.offset.x += (mouseWorldBefore.x - mouseWorldAfter.x);
            g_renderer.transform.offset.y += (mouseWorldBefore.y - mouseWorldAfter.y);
        }
    }

    // --- Populate renderer instances/nets from equipotential terms + occurrences ---
    // Rebuild renderer contents each frame from the current equipotential.
    // For each term and each occurrence we create one InstanceShape (square) and one Port associated with it.
    g_renderer.instances.clear();
    g_renderer.nets.clear();

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
        const float spacing = 40.0f;
        const float instW = 140.0f;
        const float instH = 70.0f;

        float xOffset = leftMargin;
        float canvasCenterY = canvasSize.y * 0.5f;

        int nextInstanceId = 1;
        int nextPortId = 1;

        // Keep a list of (instanceId, portId) for all created ports so we can connect them to the same net
        struct PortRef { int instanceId; int portId; };
        std::vector<PortRef> allPortRefs;
        allPortRefs.reserve(equipotential->terms.size() + equipotential->occurrences.size());

        // First: create one instance + one port per top-level term (equipotential->terms)
        // Terms are placed in a row above the center
        xOffset = leftMargin;
        for (size_t t = 0; t < equipotential->terms.size(); ++t) {
            const auto& bitTerm = equipotential->terms[t];

            // Instance name derived from term name (fallback to generated)
            std::string instName = bitTerm.name.empty() ? ("TERM" + std::to_string(nextInstanceId)) : bitTerm.name;

            InstanceShape inst;
            inst.id = nextInstanceId++;
            inst.name =  bitTerm.getString();
            inst.x = xOffset;
            inst.y = canvasCenterY - instH * 0.5f - 120.0f; // place terms row above occurrences for clarity
            inst.w = 50.0f;
            inst.h = 50.0f;
            inst.color = IM_COL32(180, 120, 200, 255);

            // Create a single port for this term
            Port p;
            p.id = nextPortId++;
            p.name = std::string("");
            if (bitTerm.direction == Direction::Output) p.lx = -0.5f;
            else if (bitTerm.direction == Direction::Input) p.lx = 0.5f;
            else p.lx = 0.0f;
            p.ly = 0.0f;

            // Term port color rule: if NOT Output -> red, otherwise green.
            // NetRenderer draws red when p.isInput == true, green when false.
            // So set p.isInput = (bitTerm.direction != Direction::Output)
            p.isInput = (bitTerm.direction != Direction::Output);

            inst.ports.push_back(p);
            g_renderer.instances.push_back(inst);

            // record port ref
            allPortRefs.push_back({ inst.id, p.id });

            xOffset += instW + spacing;
        }

        // Second: create one instance + one port per occurrence (equipotential->occurrences)
        xOffset = leftMargin;
        for (size_t idx = 0; idx < equipotential->occurrences.size(); ++idx) {
            const auto& occ = equipotential->occurrences[idx];
            const auto& bitTerm = occ.term;

            // Instance name: last element of path if available, otherwise a generated name
            std::string instName = "INST" + std::to_string(nextInstanceId);
            if (!occ.path.empty()) instName = occ.path.back();

            InstanceShape inst;
            inst.id = nextInstanceId++;
            std::string concatedPaths;
            for (auto name : occ.path) {
                concatedPaths += "/" + name;
            }
            inst.name = concatedPaths;
            inst.x = xOffset;
            inst.y = canvasCenterY - instH * 0.5f + 40.0f; // place occurrences row below center
            inst.w = instW;
            inst.h = instH;
            if ((idx & 1) == 0) inst.color = IM_COL32(100, 140, 200, 255);
            else inst.color = IM_COL32(120, 200, 140, 255);

            // Create a single port for this occurrence
            Port p;
            p.id = nextPortId++;
            p.name = bitTerm.getString();
            if (bitTerm.direction == Direction::Input) p.lx = -0.5f;
            else if (bitTerm.direction == Direction::Output) p.lx = 0.5f;
            else p.lx = 0.0f;
            p.ly = 0.0f;

            // Occurrence port color rule (user requested earlier):
            // "for occurrence if not input so green, otherwise red."
            // NetRenderer draws red when p.isInput == true, green when false.
            // To make INPUT occurrences appear green (as you reported you want),
            // set p.isInput = (bitTerm.direction != Direction::Input)
            // - if direction == Input -> p.isInput = false -> green
            // - if direction != Input -> p.isInput = true  -> red
            p.isInput = (bitTerm.direction != Direction::Input);

            inst.ports.push_back(p);
            g_renderer.instances.push_back(inst);

            // record port ref
            allPortRefs.push_back({ inst.id, p.id });

            xOffset += instW + spacing;
        }

        // --- Connect all ports to the same net (star topology) ---
        // If there are at least two ports, choose the first port as the hub and create nets from hub to every other port.
        if (allPortRefs.size() >= 2) {
            const PortRef hub = allPortRefs.front();
            for (size_t i = 1; i < allPortRefs.size(); ++i) {
                const PortRef& other = allPortRefs[i];
                NetWire n;
                n.id = static_cast<int>(g_renderer.nets.size()) + 1;
                // connect hub -> other (direction is visual only)
                n.srcInstance = hub.instanceId;
                n.srcPortId = hub.portId;
                n.dstInstance = other.instanceId;
                n.dstPortId = other.portId;
                n.color = IM_COL32(200, 200, 100, 255);
                g_renderer.nets.push_back(n);
            }
        }
    } // end if equipotential

    // Render the netlist canvas (instances + ports + nets)
    g_renderer.render(dl, canvasPos, canvasSize);

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
