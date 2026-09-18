#include "SchematicView.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace {
inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

// ---------------------------------------------------------------------------
// Label legibility helpers
//
// Names are drawn in world space (their pixel size follows zoom, like the
// boxes/ports they annotate) rather than at a fixed screen size. That keeps
// text proportional to the shrinking/growing box instead of overflowing it
// or fighting neighboring labels at any given zoom level. Below a minimum
// screen size a label is hidden outright (an unreadable smear is worse than
// no label); above a cap it stops growing so heavy zoom-in doesn't blow up
// glyphs into blurry blocks.
//
// Pin dots (which is what a top-level term renders as -- see
// EquipotentialView's zero-size term stubs) and wires follow the same
// shrink-then-vanish policy instead of being held at an artificial minimum
// pixel size forever: at extreme zoom-out they fade out of existence just
// like their labels already do, rather than persisting as a fixed-size
// clutter of dots/lines under illegible text.
// ---------------------------------------------------------------------------
constexpr float kInstanceLabelBaseSize = 13.0f; // world-space "1x zoom" size
constexpr float kPortLabelBaseSize     = 11.0f;
constexpr float kMinLabelFontSize      = 7.0f;
constexpr float kMaxLabelFontSize      = 30.0f;

constexpr float kPortDotBaseSize  = 3.5f; // world-space "1x zoom" pin dot radius
constexpr float kMinPortDotSize   = 1.5f;
constexpr float kWireBaseThickness = 2.0f; // world-space "1x zoom" wire thickness
constexpr float kMinWireThickness  = 0.75f;

// Returns 0.0f when the label would render too small to read -- callers
// should skip drawing (and any backing rect) in that case.
float labelFontSize(float baseSize, float scale) {
    float sz = baseSize * scale;
    if (sz < kMinLabelFontSize) return 0.0f;
    return std::min(sz, kMaxLabelFontSize);
}

// Same shrink-then-vanish policy as labelFontSize(), for elements (pin dots,
// wires) with no upper cap on how large they should grow when zoomed in.
float zoomedSizeOrHidden(float baseSize, float scale, float minPx) {
    float sz = baseSize * scale;
    return sz < minPx ? 0.0f : sz;
}

// Picks black or white text (by relative luminance) so a label stays legible
// against an arbitrary fill color, including diagnosis-tinted boxes.
ImU32 contrastingTextColor(ImU32 bg, unsigned char alpha = 235) {
    float r = ((bg >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
    float g = ((bg >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
    float b = ((bg >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
    float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    return luminance > 0.55f ? IM_COL32(20, 20, 20, alpha) : IM_COL32(245, 245, 245, alpha);
}

// Truncates `text` to fit within maxWidth pixels at fontSize, appending "..."
// when it doesn't fit whole ("" if there isn't even room for the ellipsis).
std::string truncateToWidth(ImFont* font, float fontSize, const std::string& text, float maxWidth) {
    if (maxWidth <= 0.0f) return "";
    if (font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str()).x <= maxWidth) return text;
    const char* kEllipsis = "...";
    float ellipsisWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, kEllipsis).x;
    if (ellipsisWidth > maxWidth) return "";
    size_t lo = 0, hi = text.size();
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        float w = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.data(), text.data() + mid).x;
        if (w + ellipsisWidth <= maxWidth) lo = mid; else hi = mid - 1;
    }
    return text.substr(0, lo) + kEllipsis;
}

// Screen-space point sampling for gate body outlines (§ standard shapes
// library, below): a plain vector of points rather than ImDrawList's own
// Path*() buffer, since a shape needs its points twice (once filled, once
// stroked as the outline) and Path*() consumes its buffer on Fill/Stroke.
void appendArcPoints(std::vector<ImVec2>& pts, ImVec2 center, float radius,
                     float a0, float a1, int segments) {
    for (int i = 0; i <= segments; ++i) {
        float t = a0 + (a1 - a0) * (float(i) / float(segments));
        pts.push_back(ImVec2(center.x + cosf(t) * radius, center.y + sinf(t) * radius));
    }
}

void appendQuadBezierPoints(std::vector<ImVec2>& pts, ImVec2 p0, ImVec2 c, ImVec2 p1, int segments) {
    for (int i = 0; i <= segments; ++i) {
        float t = float(i) / float(segments);
        float u = 1.0f - t;
        pts.push_back(ImVec2(u * u * p0.x + 2.0f * u * t * c.x + t * t * p1.x,
                              u * u * p0.y + 2.0f * u * t * c.y + t * t * p1.y));
    }
}

// Draws a sampled body outline both filled and stroked (plus a thicker
// diagnosis-severity stroke on top, when flagged) -- shared tail end of every
// standard gate shape below.
void fillAndStrokeBody(ImDrawList* dl, const std::vector<ImVec2>& pts,
                       ImU32 fillColor, ImU32 diagOutline) {
    dl->AddConvexPolyFilled(pts.data(), int(pts.size()), fillColor);
    dl->AddPolyline(pts.data(), int(pts.size()), IM_COL32(0, 0, 0, 200), 1.5f, ImDrawFlags_Closed);
    if (diagOutline != 0)
        dl->AddPolyline(pts.data(), int(pts.size()), diagOutline, 3.0f, ImDrawFlags_Closed);
}

// Draws a bubble (small negation circle), touching the tip of a gate body's
// output side -- shared by NAND/NOR/XNOR/INV.
void drawNegationBubble(ImDrawList* dl, ImVec2 tip, float radius, ImU32 fillColor) {
    ImVec2 center(tip.x + radius, tip.y);
    dl->AddCircleFilled(center, radius, fillColor);
    dl->AddCircle(center, radius, IM_COL32(0, 0, 0, 200), 16, 1.5f);
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
        float dotR = zoomedSizeOrHidden(kPortDotBaseSize, sv.transform.scale, kMinPortDotSize);
        if (dotR <= 0.0f) continue; // too small to matter at this zoom -- same fade policy as labels

        ImVec2 worldP  = sv.portWorldPos(inst, p);
        ImVec2 screenP = sv.worldToScreen(worldP, canvasPos, canvasSize);
        bool   isLeft  = p.lx < 0.0f;

        ImU32 portColor = p.color != 0 ? p.color
            : (p.isInput ? IM_COL32(200, 80, 80, 255)
                         : IM_COL32(80, 200, 80, 255));

        dl->AddCircleFilled(screenP, dotR, portColor);
        // A merged bus pin gets an extra ring so it reads as "thicker" than
        // a scalar/single-bit pin, in addition to its "[hi:lo]" label.
        if (p.isBus)
            dl->AddCircle(screenP, dotR + 2.5f, portColor, 12, 2.0f);

        float fontSize = labelFontSize(kPortLabelBaseSize, sv.transform.scale);
        if (!p.name.empty() && fontSize > 0.0f) {
            ImFont* font = ImGui::GetFont();
            // Cap label width (in screen px, scaled with zoom like everything
            // else) so a long bus name can't sprawl across neighboring pins.
            const float kPortLabelMaxWorldWidth = 90.0f;
            std::string label = truncateToWidth(font, fontSize, p.name,
                                                 kPortLabelMaxWorldWidth * sv.transform.scale);
            if (!label.empty()) {
                ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str());
                ImVec2 lblPos   = isLeft
                    ? ImVec2(screenP.x - 4.0f - textSize.x, screenP.y - textSize.y * 0.5f)
                    : ImVec2(screenP.x + 4.0f,              screenP.y - textSize.y * 0.5f);
                dl->AddRectFilled(
                    ImVec2(lblPos.x - 2.0f, lblPos.y - 1.0f),
                    ImVec2(lblPos.x + textSize.x + 2.0f, lblPos.y + textSize.y + 1.0f),
                    IM_COL32(30, 30, 30, 210));
                dl->AddText(font, fontSize, lblPos, IM_COL32(220, 220, 220, 230), label.c_str());
            }
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

        // Diagnosis outline drawn on top so it stays visible regardless of
        // the partialInterface dashed border above.
        if (inst.diagOutline != 0)
            dl->AddRect(rmin, rmax, inst.diagOutline, 4.0f, 0, 3.5f);

        float instFontSize = labelFontSize(kInstanceLabelBaseSize, sv.transform.scale);
        if (!inst.name.empty() && instFontSize > 0.0f) {
            ImFont* font = ImGui::GetFont();
            float padding = std::max(2.0f, 6.0f * sv.transform.scale);
            float maxWidth = (rmax.x - rmin.x) - 2.0f * padding;
            std::string label = truncateToWidth(font, instFontSize, inst.name, maxWidth);
            if (!label.empty()) {
                ImVec2 textSize = font->CalcTextSizeA(instFontSize, FLT_MAX, 0.0f, label.c_str());
                ImVec2 textPos  = ImVec2((rmin.x + rmax.x) * 0.5f - textSize.x * 0.5f,
                                         rmin.y + padding);
                dl->AddText(font, instFontSize, textPos, contrastingTextColor(inst.color), label.c_str());
            }
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

        // Hierarchy expand/collapse glyph: a small "+"/"-" square straddling
        // the top-center of the box. Its world-space rect is shared with
        // EquipotentialView's click hit-test via hierToggleGlyphRect().
        if (canShowHierToggle(inst)) {
            float gx0, gy0, gx1, gy1;
            hierToggleGlyphRect(inst, gx0, gy0, gx1, gy1);
            ImVec2 gmin, gmax;
            sv.worldRectToScreen(gx0, gy0, gx1 - gx0, gy1 - gy0, canvasPos, canvasSize, gmin, gmax);
            dl->AddRectFilled(gmin, gmax, IM_COL32(40, 40, 40, 230), 3.0f);
            dl->AddRect(gmin, gmax, IM_COL32(200, 200, 200, 200), 3.0f, 0, 1.5f);
            const char* glyph = inst.hierExpanded ? "-" : "+";
            ImVec2 ts = ImGui::CalcTextSize(glyph);
            ImVec2 c((gmin.x + gmax.x) * 0.5f, (gmin.y + gmax.y) * 0.5f);
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), glyph);
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
    if (inst.diagOutline != 0)
        dl->AddTriangle(tl, bl, mr, inst.diagOutline, 3.0f);

    // Label ("assign") near top-left of the bounding box, small and subtle
    ImVec2 lblPos = ImVec2(tl.x + 4.0f, tl.y + 4.0f);
    dl->AddText(lblPos, IM_COL32(255, 255, 255, 180), "=");

    drawPorts(dl, inst, sv, canvasPos, canvasSize);
}

// ---------------------------------------------------------------------------
// Standard shapes library
//
// Traditional (non-IEC) gate symbols for PrimitiveType::{And,Nand,Or,Nor,
// Xor,Xnor,Inv,Buf,Dff}. Input/output pin positions are untouched -- they
// stay wherever EquipotentialView already placed them (left edge for
// inputs, right edge for outputs, evenly spaced by portLy()) -- these
// functions only draw a different body outline than the generic box inside
// the same inst.x/y/w/h, then call the same shared drawPorts(). Arity
// (2..N inputs) therefore needs no dedicated handling here: however many
// input ports the instance actually has already determines their spacing,
// and the body outline (D-shape / pointed shield / triangle) scales to
// whatever inst.w/inst.h the caller assigned, same as drawGenericInstance.
// ---------------------------------------------------------------------------

// AND / NAND — flat back, semicircular (bulging right) front. NAND adds a
// negation bubble at the tip.
static void drawAndLikeInstance(ImDrawList* dl, const InstanceShape& inst,
                                const SchematicView& sv,
                                const ImVec2& canvasPos, const ImVec2& canvasSize,
                                bool negated) {
    ImVec2 rmin, rmax;
    sv.worldRectToScreen(inst.x, inst.y, inst.w, inst.h, canvasPos, canvasSize, rmin, rmax);
    float cy = (rmin.y + rmax.y) * 0.5f;
    float xm = (rmin.x + rmax.x) * 0.5f;
    float r  = (rmax.y - rmin.y) * 0.5f;

    std::vector<ImVec2> pts;
    pts.push_back(ImVec2(rmin.x, rmin.y));
    appendArcPoints(pts, ImVec2(xm, cy), r, -1.5707963f, 1.5707963f, 24);
    pts.push_back(ImVec2(rmin.x, rmax.y));
    fillAndStrokeBody(dl, pts, inst.color, inst.diagOutline);

    if (negated) {
        float bubbleR = std::max(2.5f, r * 0.22f);
        drawNegationBubble(dl, ImVec2(xm + r, cy), bubbleR, inst.color);
    }

    drawPorts(dl, inst, sv, canvasPos, canvasSize);
}

// OR / NOR / XOR / XNOR — curved sides bulging outward to a point on the
// right; NOR/XNOR add a negation bubble at the tip; XOR/XNOR add the extra
// curved line just outside the input edge.
static void drawOrLikeInstance(ImDrawList* dl, const InstanceShape& inst,
                               const SchematicView& sv,
                               const ImVec2& canvasPos, const ImVec2& canvasSize,
                               bool negated, bool exclusive) {
    ImVec2 rmin, rmax;
    sv.worldRectToScreen(inst.x, inst.y, inst.w, inst.h, canvasPos, canvasSize, rmin, rmax);
    float cy = (rmin.y + rmax.y) * 0.5f;
    float sw = rmax.x - rmin.x;
    float sh = rmax.y - rmin.y;
    ImVec2 tip(rmax.x, cy);

    std::vector<ImVec2> pts;
    pts.push_back(ImVec2(rmin.x, rmin.y));
    appendQuadBezierPoints(pts, ImVec2(rmin.x, rmin.y),
                           ImVec2(rmin.x + sw * 0.5f, rmin.y - sh * 0.10f), tip, 16);
    appendQuadBezierPoints(pts, tip,
                           ImVec2(rmin.x + sw * 0.5f, rmax.y + sh * 0.10f),
                           ImVec2(rmin.x, rmax.y), 16);
    fillAndStrokeBody(dl, pts, inst.color, inst.diagOutline);

    if (exclusive) {
        float gap = std::max(2.0f, sw * 0.08f);
        dl->AddLine(ImVec2(rmin.x - gap, rmin.y), ImVec2(rmin.x - gap, rmax.y),
                    IM_COL32(0, 0, 0, 200), 1.5f);
    }

    if (negated) {
        float bubbleR = std::max(2.5f, sh * 0.11f);
        drawNegationBubble(dl, tip, bubbleR, inst.color);
    }

    drawPorts(dl, inst, sv, canvasPos, canvasSize);
}

// INV / BUF — triangle pointing right, same silhouette as the "assign"
// pass-through shape; INV adds a negation bubble at the tip.
static void drawBufLikeInstance(ImDrawList* dl, const InstanceShape& inst,
                                const SchematicView& sv,
                                const ImVec2& canvasPos, const ImVec2& canvasSize,
                                bool negated) {
    ImVec2 tl = sv.worldToScreen(ImVec2(inst.x,          inst.y),          canvasPos, canvasSize);
    ImVec2 bl = sv.worldToScreen(ImVec2(inst.x,          inst.y + inst.h), canvasPos, canvasSize);
    ImVec2 mr = sv.worldToScreen(ImVec2(inst.x + inst.w, inst.y + inst.h * 0.5f), canvasPos, canvasSize);

    dl->AddTriangleFilled(tl, bl, mr, inst.color);
    dl->AddTriangle(tl, bl, mr, IM_COL32(0, 0, 0, 200), 1.5f);
    if (inst.diagOutline != 0)
        dl->AddTriangle(tl, bl, mr, inst.diagOutline, 3.0f);

    if (negated) {
        float bubbleR = std::max(2.5f, (bl.y - tl.y) * 0.11f);
        drawNegationBubble(dl, mr, bubbleR, inst.color);
    }

    drawPorts(dl, inst, sv, canvasPos, canvasSize);
}

// True for a pin name commonly used for a flip-flop's clock input, so
// drawDffInstance() can mark it with the usual clock-triangle notch.
static bool looksLikeClockPinName(const std::string& name) {
    std::string n = name;
    for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return n == "clk" || n == "clock" || n == "ck" || n == "c";
}

// DFF (and other clocked sequential cells) — the generic box, plus the
// standard clock-triangle notch on the left edge at the clock pin's
// position, so a flop reads differently from a plain unclassified
// hierarchical/blackbox instance even though its outline is the same.
static void drawDffInstance(ImDrawList* dl, const InstanceShape& inst,
                            const SchematicView& sv,
                            const ImVec2& canvasPos, const ImVec2& canvasSize) {
    drawGenericInstance(dl, inst, sv, canvasPos, canvasSize);

    for (const auto& p : inst.ports) {
        if (p.direction != Direction::Input || !looksLikeClockPinName(p.name)) continue;
        ImVec2 world  = sv.portWorldPos(inst, p);
        float  notchH = std::min(inst.h * 0.3f, 10.0f);
        ImVec2 top  = sv.worldToScreen(ImVec2(inst.x,               world.y - notchH * 0.5f), canvasPos, canvasSize);
        ImVec2 bot  = sv.worldToScreen(ImVec2(inst.x,               world.y + notchH * 0.5f), canvasPos, canvasSize);
        ImVec2 apex = sv.worldToScreen(ImVec2(inst.x + notchH * 0.6f, world.y),               canvasPos, canvasSize);
        dl->AddLine(top, apex, IM_COL32(0, 0, 0, 200), 1.5f);
        dl->AddLine(apex, bot, IM_COL32(0, 0, 0, 200), 1.5f);
        break; // one clock pin is enough to draw the notch once
    }
}

// ---------------------------------------------------------------------------
// Gate dispatcher — add new PrimitiveType values here as the library grows
// ---------------------------------------------------------------------------
// To add a new gate type:
//   1. Write a static drawXxxInstance() function above with the same signature
//   2. Add a case below matching its PrimitiveType
// ---------------------------------------------------------------------------
void SchematicView::drawInstance(ImDrawList* dl, const InstanceShape& inst,
                                 const ImVec2& canvasPos, const ImVec2& canvasSize) const {
    switch (inst.primitiveType) {
        case PrimitiveType::Assign:
            drawAssignInstance(dl, inst, *this, canvasPos, canvasSize);
            break;
        case PrimitiveType::And:
            drawAndLikeInstance(dl, inst, *this, canvasPos, canvasSize, /*negated=*/false);
            break;
        case PrimitiveType::Nand:
            drawAndLikeInstance(dl, inst, *this, canvasPos, canvasSize, /*negated=*/true);
            break;
        case PrimitiveType::Or:
            drawOrLikeInstance(dl, inst, *this, canvasPos, canvasSize, /*negated=*/false, /*exclusive=*/false);
            break;
        case PrimitiveType::Nor:
            drawOrLikeInstance(dl, inst, *this, canvasPos, canvasSize, /*negated=*/true, /*exclusive=*/false);
            break;
        case PrimitiveType::Xor:
            drawOrLikeInstance(dl, inst, *this, canvasPos, canvasSize, /*negated=*/false, /*exclusive=*/true);
            break;
        case PrimitiveType::Xnor:
            drawOrLikeInstance(dl, inst, *this, canvasPos, canvasSize, /*negated=*/true, /*exclusive=*/true);
            break;
        case PrimitiveType::Buf:
            drawBufLikeInstance(dl, inst, *this, canvasPos, canvasSize, /*negated=*/false);
            break;
        case PrimitiveType::Inv:
            drawBufLikeInstance(dl, inst, *this, canvasPos, canvasSize, /*negated=*/true);
            break;
        case PrimitiveType::Dff:
            drawDffInstance(dl, inst, *this, canvasPos, canvasSize);
            break;
        default:
            drawGenericInstance(dl, inst, *this, canvasPos, canvasSize);
            break;
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

    // Too thin to matter at this zoom -- same fade policy as labels/pin dots.
    float thickness = zoomedSizeOrHidden(kWireBaseThickness, transform.scale, kMinWireThickness);
    if (thickness <= 0.0f) return;
    if (net.isBus) thickness *= 2.0f;

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

    // Top-level nets draw first (so top-level instances render on top), as
    // before. A net nested inside an expanded instance (containerShapeId set)
    // is drawn later instead -- see drawSubtree below -- so that instance's
    // opaque box fill doesn't get painted over it afterward.
    for (const auto& net : nets) {
        if (net.containerShapeId < 0) drawNet(dl, net, canvasPos, canvasSize);
    }

    // Recursive draw: a box, then the nets internal to it, then its nested
    // children on top of those nets -- so hierarchy embedding never hides a
    // wire under a box's fill or a child under its own internal wiring.
    std::function<void(const InstanceShape&)> drawSubtree = [&](const InstanceShape& inst) {
        drawInstance(dl, inst, canvasPos, canvasSize);
        for (const auto& net : nets)
            if (net.containerShapeId == inst.id) drawNet(dl, net, canvasPos, canvasSize);
        for (const auto& child : instances)
            if (child.parentShapeId == inst.id) drawSubtree(child);
    };
    for (const auto& inst : instances) {
        if (inst.parentShapeId < 0) drawSubtree(inst);
    }

    dl->PopClipRect();
}
