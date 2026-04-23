#include "SchematicView.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace {
inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}
} // namespace

// Convert a point in world coordinates to screen coordinates using the renderer transform.
ImVec2 SchematicView::worldToScreen(const ImVec2& world, const ImVec2& canvasPos, const ImVec2& /*canvasSize*/) const {
    // world -> scaled screen offset
    float sx = (world.x - transform.offset.x + transform.screenOrigin.x / transform.scale) * transform.scale;
    float sy = (world.y - transform.offset.y + transform.screenOrigin.y / transform.scale) * transform.scale;
    return ImVec2(canvasPos.x + sx, canvasPos.y + sy);
}

// Convert a rectangle in world coordinates (x,y,w,h) to screen rect (min,max)
void SchematicView::worldRectToScreen(float x, float y, float w, float h,
                                      const ImVec2& canvasPos, const ImVec2& canvasSize,
                                      ImVec2& outMin, ImVec2& outMax) const {
    ImVec2 topLeft = worldToScreen(ImVec2(x, y), canvasPos, canvasSize);
    ImVec2 bottomRight = worldToScreen(ImVec2(x + w, y + h), canvasPos, canvasSize);
    outMin = ImVec2(std::min(topLeft.x, bottomRight.x), std::min(topLeft.y, bottomRight.y));
    outMax = ImVec2(std::max(topLeft.x, bottomRight.x), std::max(topLeft.y, bottomRight.y));
}

// Find instance by id (returns pointer or nullptr) -- const
InstanceShape* SchematicView::findInstanceById(int id) const {
    for (auto &inst : instances) {
        if (inst.id == id) return const_cast<InstanceShape*>(&inst);
    }
    return nullptr;
}

// Find port by id within an instance (returns pointer or nullptr) -- const
Port* SchematicView::findPortById(InstanceShape& inst, int portId) const {
    for (auto &p : inst.ports) {
        if (p.id == portId) return const_cast<Port*>(&p);
    }
    return nullptr;
}

// Compute absolute world position of a port given its instance and normalized local coords (lx,ly).
ImVec2 SchematicView::portWorldPos(const InstanceShape& inst, const Port& port) const {
    // port.lx, port.ly are normalized: -0.5..0.5 horizontally/vertically where 0 is center
    float px = inst.x + (inst.w * (0.5f + port.lx)); // convert -0.5..0.5 to 0..1 then * width
    float py = inst.y + (inst.h * (0.5f + port.ly));
    return ImVec2(px, py);
}

// Draw a single instance (rectangle, name, ports)
void SchematicView::drawInstance(ImDrawList* dl, const InstanceShape& inst, const ImVec2& canvasPos, const ImVec2& canvasSize) const {
    ImVec2 rmin, rmax;
    worldRectToScreen(inst.x, inst.y, inst.w, inst.h, canvasPos, canvasSize, rmin, rmax);

    const bool hasSize = inst.w > 0.0f && inst.h > 0.0f;
    const bool hasFill = ((inst.color >> 24) & 0xFF) > 0;
    if (hasSize && hasFill) {
        // Background
        dl->AddRectFilled(rmin, rmax, inst.color, 4.0f);

        // Border
        dl->AddRect(rmin, rmax, IM_COL32(0,0,0,200), 4.0f, 0, 2.0f);

        // Name (centered)
        if (!inst.name.empty()) {
            ImVec2 textSize = ImGui::CalcTextSize(inst.name.c_str());
            ImVec2 textPos = ImVec2((rmin.x + rmax.x) * 0.5f - textSize.x * 0.5f, rmin.y + 6.0f);
            dl->AddText(textPos, IM_COL32(255,255,255,230), inst.name.c_str());
        }
    }

    // Draw ports (triangles)
    for (const auto& p : inst.ports) {
        ImVec2 worldP = portWorldPos(inst, p);
        ImVec2 screenP = worldToScreen(worldP, canvasPos, canvasSize);
        float radius = std::max(4.0f, 6.0f * transform.scale);
        bool isLeft = p.lx < 0.0f;
        bool pointRight = true;

        // Port color selection:
        // 1) If port.color != 0 use it (explicit override).
        // 2) Otherwise use p.isInput: true -> red, false -> green.
        ImU32 portColor = p.color != 0 ? p.color : (p.isInput ? IM_COL32(200, 80, 80, 255) : IM_COL32(80, 200, 80, 255));
        ImVec2 triCenter = screenP;
        if (p.direction == Direction::Input) {
            float inset = radius * 0.6f;
            triCenter.x += (isLeft ? inset : -inset);
        }
        if (pointRight) {
            ImVec2 tip = ImVec2(triCenter.x + radius, triCenter.y);
            ImVec2 b1 = ImVec2(triCenter.x - radius, triCenter.y - radius);
            ImVec2 b2 = ImVec2(triCenter.x - radius, triCenter.y + radius);
            dl->AddTriangleFilled(tip, b1, b2, portColor);
            dl->AddTriangle(tip, b1, b2, IM_COL32(0, 0, 0, 200), 1.0f);
        } else {
            ImVec2 tip = ImVec2(triCenter.x - radius, triCenter.y);
            ImVec2 b1 = ImVec2(triCenter.x + radius, triCenter.y - radius);
            ImVec2 b2 = ImVec2(triCenter.x + radius, triCenter.y + radius);
            dl->AddTriangleFilled(tip, b1, b2, portColor);
            dl->AddTriangle(tip, b1, b2, IM_COL32(0, 0, 0, 200), 1.0f);
        }

        // port label (small)
        if (!p.name.empty()) {
            ImVec2 textSize = ImGui::CalcTextSize(p.name.c_str());
            ImVec2 lblPos = isLeft
                ? ImVec2(screenP.x - radius - 4.0f - textSize.x, screenP.y - 6.0f)
                : ImVec2(screenP.x + radius + 4.0f, screenP.y - 6.0f);
            dl->AddText(lblPos, IM_COL32(220,220,220,220), p.name.c_str());
        }
    }
}

void SchematicView::drawNet(ImDrawList* dl, const NetWire& net, const ImVec2& canvasPos, const ImVec2& canvasSize) const {
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
    float portRadius = std::max(4.0f, 6.0f * transform.scale);
    // All port triangles point right: connect to triangle tip.
    ImVec2 srcTip = ImVec2(srcScreen.x + portRadius, srcScreen.y);
    ImVec2 dstTip = ImVec2(dstScreen.x + portRadius, dstScreen.y);

    const float stub = std::max(12.0f, 18.0f * transform.scale);
    float srcDir = (srcPort->lx < 0.0f) ? -1.0f : 1.0f;
    float dstDir = (dstPort->lx < 0.0f) ? -1.0f : 1.0f;

    ImVec2 p0 = srcTip;
    ImVec2 p1 = ImVec2(srcScreen.x + stub * srcDir, srcScreen.y);
    ImVec2 p4 = ImVec2(dstScreen.x - stub * dstDir, dstScreen.y);
    float midX = (p1.x + p4.x) * 0.5f;
    ImVec2 p2 = ImVec2(midX, p1.y);
    ImVec2 p3 = ImVec2(midX, p4.y);
    ImVec2 p5 = dstTip;

    std::array<ImVec2, 6> points = {p0, p1, p2, p3, p4, p5};
    ImU32 col = net.color;
    float thickness = std::max(1.0f, 2.0f * transform.scale);

    // Draw segments individually to keep thickness consistent at joints.
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        dl->AddLine(points[i], points[i + 1], IM_COL32(0,0,0,80), thickness + 2.0f);
        dl->AddLine(points[i], points[i + 1], col, thickness);
    }

    // Endpoints highlight
    dl->AddCircleFilled(srcTip, 3.0f + transform.scale, IM_COL32(255,255,255,200));
    dl->AddCircleFilled(dstTip, 3.0f + transform.scale, IM_COL32(255,255,255,200));
}

// Main render entry
bool SchematicView::computeWorldBounds(ImVec2& outMin, ImVec2& outMax) const {
    if (instances.empty()) return false;
    float minX = instances[0].x;
    float minY = instances[0].y;
    float maxX = instances[0].x + instances[0].w;
    float maxY = instances[0].y + instances[0].h;
    for (const auto& inst : instances) {
        minX = std::min(minX, inst.x);
        minY = std::min(minY, inst.y);
        maxX = std::max(maxX, inst.x + inst.w);
        maxY = std::max(maxY, inst.y + inst.h);
    }
    outMin = ImVec2(minX, minY);
    outMax = ImVec2(maxX, maxY);
    return true;
}

void SchematicView::fitToContents(const ImVec2& /*canvasPos*/, const ImVec2& canvasSize, float padding) {
    ImVec2 boundsMin, boundsMax;
    if (!computeWorldBounds(boundsMin, boundsMax)) return;

    float width = std::max(1.0f, boundsMax.x - boundsMin.x);
    float height = std::max(1.0f, boundsMax.y - boundsMin.y);

    float scaleX = (canvasSize.x - 2.0f * padding) / width;
    float scaleY = (canvasSize.y - 2.0f * padding) / height;
    float targetScale = std::min(scaleX, scaleY);
    targetScale = clampf(targetScale, minScale, maxScale);
    transform.scale = targetScale;

    float usedWidth = width * transform.scale;
    float usedHeight = height * transform.scale;
    float padX = std::max(padding, (canvasSize.x - usedWidth) * 0.5f);
    float padY = std::max(padding, (canvasSize.y - usedHeight) * 0.5f);

    // With screenOrigin at (0,0), offset is the world-space top-left of the screen.
    transform.offset.x = boundsMin.x - padX / transform.scale;
    transform.offset.y = boundsMin.y - padY / transform.scale;
}

void SchematicView::zoomBy(float factor) {
    transform.scale = clampf(transform.scale * factor, minScale, maxScale);
}

void SchematicView::requestFit(bool resetInteraction) {
    needsFit_ = true;
    if (resetInteraction) {
        hasUserInteraction_ = false;
    }
}

void SchematicView::updateFitIfNeeded(const ImVec2& canvasPos, const ImVec2& canvasSize, float padding) {
    if (!needsFit_ || hasUserInteraction_) return;
    fitToContents(canvasPos, canvasSize, padding);
    needsFit_ = false;
}

void SchematicView::handleInteraction(const ImVec2& canvasPos, const ImVec2& /*canvasSize*/) {
    ImGuiIO& io = ImGui::GetIO();
    const bool allowKeyboard = ImGui::IsItemHovered() || ImGui::IsItemActive();

    // Middle mouse drag to pan (also allow right mouse drag)
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {
        ImVec2 delta = io.MouseDelta;
        transform.offset.x -= delta.x / transform.scale;
        transform.offset.y -= delta.y / transform.scale;
        hasUserInteraction_ = true;
    }

    // Zoom with wheel when hovered
    if (ImGui::IsItemHovered()) {
        float wheel = io.MouseWheel;
        if (wheel != 0.0f) {
            hasUserInteraction_ = true;
            float oldScale = transform.scale;
            float zoomFactor = (wheel > 0.0f) ? 1.1f : 0.9f;
            transform.scale = clampf(transform.scale * zoomFactor, minScale, maxScale);

            // Zoom to mouse position (keeps mouse world point stable)
            ImVec2 mousePos = io.MousePos;
            ImVec2 mouseWorldBefore = ImVec2(
                (mousePos.x - canvasPos.x) / oldScale + transform.offset.x - transform.screenOrigin.x / oldScale,
                (mousePos.y - canvasPos.y) / oldScale + transform.offset.y - transform.screenOrigin.y / oldScale
            );
            ImVec2 mouseWorldAfter = ImVec2(
                (mousePos.x - canvasPos.x) / transform.scale + transform.offset.x - transform.screenOrigin.x / transform.scale,
                (mousePos.y - canvasPos.y) / transform.scale + transform.offset.y - transform.screenOrigin.y / transform.scale
            );
            transform.offset.x += (mouseWorldBefore.x - mouseWorldAfter.x);
            transform.offset.y += (mouseWorldBefore.y - mouseWorldAfter.y);
        }
    }

    if (allowKeyboard) {
        float panSpeed = 420.0f * io.DeltaTime / std::max(0.001f, transform.scale);
        if (ImGui::IsKeyDown(ImGuiKey_A)) {
            transform.offset.x -= panSpeed;
            hasUserInteraction_ = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_D)) {
            transform.offset.x += panSpeed;
            hasUserInteraction_ = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_W)) {
            transform.offset.y -= panSpeed;
            hasUserInteraction_ = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_S)) {
            transform.offset.y += panSpeed;
            hasUserInteraction_ = true;
        }

        if (ImGui::IsKeyDown(ImGuiKey_Equal) || ImGui::IsKeyDown(ImGuiKey_KeypadAdd)) {
            float oldScale = transform.scale;
            transform.scale = clampf(transform.scale * 1.02f, minScale, maxScale);
            float deltaScale = transform.scale - oldScale;
            if (deltaScale != 0.0f) hasUserInteraction_ = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_Minus) || ImGui::IsKeyDown(ImGuiKey_KeypadSubtract)) {
            float oldScale = transform.scale;
            transform.scale = clampf(transform.scale * 0.98f, minScale, maxScale);
            float deltaScale = transform.scale - oldScale;
            if (deltaScale != 0.0f) hasUserInteraction_ = true;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_0) || ImGui::IsKeyPressed(ImGuiKey_Home)) {
            requestFit(true);
        }
    }
}

void SchematicView::render(ImDrawList* dl, const ImVec2& canvasPos, const ImVec2& canvasSize) {
    dl->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

    // Draw background grid
    if (showGrid) {
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
    }

    // Draw nets first (so instances render on top)
    for (const auto& net : nets) {
        drawNet(dl, net, canvasPos, canvasSize);
    }

    // Draw instances
    for (const auto& inst : instances) {
        drawInstance(dl, inst, canvasPos, canvasSize);
    }

    dl->PopClipRect();
}
