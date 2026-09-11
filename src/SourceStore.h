#pragma once

#include <string>
#include <vector>

// Holds the most recently fetched RTL source text (from a source_response),
// split into lines, plus which line to scroll to/highlight. Same
// static/global state pattern as DiagnosisStore/EquipotentialView.
class SourceStore {
  public:
    static void setSource(const std::string& file, int line, const std::string& text);
    static void clear();

    static bool hasSource();
    static const std::string& file();
    static const std::vector<std::string>& lines();
    static int targetLine();
    // True once right after a fresh setSource() call, then false again --
    // lets the viewer scroll-to-line exactly once instead of every frame.
    static bool takePendingScroll();
};
