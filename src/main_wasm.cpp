// Browser / VSCode webview entry point.
// Compiled with Emscripten; uses WebSocketProvider to talk to a remote naja server.
#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>

#include "AppLogic.h"
#include "GUIData.h"
#include "WebSocketProvider.h"
#include "Console.h"

static AppState g_state;

static void mainLoop() {
  try {
    appFrame(g_state);
  } catch (const std::exception& e) {
    Console::Error("Exception in main loop: " + std::string(e.what()));
  }
}

int main() {
  SDL_Init(SDL_INIT_VIDEO);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

  g_state.window = SDL_CreateWindow(
    "najaeda Netlist Viewer",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    1280, 720,
    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  g_state.glContext = SDL_GL_CreateContext(g_state.window);
  SDL_GL_MakeCurrent(g_state.window, g_state.glContext);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui_ImplSDL2_InitForOpenGL(g_state.window, g_state.glContext);
  ImGui_ImplOpenGL3_Init("#version 300 es");

  g_state.guiData  = new GUIData();
  g_state.provider = new WebSocketProvider("ws://localhost:8081/ws");

  try {
    setupProvider(g_state);
  } catch (const std::exception& e) {
    Console::Error("Error setting up provider: " + std::string(e.what()));
  }

  emscripten_set_main_loop(mainLoop, 0, true);

  // Cleanup (unreachable under Emscripten, kept for completeness).
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(g_state.glContext);
  SDL_DestroyWindow(g_state.window);
  SDL_Quit();
  return 0;
}

#endif // __EMSCRIPTEN__
