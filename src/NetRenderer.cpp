#include "NetRenderer.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>

// Convert a point in world coordinates to screen coordinates using the renderer transform.
ImVec2 NetRenderer::worldToScreen(const ImVec2& world, const ImVec2& canvasPos, const ImVec2& /*canvasSize*/) const {
    // world -> scaled screen offset
    float sx = (world.x - transform.offset.x + transform.screenOrigin.x / transform.scale) * transform.scale;
    float sy = (world.y - transform.offset.y + transform.screenOrigin.y / transform.scale) * transform.scale;
    return ImVec2(canvasPos.x + sx, canvasPos.y + sy);
}

// Convert a rectangle in world coordinates (x,y,w,h) to screen rect (min,max)
void NetRenderer::worldRectToScreen(float x, float y, float w, float h,
                                    const ImVec2& canvasPos, const ImVec2& canvasSize,
                                    ImVec2& outMin, ImVec2& outMax) const {
    ImVec2 topLeft = worldToScreen(ImVec2(x, y), canvasPos, canvasSize);
    ImVec2 bottomRight = worldToScreen(ImVec2(x + w, y + h), canvasPos, canvasSize);
    outMin = ImVec2(std::min(topLeft.x, bottomRight.x), std::min(topLeft.y, bottomRight.y));
    outMax = ImVec2(std::max(topLeft.x, bottomRight.x), std::max(topLeft.y, bottomRight.y));
}

// Find instance by id (returns pointer or nullptr) -- const
InstanceShape* NetRenderer::findInstanceById(int id) const {
    for (auto &inst : instances) {
        if (inst.id == id) return const_cast<InstanceShape*>(&inst);
    }
    return nullptr;
}

// Find port by id within an instance (returns pointer or nullptr) -- const
Port* NetRenderer::findPortById(InstanceShape& inst, int portId) const {
    for (auto &p : inst.ports) {
        if (p.id == portId) return const_cast<Port*>(&p);
    }
    return nullptr;
}

// Compute absolute world position of a port given its instance and normalized local coords (lx,ly).
ImVec2 NetRenderer::portWorldPos(const InstanceShape& inst, const Port& port) const {
    // port.lx, port.ly are normalized: -0.5..0.5 horizontally/vertically where 0 is center
    float px = inst.x + (inst.w * (0.5f + port.lx)); // convert -0.5..0.5 to 0..1 then * width
    float py = inst.y + (inst.h * (0.5f + port.ly));
    return ImVec2(px, py);
}

// Draw a single instance (rectangle, name, ports)
void NetRenderer::drawInstance(ImDrawList* dl, const InstanceShape& inst, const ImVec2& canvasPos, const ImVec2& canvasSize) const {
    ImVec2 rmin, rmax;
    worldRectToScreen(inst.x, inst.y, inst.w, inst.h, canvasPos, canvasSize, rmin, rmax);

    // Background
    dl->AddRectFilled(rmin, rmax, inst.color, 4.0f);

    // Border
    dl->AddRect(rmin, rmax, IM_COL32(0,0,0,200), 4.0f, 0, 2.0f);

    // Name (centered)
    ImVec2 textSize = ImGui::CalcTextSize(inst.name.c_str());
    ImVec2 textPos = ImVec2((rmin.x + rmax.x) * 0.5f - textSize.x * 0.5f, rmin.y + 6.0f);
    dl->AddText(textPos, IM_COL32(255,255,255,230), inst.name.c_str());

    // Draw ports
    for (const auto& p : inst.ports) {
        ImVec2 worldP = portWorldPos(inst, p);
        ImVec2 screenP = worldToScreen(worldP, canvasPos, canvasSize);
        float radius = std::max(3.0f, 6.0f * transform.scale);

        // Port color selection:
        // 1) If port.color != 0 use it (explicit override).
        // 2) Otherwise use p.isInput: true -> red, false -> green.
        ImU32 portColor = p.color != 0 ? p.color : (p.isInput ? IM_COL32(200, 80, 80, 255) : IM_COL32(80, 200, 80, 255));
        dl->AddCircleFilled(screenP, radius, portColor);

        // port label (small)
        ImVec2 lblPos = ImVec2(screenP.x + radius + 4.0f, screenP.y - 6.0f);
        dl->AddText(lblPos, IM_COL32(220,220,220,220), p.name.c_str());
    }
}

// Draw a net (simple bezier between ports)
void NetRenderer::drawNet(ImDrawList* dl, const NetWire& net, const ImVec2& canvasPos, const ImVec2& canvasSize) const {
    InstanceShape* srcInst = findInstanceById(net.srcInstance);
    InstanceShape* dstInst = findInstanceById(net.dstInstance);
    if (!srcInst || !dstInst) return;

    Port* srcPort = findPortById(*srcInst, net.srcPortId);
    Port* dstPort = findPortById(*dstInst, net.dstPortId);
    if (!srcPort || !dstPort) return;

    ImVec2 srcWorld = portWorldPos(*srcInst, *srcPort);
    ImVec2 dstWorld = portWorldPos(*dstInst, *dstPort);
    ImVec2 srcScreen = worldToScreen(srcWorld, canvasPos, canvasSize);
    ImVec2 dstScreen = worldToScreen(dstWorld, canvasPos, canvasSize);

    // Control points for a smooth curve
    float dx = dstScreen.x - srcScreen.x;
    float dy = dstScreen.y - srcScreen.y;
    float dist = std::sqrt(dx*dx + dy*dy);
    float ctrl = std::min(200.0f, dist * 0.5f);

    ImVec2 c1 = ImVec2(srcScreen.x + ctrl, srcScreen.y);
    ImVec2 c2 = ImVec2(dstScreen.x - ctrl, dstScreen.y);

    ImU32 col = net.color;
    float thickness = std::max(1.0f, 2.0f * transform.scale);

    // Draw shadow / glow
    dl->AddBezierCubic(srcScreen, c1, c2, dstScreen, IM_COL32(0,0,0,80), thickness + 2.0f);
    // Draw main curve
    dl->AddBezierCubic(srcScreen, c1, c2, dstScreen, col, thickness);

    // Draw endpoints highlight
    dl->AddCircleFilled(srcScreen, 3.0f + transform.scale, IM_COL32(255,255,255,200));
    dl->AddCircleFilled(dstScreen, 3.0f + transform.scale, IM_COL32(255,255,255,200));
}

// Main render entry
void NetRenderer::render(ImDrawList* dl, const ImVec2& canvasPos, const ImVec2& canvasSize) {
    // Draw background grid
    const ImU32 gridCol = IM_COL32(60, 60, 60, 80);
    const float gridSpacingWorld = 50.0f; // world units between grid lines
    // Convert spacing to screen pixels
    float spacingPx = gridSpacingWorld * transform.scale;
    if (spacingPx >= 6.0f) {
        // find top-left world coordinate of canvas
        ImVec2 topLeftWorld = ImVec2(transform.offset.x - transform.screenOrigin.x / transform.scale,
                                     transform.offset.y - transform.screenOrigin.y / transform.scale);
        // compute first grid line in screen coords
        float startX = canvasPos.x - fmodf((topLeftWorld.x * transform.scale), spacingPx);
        float startY = canvasPos.y - fmodf((topLeftWorld.y * transform.scale), spacingPx);
        for (float x = startX; x < canvasPos.x + canvasSize.x; x += spacingPx) {
            dl->AddLine(ImVec2(x, canvasPos.y), ImVec2(x, canvasPos.y + canvasSize.y), gridCol, 1.0f);
        }
        for (float y = startY; y < canvasPos.y + canvasSize.y; y += spacingPx) {
            dl->AddLine(ImVec2(canvasPos.x, y), ImVec2(canvasPos.x + canvasSize.x, y), gridCol, 1.0f);
        }
    }

    // Draw nets first (so instances render on top)
    for (const auto& net : nets) {
        drawNet(dl, net, canvasPos, canvasSize);
    }

    // Draw instances
    for (const auto& inst : instances) {
        drawInstance(dl, inst, canvasPos, canvasSize);
    }
}
