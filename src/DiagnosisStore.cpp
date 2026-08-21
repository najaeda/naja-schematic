#include "DiagnosisStore.h"

#include <map>

namespace {
std::vector<DiagnosisItem> g_items;
std::map<std::string, std::vector<const DiagnosisItem*>> g_instanceIndex;
std::map<std::string, std::vector<const DiagnosisItem*>> g_netIndex;

std::string netKey(const std::string& pathKey, const std::string& terminal) {
  return pathKey + "::" + terminal;
}

void rebuildIndex() {
  g_instanceIndex.clear();
  g_netIndex.clear();
  for (const auto& item : g_items) {
    if (item.kind == DiagnosisKind::Instance) {
      g_instanceIndex[item.pathKey()].push_back(&item);
    } else {
      g_netIndex[netKey(item.pathKey(), item.terminal)].push_back(&item);
    }
  }
}

DiagnosisSeverity worstOf(const std::vector<const DiagnosisItem*>& items) {
  DiagnosisSeverity worst = DiagnosisSeverity::Info;
  for (const auto* item : items) {
    if (item->severity > worst) worst = item->severity;
  }
  return worst;
}
} // namespace

void DiagnosisStore::setDiagnostics(std::vector<DiagnosisItem> items) {
  g_items = std::move(items);
  rebuildIndex();
}

void DiagnosisStore::clear() {
  g_items.clear();
  g_instanceIndex.clear();
  g_netIndex.clear();
}

const std::vector<DiagnosisItem>& DiagnosisStore::all() { return g_items; }

std::vector<const DiagnosisItem*> DiagnosisStore::instanceDiagnostics(const std::string& pathKey) {
  auto it = g_instanceIndex.find(pathKey);
  return it != g_instanceIndex.end() ? it->second : std::vector<const DiagnosisItem*>{};
}

std::vector<const DiagnosisItem*> DiagnosisStore::netDiagnostics(const std::string& pathKey,
                                                                  const std::string& terminal) {
  auto it = g_netIndex.find(netKey(pathKey, terminal));
  return it != g_netIndex.end() ? it->second : std::vector<const DiagnosisItem*>{};
}

ImU32 DiagnosisStore::colorForSeverity(DiagnosisSeverity sev) {
  switch (sev) {
    case DiagnosisSeverity::Error:   return IM_COL32(230,  60,  60, 255);
    case DiagnosisSeverity::Warning: return IM_COL32(230, 170,  40, 255);
    case DiagnosisSeverity::Info:    return IM_COL32( 70, 160, 230, 255);
  }
  return 0;
}

ImU32 DiagnosisStore::instanceColor(const std::string& pathKey) {
  auto items = instanceDiagnostics(pathKey);
  if (items.empty()) return 0;
  return colorForSeverity(worstOf(items));
}

ImU32 DiagnosisStore::netColor(const std::string& pathKey, const std::string& terminal) {
  auto items = netDiagnostics(pathKey, terminal);
  if (items.empty()) return 0;
  return colorForSeverity(worstOf(items));
}
