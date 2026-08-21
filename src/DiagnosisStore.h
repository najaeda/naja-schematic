#pragma once

#include <string>
#include <vector>

#include <imgui.h>

#include "Types.h"

// Global store for AI-diagnosis annotations (kepler-formal, naja-scope, ...)
// overlaid on the currently loaded netlist.
//
// Static/global state, following the same pattern as EquipotentialView: set
// once per diagnosis_response (or "Load Diagnosis JSON..."), queried by
// NetlistTree and EquipotentialView during rendering so they don't need a
// GUIData/provider pointer threaded through just for this.
class DiagnosisStore {
  public:
    // Replaces the current diagnosis set (a fresh diagnosis run).
    static void setDiagnostics(std::vector<DiagnosisItem> items);
    static void clear();
    static const std::vector<DiagnosisItem>& all();

    // pathKey: slash-joined instance-name path, root excluded ("" == top level).
    // Matches NetlistTree::getPathKey() and EquipotentialView's instance keys.
    static std::vector<const DiagnosisItem*> instanceDiagnostics(const std::string& pathKey);

    // terminal: pin/port base name, no bus-bit suffix (e.g. "Q", not "Q[3]").
    static std::vector<const DiagnosisItem*> netDiagnostics(const std::string& pathKey,
                                                             const std::string& terminal);

    // Convenience: color for the worst severity at this path, 0 if unflagged.
    static ImU32 instanceColor(const std::string& pathKey);
    static ImU32 netColor(const std::string& pathKey, const std::string& terminal);

    static ImU32 colorForSeverity(DiagnosisSeverity sev);
};
