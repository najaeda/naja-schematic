#pragma once

// Renders the currently-loaded RTL source text (see SourceStore) into a
// read-only, line-numbered, scrollable panel; scrolls to and highlights the
// target line once each time a new file is loaded.
class SourceView {
  public:
    static void render();
};
