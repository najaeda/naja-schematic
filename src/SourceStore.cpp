#include "SourceStore.h"

namespace {
std::string              g_file;
std::vector<std::string> g_lines;
int                       g_targetLine    = 0;
bool                      g_hasSource     = false;
bool                      g_pendingScroll = false;
} // namespace

void SourceStore::setSource(const std::string& file, int line, const std::string& text) {
  g_file = file;
  g_targetLine = line;
  g_lines.clear();

  size_t start = 0;
  while (start <= text.size()) {
    size_t nl = text.find('\n', start);
    if (nl == std::string::npos) {
      g_lines.push_back(text.substr(start));
      break;
    }
    std::string lineStr = text.substr(start, nl - start);
    if (!lineStr.empty() && lineStr.back() == '\r') lineStr.pop_back();
    g_lines.push_back(std::move(lineStr));
    start = nl + 1;
  }

  g_hasSource     = true;
  g_pendingScroll = true;
}

void SourceStore::clear() {
  g_file.clear();
  g_lines.clear();
  g_targetLine    = 0;
  g_hasSource     = false;
  g_pendingScroll = false;
}

bool SourceStore::hasSource() { return g_hasSource; }
const std::string& SourceStore::file() { return g_file; }
const std::vector<std::string>& SourceStore::lines() { return g_lines; }
int SourceStore::targetLine() { return g_targetLine; }

bool SourceStore::takePendingScroll() {
  bool v = g_pendingScroll;
  g_pendingScroll = false;
  return v;
}
