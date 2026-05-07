// EquipotentialView.cpp — merged instances, horizontal expansion
#include "EquipotentialView.h"

#include <algorithm>
#include <map>
#include <set>
#include <imgui.h>

#include "Types.h"
#include "SchematicView.h"
#include "INetlistProvider.h"

// ---------------------------------------------------------------------------
// Layout geometry constants
// ---------------------------------------------------------------------------
static constexpr float kInstW       = 180.0f;
static constexpr float kInstH       = 70.0f;
static constexpr float kColGap      = 120.0f;
static constexpr float kRowSpacing  = 24.0f;
static constexpr float kPortSpacing = 18.0f;
static constexpr float kLeftMargin  = 20.0f;
static constexpr float kNetVGap     = 80.0f;

// ---------------------------------------------------------------------------
// Internal item type
// ---------------------------------------------------------------------------
struct Item {
    std::string            label;       // port/term name (for matching, port rendering)
    std::string            fullName;    // slash-path for instances; term name for terms
    Direction              direction   = Direction::Inout;
    bool                   isTerm      = false;
    DesignRef              designRef{};
    unsigned               termChildId = 0;
    std::optional<int>     termBit;
    std::vector<unsigned>  pathIds;

    const std::string& key() const { return fullName.empty() ? label : fullName; }
};

// ---------------------------------------------------------------------------
// Static view state
// ---------------------------------------------------------------------------
static SchematicView      g_schematic;
static int                g_pendingZoomSteps = 0;
static bool               g_pendingFit       = false;
static bool               g_pendingClear     = false;
static INetlistProvider*  g_provider         = nullptr;

struct OccurrenceInfo { std::string pathKey; DesignRef designRef; };
struct PortEquiRequest {
    std::vector<unsigned> pathIds;
    unsigned              termId = 0;
    std::optional<int>    bit;
};
static std::map<int, OccurrenceInfo>  g_occInfoByShapeId;
static std::map<int, PortEquiRequest> g_portEquiByPortId;

static std::map<std::string, std::vector<EquipotentialView::ExpandedPort>> g_expandedInstances;
static std::set<std::string> g_pendingExpansions;

// Persistent layout state
static std::map<std::string, ImVec2>  g_placedPositions;  // key → world top-left
static std::set<const Equipotential*> g_laidOut;
static float                          g_layoutNextY = 0.f;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void buildItems(const Equipotential* eq,
                       std::vector<Item>& drivers,
                       std::vector<Item>& receivers) {
    for (const auto& bt : eq->terms) {
        Item item;
        item.label       = bt.getString();
        item.fullName    = bt.name;
        item.direction   = bt.direction;
        item.isTerm      = true;
        item.termChildId = bt.child_id;
        item.termBit     = bt.bit;
        (bt.direction == Direction::Input ? drivers : receivers).push_back(std::move(item));
    }
    for (const auto& occ : eq->occurrences) {
        Item item;
        item.label       = occ.term.getString();
        item.isTerm      = false;
        item.designRef   = occ.designRef;
        item.termChildId = occ.term.child_id;
        item.termBit     = occ.term.bit;
        item.pathIds     = occ.pathIds;
        std::string joined;
        bool first = true;
        for (const auto& seg : occ.path) {
            if (!first) joined += '/';
            joined += seg;
            first = false;
        }
        item.fullName  = std::move(joined);
        item.direction = occ.term.direction;
        (occ.term.direction == Direction::Output ? drivers : receivers).push_back(std::move(item));
    }
}

static float portLy(int i, int n) {
    return n > 1 ? -0.4f + 0.8f * float(i) / float(n - 1) : 0.0f;
}

// ---------------------------------------------------------------------------
// Layout: called once per new equip, stores positions in g_placedPositions
// ---------------------------------------------------------------------------
static void layoutEquipotential(const Equipotential* eq) {
    if (g_laidOut.count(eq)) return;
    g_laidOut.insert(eq);

    std::vector<Item> drivers, receivers;
    buildItems(eq, drivers, receivers);
    if (drivers.empty() && receivers.empty()) return;

    // Find anchor: first non-term already placed
    const Item* anchor      = nullptr;
    bool        anchorDrives = false;

    for (const auto& item : drivers) {
        if (!item.isTerm && g_placedPositions.count(item.key()))
            { anchor = &item; anchorDrives = true; break; }
    }
    if (!anchor) {
        for (const auto& item : receivers) {
            if (!item.isTerm && g_placedPositions.count(item.key()))
                { anchor = &item; anchorDrives = false; break; }
        }
    }

    if (!anchor) {
        // First/independent net: two-column layout below existing content
        const float lx = kLeftMargin;
        const float rx = kLeftMargin + kInstW + kColGap;
        float dyl = g_layoutNextY, dyr = g_layoutNextY;
        for (const auto& item : drivers) {
            if (!item.isTerm) {
                g_placedPositions.emplace(item.key(), ImVec2{lx, dyl});
                dyl += kInstH + kRowSpacing;
            }
        }
        for (const auto& item : receivers) {
            if (!item.isTerm) {
                g_placedPositions.emplace(item.key(), ImVec2{rx, dyr});
                dyr += kInstH + kRowSpacing;
            }
        }
        g_layoutNextY = std::max(dyl, dyr) + kNetVGap;
    } else {
        // Expansion: extend horizontally from anchor
        const ImVec2& ap  = g_placedPositions[anchor->key()];
        const auto& items = anchorDrives ? receivers : drivers;
        float newX = anchorDrives
            ? ap.x + kInstW + kColGap    // new receivers go right of driver
            : ap.x - kInstW - kColGap;  // new drivers go left of receiver
        float dy = ap.y;
        for (const auto& item : items) {
            if (!item.isTerm) {
                auto [it, inserted] = g_placedPositions.emplace(item.key(), ImVec2{newX, dy});
                if (inserted) dy += kInstH + kRowSpacing;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void EquipotentialView::zoomIn()    { g_pendingZoomSteps++; }
void EquipotentialView::zoomOut()   { g_pendingZoomSteps--; }
void EquipotentialView::fitView()   { g_pendingFit = true; }
void EquipotentialView::clearNets() { g_pendingClear = true; }

void EquipotentialView::resetLayout() {
    g_placedPositions.clear();
    g_laidOut.clear();
    g_expandedInstances.clear();
    g_pendingExpansions.clear();
    g_layoutNextY = 0.f;
    g_pendingFit   = true;
}

bool EquipotentialView::takePendingClear() {
    if (!g_pendingClear) return false;
    g_pendingClear = false;
    resetLayout();
    return true;
}

void EquipotentialView::setProvider(INetlistProvider* p) { g_provider = p; }

void EquipotentialView::applyInstanceExpansion(
        const std::string& pathKey,
        const std::vector<ExpandedPort>& ports) {
    g_expandedInstances[pathKey] = ports;
    g_pendingExpansions.erase(pathKey);
}

// ---------------------------------------------------------------------------
// Color helpers
// ---------------------------------------------------------------------------
namespace {
ImColor topTermColor(Direction d) {
    switch (d) {
        case Direction::Input:  return ImColor(255,   0,   0);
        case Direction::Output: return ImColor(  0, 255,   0);
        default:                return ImColor(255, 255,   0);
    }
}
ImColor occTermColor(Direction d) {
    switch (d) {
        case Direction::Input:  return ImColor(  0, 255,   0);
        case Direction::Output: return ImColor(255,   0,   0);
        default:                return ImColor(255, 255,   0);
    }
}
} // namespace

// ---------------------------------------------------------------------------
// renderSchematic
// ---------------------------------------------------------------------------
void EquipotentialView::renderSchematic(const std::vector<Equipotential*>& equipotentials) {
    const float scrollSz = ImGui::GetFrameHeight();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 csz   = ImVec2(std::max(10.f, avail.x - scrollSz),
                          std::max(10.f, avail.y - scrollSz));
    ImVec2 cur0  = ImGui::GetCursorPos();

    ImGui::BeginChild("##SC", csz, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 inner = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("cv", inner,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      cpos = ImGui::GetItemRectMin();

    g_schematic.handleInteraction(cpos, inner);

    if (g_pendingZoomSteps) {
        float f = g_pendingZoomSteps > 0 ? 1.1f : 0.9f;
        for (int i = 0; i < std::abs(g_pendingZoomSteps); ++i) g_schematic.zoomBy(f);
        g_pendingZoomSteps = 0;
    }
    if (g_pendingFit) { g_schematic.requestFit(true); g_pendingFit = false; }

    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##ctx");
    if (ImGui::BeginPopup("##ctx")) {
        if (ImGui::MenuItem("Clear all nets")) g_pendingClear = true;
        if (ImGui::MenuItem("Fit view"))       g_schematic.requestFit(true);
        ImGui::EndPopup();
    }

    // Double-click: port dot → load equip; instance box → expand interface
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && g_provider) {

        float s  = g_schematic.transform.scale;
        ImVec2 m = ImGui::GetMousePos();
        float wx = (m.x - cpos.x) / s + g_schematic.transform.offset.x
                   - g_schematic.transform.screenOrigin.x / s;
        float wy = (m.y - cpos.y) / s + g_schematic.transform.offset.y
                   - g_schematic.transform.screenOrigin.y / s;
        const float hr2 = (10.f / std::max(0.01f, s)) * (10.f / std::max(0.01f, s));
        bool hit = false;

        for (const auto& inst : g_schematic.instances) {
            for (const auto& port : inst.ports) {
                ImVec2 pw = g_schematic.portWorldPos(inst, port);
                float dx = wx - pw.x, dy = wy - pw.y;
                if (dx*dx + dy*dy > hr2) continue;
                auto it = g_portEquiByPortId.find(port.id);
                if (it == g_portEquiByPortId.end()) break;
                json j;
                j["request"] = "load_equipotential";
                j["path"]    = it->second.pathIds;
                j["term_id"] = it->second.termId;
                if (it->second.bit.has_value()) j["bit"] = it->second.bit.value();
                g_provider->send(j.dump());
                hit = true; break;
            }
            if (hit) break;
        }
        if (!hit) {
            for (const auto& inst : g_schematic.instances) {
                if (!inst.partialInterface) continue;
                if (wx < inst.x || wx > inst.x + inst.w) continue;
                if (wy < inst.y || wy > inst.y + inst.h) continue;
                auto it = g_occInfoByShapeId.find(inst.id);
                if (it == g_occInfoByShapeId.end()) break;
                if (g_pendingExpansions.count(it->second.pathKey)) break;
                g_pendingExpansions.insert(it->second.pathKey);
                json req;
                req["request"]                  = "expand_instance_terms";
                req["path_key"]                 = it->second.pathKey;
                req["design_ref"]["db_id"]      = it->second.designRef.db_id;
                req["design_ref"]["library_id"] = it->second.designRef.library_id;
                req["design_ref"]["design_id"]  = it->second.designRef.design_id;
                g_provider->send(req.dump());
                break;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Layout new equips (once each, stores positions)
    // -----------------------------------------------------------------------
    for (Equipotential* eq : equipotentials)
        if (eq) layoutEquipotential(eq);

    // -----------------------------------------------------------------------
    // Rebuild geometry: merged instances + per-equip wires
    // -----------------------------------------------------------------------
    g_schematic.instances.clear();
    g_schematic.nets.clear();
    g_occInfoByShapeId.clear();
    g_portEquiByPortId.clear();

    int nextInstId = 1, nextPortId = 1, totalItems = 0;

    // Pass 1: collect merged instance data across all equips
    struct PortSlot {
        int                   portId;
        std::string           name;
        Direction             direction;
        unsigned              termChildId;
        std::optional<int>    termBit;
        std::vector<unsigned> pathIds;
    };
    struct MInst {
        ImVec2                pos{};
        bool                  initialized = false;
        DesignRef             designRef{};
        std::vector<PortSlot> ports;
    };
    std::map<std::string, MInst> minsts;

    // Per-equip wire endpoints: {key, portId}
    struct WireEnd { std::string key; int portId; };
    std::vector<std::vector<WireEnd>> equiEnds(equipotentials.size());

    for (size_t ei = 0; ei < equipotentials.size(); ++ei) {
        Equipotential* eq = equipotentials[ei];
        if (!eq || (eq->terms.empty() && eq->occurrences.empty())) continue;

        std::vector<Item> drivers, receivers;
        buildItems(eq, drivers, receivers);
        totalItems += int(drivers.size() + receivers.size());

        for (int pass = 0; pass < 2; ++pass) {
            const auto& items = pass == 0 ? drivers : receivers;
            for (const auto& item : items) {
                if (item.isTerm) {
                    int pid = nextPortId++;
                    equiEnds[ei].push_back({ "term:" + item.label, pid });
                    g_portEquiByPortId[pid] = { item.pathIds, item.termChildId, item.termBit };
                    continue;
                }
                auto& mi = minsts[item.key()];
                if (!mi.initialized) {
                    mi.initialized = true;
                    mi.designRef   = item.designRef;
                    auto pit = g_placedPositions.find(item.key());
                    mi.pos = pit != g_placedPositions.end()
                        ? pit->second : ImVec2{kLeftMargin, 0.f};
                }
                // Find or create port slot for this port
                int pid = -1;
                for (const auto& ps : mi.ports) {
                    if (ps.name == item.label && ps.direction == item.direction)
                        { pid = ps.portId; break; }
                }
                if (pid < 0) {
                    pid = nextPortId++;
                    PortSlot ps;
                    ps.portId      = pid;
                    ps.name        = item.label;
                    ps.direction   = item.direction;
                    ps.termChildId = item.termChildId;
                    ps.termBit     = item.termBit;
                    ps.pathIds     = item.pathIds;
                    mi.ports.push_back(std::move(ps));
                    g_portEquiByPortId[pid] = { item.pathIds, item.termChildId, item.termBit };
                }
                equiEnds[ei].push_back({ item.key(), pid });
            }
        }
    }

    // Pass 2: build InstanceShapes from merged data
    std::map<std::string, int> keyToInstId;

    for (auto& [key, mi] : minsts) {
        auto expIt = g_expandedInstances.find(key);
        bool isExp = expIt != g_expandedInstances.end() && !expIt->second.empty();

        InstanceShape inst;
        inst.id    = nextInstId++;
        inst.x     = mi.pos.x;
        inst.y     = mi.pos.y;
        inst.w     = kInstW;
        inst.name  = key;
        inst.color = IM_COL32(100, 140, 200, 255);
        g_occInfoByShapeId[inst.id] = { key, mi.designRef };
        keyToInstId[key] = inst.id;

        if (isExp) {
            inst.partialInterface = false;
            int nL = 0, nR = 0;
            for (const auto& ep : expIt->second)
                (ep.direction == Direction::Input ? nL : nR)++;
            inst.h = std::max(kInstH, float(std::max(nL, nR)) * kPortSpacing + 10.f);

            int li = 0, ri = 0;
            for (const auto& ep : expIt->second) {
                bool isIn = (ep.direction == Direction::Input);
                // Reuse portId from accumulated data if this port was in a loaded equip
                int pid = nextPortId++;
                for (const auto& ps : mi.ports) {
                    if (ps.name == ep.name && ps.direction == ep.direction)
                        { pid = ps.portId; nextPortId--; break; }
                }
                // Instance pathIds from any known port slot
                auto pathIds = mi.ports.empty() ? std::vector<unsigned>{} : mi.ports[0].pathIds;
                g_portEquiByPortId[pid] = { pathIds, ep.childId, ep.bit };

                Port p;
                p.id = pid; p.name = ep.name;
                p.lx = isIn ? -0.5f : 0.5f;
                p.ly = isIn ? portLy(li++, nL) : portLy(ri++, nR);
                p.direction = ep.direction;
                p.isInput   = !isIn;
                inst.ports.push_back(p);
            }
        } else {
            inst.partialInterface = true;
            int nL = 0, nR = 0;
            for (const auto& ps : mi.ports)
                (ps.direction == Direction::Input ? nL : nR)++;
            inst.h = std::max(kInstH, float(std::max(nL, nR)) * kPortSpacing + 10.f);

            int li = 0, ri = 0;
            for (const auto& ps : mi.ports) {
                bool isIn = (ps.direction == Direction::Input);
                Port p;
                p.id = ps.portId; p.name = ps.name;
                p.lx = isIn ? -0.5f : 0.5f;
                p.ly = isIn ? portLy(li++, nL) : portLy(ri++, nR);
                p.direction = ps.direction;
                p.isInput   = !isIn;
                inst.ports.push_back(p);
            }
        }
        g_schematic.instances.push_back(std::move(inst));
    }

    // Pass 3: build term InstanceShapes per equip (not merged)
    for (size_t ei = 0; ei < equipotentials.size(); ++ei) {
        Equipotential* eq = equipotentials[ei];
        if (!eq) continue;

        std::vector<Item> drivers, receivers;
        buildItems(eq, drivers, receivers);

        // Determine column X for this equip's terms from placed instances
        float lx = kLeftMargin, rx = kLeftMargin + kInstW + kColGap;
        float bandY = 0.f; int nBand = 0;
        auto accInst = [&](const Item& item) {
            if (item.isTerm) return;
            auto pit = g_placedPositions.find(item.key());
            if (pit == g_placedPositions.end()) return;
            lx = std::min(lx, pit->second.x);
            rx = std::max(rx, pit->second.x + kInstW);
            bandY += pit->second.y; ++nBand;
        };
        for (const auto& d : drivers)   accInst(d);
        for (const auto& r : receivers) accInst(r);
        if (nBand) bandY /= float(nBand);

        float termLx = lx  - 40.f;
        float termRx = rx  + 20.f;

        for (int pass = 0; pass < 2; ++pass) {
            const auto& items = pass == 0 ? drivers : receivers;
            float ty = bandY;
            for (const auto& item : items) {
                if (!item.isTerm) { ty += kInstH + kRowSpacing; continue; }

                int pid = -1;
                for (const auto& we : equiEnds[ei]) {
                    if (we.key == "term:" + item.label) { pid = we.portId; break; }
                }
                if (pid < 0) continue;

                bool isInput = (item.direction == Direction::Input);
                InstanceShape inst;
                inst.id = nextInstId++;
                inst.x  = isInput ? termLx : termRx;
                inst.y  = ty;
                inst.w  = 0.f; inst.h = 0.f;
                inst.color = IM_COL32(0, 0, 0, 0);
                inst.partialInterface = false;

                Port p;
                p.id = pid; p.name = item.label;
                p.lx = isInput ? 1.0f : -1.0f;
                p.ly = 0.f;
                p.direction = item.direction;
                p.isInput   = isInput;
                inst.ports.push_back(p);
                keyToInstId["term:" + item.label + ":" + std::to_string(ei)] = inst.id;
                // Patch the equiEnds key so wire lookup works
                for (auto& we : equiEnds[ei]) {
                    if (we.key == "term:" + item.label)
                        we.key = "term:" + item.label + ":" + std::to_string(ei);
                }
                g_schematic.instances.push_back(std::move(inst));
                ty += 30.f;
            }
        }
    }

    // Pass 4: wires per equip
    for (size_t ei = 0; ei < equipotentials.size(); ++ei) {
        const auto& ends = equiEnds[ei];
        if (ends.size() < 2) continue;

        struct WR { int instId; int portId; };
        std::vector<WR> wrs;
        for (const auto& we : ends) {
            auto it = keyToInstId.find(we.key);
            if (it == keyToInstId.end()) continue;
            wrs.push_back({ it->second, we.portId });
        }
        if (wrs.size() < 2) continue;

        for (size_t i = 1; i < wrs.size(); ++i) {
            NetWire n;
            n.id          = int(g_schematic.nets.size()) + 1;
            n.srcInstance = wrs[0].instId;
            n.srcPortId   = wrs[0].portId;
            n.dstInstance = wrs[i].instId;
            n.dstPortId   = wrs[i].portId;
            n.color       = IM_COL32(200, 200, 100, 255);
            g_schematic.nets.push_back(n);
        }
    }

    static int lastTotal = -1;
    if (totalItems != lastTotal) { g_schematic.requestFit(true); lastTotal = totalItems; }
    g_schematic.updateFitIfNeeded(cpos, inner, 60.f);
    g_schematic.render(dl, cpos, inner);
    ImGui::EndChild();

    // Scrollbars
    ImVec2 wMin(-500.f, -500.f), wMax(500.f, 500.f);
    if (g_schematic.computeWorldBounds(wMin, wMax)) {
        wMin.x -= 120.f; wMin.y -= 120.f; wMax.x += 120.f; wMax.y += 120.f;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 24.f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,         ImVec4(0.12f, 0.12f, 0.12f, 1));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,       ImVec4(0.45f, 0.45f, 0.45f, 1));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.65f, 0.65f, 0.65f, 1));
    ImGui::SetCursorPos(ImVec2(cur0.x + csz.x, cur0.y));
    ImGui::VSliderFloat("##VS", ImVec2(scrollSz, csz.y),
        &g_schematic.transform.offset.y, wMax.y, wMin.y, "");
    ImGui::SetCursorPos(ImVec2(cur0.x, cur0.y + csz.y));
    ImGui::SetNextItemWidth(csz.x);
    ImGui::SliderFloat("##HS", &g_schematic.transform.offset.x, wMin.x, wMax.x, "");
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// renderTable
// ---------------------------------------------------------------------------
void EquipotentialView::renderTable(const std::vector<Equipotential*>& equipotentials) {
    if (!ImGui::BeginTable("eq_table", 2,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        return;

    ImGui::TableSetupColumn("Element",   ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Direction", ImGuiTableColumnFlags_WidthFixed, 70);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < equipotentials.size(); ++i) {
        const Equipotential* eq = equipotentials[i];
        if (!eq) continue;
        if (i > 0) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Separator();
            ImGui::TableSetColumnIndex(1); ImGui::Separator();
        }
        for (const auto& t : eq->terms) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", t.getString().c_str());
            ImGui::TableSetColumnIndex(1);
            auto c = topTermColor(t.direction);
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(c.Value.x, c.Value.y, c.Value.z, c.Value.w));
            ImGui::Text("%s", toString(t.direction));
            ImGui::PopStyleColor();
        }
        for (const auto& occ : eq->occurrences) {
            std::string label;
            for (const auto& seg : occ.path) { label += seg; label += '/'; }
            label += occ.term.getString();
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", label.c_str());
            ImGui::TableSetColumnIndex(1);
            auto c = occTermColor(occ.term.direction);
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(c.Value.x, c.Value.y, c.Value.z, c.Value.w));
            ImGui::Text("%s", toString(occ.term.direction));
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndTable();
}
