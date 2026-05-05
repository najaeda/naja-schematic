#pragma once

#include <SDL.h>
#include <SDL_opengl.h>

class INetlistProvider;
class GUIData;

// Shared application state used by both native and WASM entry points.
struct AppState {
  SDL_Window*       window    {nullptr};
  SDL_GLContext     glContext {};
  GUIData*          guiData   {nullptr};
  INetlistProvider* provider  {nullptr};
  bool              connected {false};
};

// Wire all provider callbacks (on_open, on_message, on_close, on_error)
// and call provider->start().  Must be called once after SDL/GL/ImGui init.
void setupProvider(AppState& state);

// One frame: poll events, build ImGui, render.
// Returns false when the application should quit.
bool appFrame(AppState& state);
