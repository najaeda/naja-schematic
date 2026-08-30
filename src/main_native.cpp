// Standalone desktop entry point.
// Compiled natively (no Emscripten); uses LocalSNLProvider to load netlists directly.
#ifndef __EMSCRIPTEN__

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>
#include <iostream>
#include <string>

#include "AppLogic.h"
#include "GUIData.h"
#include "LocalSNLProvider.h"
#include "Console.h"

int main(int argc, char* argv[]) {
  SDL_Init(SDL_INIT_VIDEO);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  AppState state;
  state.window = SDL_CreateWindow(
    "najaeda Netlist Viewer (Standalone)",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    1280, 720,
    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!state.window) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
    return 1;
  }

  state.glContext = SDL_GL_CreateContext(state.window);
  SDL_GL_MakeCurrent(state.window, state.glContext);
  SDL_GL_SetSwapInterval(1); // vsync

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui_ImplSDL2_InitForOpenGL(state.window, state.glContext);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  state.guiData  = new GUIData();

  auto* provider = new LocalSNLProvider();

  // CLI usage: naja-schematic-standalone [<design>] [--diagnosis <path>]
  // <design> is loaded by extension (.sv/.v/otherwise-assumed-SNL-directory);
  // --diagnosis pre-loads a diagnosis_response-shaped JSON so an external
  // caller (a script, or naja-agent's skill, after an edit-check cycle) can
  // open a fully annotated view in one command instead of requiring a human
  // to click through File > Open .../Load Diagnosis JSON... by hand.
  std::string designPath;
  std::string diagnosisPath;
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "--diagnosis" && i + 1 < argc) {
      diagnosisPath = argv[++i];
    } else if (designPath.empty()) {
      designPath = arg;
    }
  }

  if (!designPath.empty()) {
    std::string ext = designPath.size() >= 3 ? designPath.substr(designPath.rfind('.') + 1) : "";
    if (ext == "sv")
      provider->loadSystemVerilog({designPath});
    else if (ext == "v")
      provider->loadVerilog({designPath}, {});
    else
      provider->loadSNL(designPath); // assume SNL directory
  }

  state.provider = provider;
  setupProvider(state);

  // Load any requested diagnosis only after setupProvider() has completed
  // its synchronous root-load handshake (provider->start() at the end of
  // setupProvider fires it, which clears any previously-set diagnosis) --
  // otherwise the diagnosis we just loaded would be wiped immediately.
  if (!diagnosisPath.empty()) {
    loadDiagnosisFile(diagnosisPath);
  }

  while (appFrame(state)) {
    // loop until the window is closed
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(state.glContext);
  SDL_DestroyWindow(state.window);
  SDL_Quit();
  return 0;
}

#endif // __EMSCRIPTEN__
