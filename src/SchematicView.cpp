#include "SchematicView.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace {
inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

// Draws a dashed rectangle in screen space (no corner rounding).
// dashPx / gapPx are in screen pixels so they stay legible at any zoom.
void addDashedRect(ImDrawList* dl, ImVec2 rmin, ImVec2 rmax,
                   ImU32 col, float thickness,
                   float dashPx = 6.0f, float gapPx = 4.0f) {
    auto seg = [&](float ax, float ay, float bx, float by) {
        float dx = bx - ax, dy = by - ay;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.001f) return;
        dx /= len; dy /= len;
        bool on = true;
        for (float t = 0.0f; t < len; ) {
            float step = on ? dashPx : gapPx;
            float t2 = std::min(t + step, len);
            if (on)
                dl->AddLine({ax + dx * t, ay + dy * t},
                            {ax + dx * t2, ay + dy * t2}, col, thickness);
            t = t2;
            on = !on;
        }
    };
    seg(rmin.x, rmin.y, rmax.x, rmin.y); // top
    seg(rmax.x, rmin.y, rmax.x, rmax.y); // right
    seg(rmax.x, rmax.y, rmin.x, rmax.y); // bottom
    seg(rmin.x, rmax.y, rmin.x, rmin.y); // left
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

// ---------------------------------------------------------------------------
// Shared port drawing (used by all gate renderers)
// ---------------------------------------------------------------------------
static void drawPorts(ImDrawList* dl, const InstanceShape& inst,
                      const SchematicView& sv,
                      const ImVec2& canvasPos, const ImVec2& canvasSize) {
    for (const auto& p : inst.ports) {
        ImVec2 worldP  = sv.portWorldPos(inst, p);
        ImVec2 screenP = sv.worldToScreen(worldP, canvasPos, canvasSize);
        bool   isLeft  = p.lx < 0.0f;

        ImU32 portColor = p.color != 0 ? p.color
            : (p.isInput ? IM_COL32(200, 80, 80, 255)
                         : IM_COL32(80, 200, 80, 255));

        float dotR = std::max(3.0f, 3.5f * sv.transform.scale);
        dl->AddCircleFilled(screenP, dotR, portColor);

        if (!p.name.empty()) {
            ImVec2 textSize = ImGui::CalcTextSize(p.name.c_str());
            ImVec2 lblPos   = isLeft
                ? ImVec2(screenP.x - 4.0f - textSize.x, screenP.y - textSize.y * 0.5f)
                : ImVec2(screenP.x + 4.0f,              screenP.y - textSize.y * 0.5f);
            dl->AddRectFilled(
                ImVec2(lblPos.x - 2.0f, lblPos.y - 1.0f),
                ImVec2(lblPos.x + textSize.x + 2.0f, lblPos.y + textSize.y + 1.0f),
                IM_COL32(30, 30, 30, 210));
            dl->AddText(lblPos, IM_COL32(220, 220, 220, 230), p.name.c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// Generic box renderer (fallback for unknown gate types)
// ---------------------------------------------------------------------------
static void drawGenericInstance(ImDrawList* dl, const InstanceShape& inst,
                                const SchematicView& sv,
                                const ImVec2& canvasPos, const ImVec2& canvasSize) {
    ImVec2 rmin, rmax;
    sv.worldRectToScreen(inst.x, inst.y, inst.w, inst.h, canvasPos, canvasSize, rmin, rmax);

    if (inst.w > 0.0f && inst.h > 0.0f && ((inst.color >> 24) & 0xFF) > 0) {
        dl->AddRectFilled(rmin, rmax, inst.color, 4.0f);

        if (inst.partialInterface)
            addDashedRect(dl, rmin, rmax, IM_COL32(210, 175, 55, 240), 2.0f);
        else
            dl->AddRect(rmin, rmax, IM_COL32(0, 0, 0, 200), 4.0f, 0, 2.0f);

        if (!inst.name.empty()) {
            ImVec2 textSize = ImGui::CalcTextSize(inst.name.c_str());
            ImVec2 textPos  = ImVec2((rmin.x + rmax.x) * 0.5f - textSize.x * 0.5f,
                                     rmin.y + 6.0f);
            dl->AddText(textPos, IM_COL32(255, 255, 255, 230), inst.name.c_str());
        }

        if (inst.partialInterface) {
            float boxH = rmax.y - rmin.y;
            if (boxH > 28.0f) {
                const float dotR    = 2.5f;
                const float spacing = 7.0f;
                const float dotY    = rmax.y - dotR - 5.0f;
                const float dotX    = (rmin.x + rmax.x) * 0.5f;
                const ImU32 dotCol  = IM_COL32(210, 175, 55, 220);
                dl->AddCircleFilled({dotX - spacing, dotY}, dotR, dotCol);
                dl->AddCircleFilled({dotX,           dotY}, dotR, dotCol);
                dl->AddCircleFilled({dotX + spacing, dotY}, dotR, dotCol);
            }
        }
    }

    drawPorts(dl, inst, sv, canvasPos, canvasSize);
}

// ---------------------------------------------------------------------------
// assign / buffer — triangle pointing right (input left, output apex right)
// ---------------------------------------------------------------------------
static void drawAssignInstance(ImDrawList* dl, const InstanceShape& inst,
                               const SchematicView& sv,
                               const ImVec2& canvasPos, const ImVec2& canvasSize) {
    ImVec2 tl = sv.worldToScreen(ImVec2(inst.x,          inst.y),          canvasPos, canvasSize);
    ImVec2 bl = sv.worldToScreen(ImVec2(inst.x,          inst.y + inst.h), canvasPos, canvasSize);
    ImVec2 mr = sv.worldToScreen(ImVec2(inst.x + inst.w, inst.y + inst.h * 0.5f), canvasPos, canvasSize);

    // Filled triangle body
    dl->AddTriangleFilled(tl, bl, mr, IM_COL32(80, 160, 220, 220));
    // Outline
    dl->AddTriangle(tl, bl, mr, IM_COL32(0, 0, 0, 200), 1.5f);

    // Label ("assign") near top-left of the bounding box, small and subtle
    ImVec2 lblPos = ImVec2(tl.x + 4.0f, tl.y + 4.0f);
    dl->AddText(lblPos, IM_COL32(255, 255, 255, 180), "=");

    drawPorts(dl, inst, sv, canvasPos, canvasSize);
}

// ---------------------------------------------------------------------------
// Gate dispatcher — add new model names here as the library grows
// ---------------------------------------------------------------------------
// To add a new gate type:
//   1. Write a static drawXxxInstance() function above with the same signature
//   2. Add an else-if branch below matching its modelName string
// ---------------------------------------------------------------------------
void SchematicView::drawInstance(ImDrawList* dl, const InstanceShape& inst,
                                 const ImVec2& canvasPos, const ImVec2& canvasSize) const {
    if (inst.modelName == "assign") {
        drawAssignInstance(dl, inst, *this, canvasPos, canvasSize);
    }
    // else if (inst.modelName == "and2")  { drawAnd2Instance (...); }
    // else if (inst.modelName == "or2")   { drawOr2Instance  (...); }
    // else if (inst.modelName == "dff")   { drawDffInstance  (...); }
    else {
        drawGenericInstance(dl, inst, *this, canvasPos, canvasSize);
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
    // Wire connects directly at the port world position (no triangle offset).
    float srcSide = (srcPort->lx >= 0.0f) ? 1.0f : -1.0f;
    float dstSide = (dstPort->lx >= 0.0f) ? 1.0f : -1.0f;

    const float stub = std::max(12.0f, 18.0f * transform.scale);

    // Depart/arrive with a short stub so the wire leaves the box orthogonally.
    ImVec2 p0 = srcScreen;
    ImVec2 p1 = ImVec2(srcScreen.x + stub * srcSide, srcScreen.y);
    ImVec2 p4 = ImVec2(dstScreen.x + stub * dstSide, dstScreen.y);
    float  midX = (p1.x + p4.x) * 0.5f;
    ImVec2 p2 = ImVec2(midX, p1.y);
    ImVec2 p3 = ImVec2(midX, p4.y);
    ImVec2 p5 = dstScreen;

    std::array<ImVec2, 6> points = {p0, p1, p2, p3, p4, p5};
    ImU32 col = net.color;
    float thickness = std::max(1.0f, 2.0f * transform.scale);

    // Draw segments individually to keep thickness consistent at joints.
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        dl->AddLine(points[i], points[i + 1], IM_COL32(0,0,0,80), thickness + 2.0f);
        dl->AddLine(points[i], points[i + 1], col, thickness);
    }
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
