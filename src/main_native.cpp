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

  // If a path is passed on the command line, load it by extension.
  if (argc > 1) {
    std::string path(argv[1]);
    std::string ext = path.size() >= 3 ? path.substr(path.rfind('.') + 1) : "";
    if (ext == "sv")
      provider->loadSystemVerilog({path});
    else if (ext == "v")
      provider->loadVerilog({path}, {});
    else
      provider->loadSNL(path); // assume SNL directory
  }

  state.provider = provider;
  setupProvider(state);

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
