#include <iostream>

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <emscripten.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "Console.h"
#include "WebSocketClient.h"
#include "NetlistTree.h"
#include "Types.h"

WebSocketClient* ws;
NetlistTree* netlist;
bool connected = false;

SDL_Window* window;
SDL_GLContext gl_context;

void setupWebsocket() {
  ws = new WebSocketClient("ws://localhost:8081/ws");
  netlist = new NetlistTree(ws);

  ws->on_open([&]() {
    connected = true;
    Console::Log("✅ Connected to backend");
    ws->send(R"({"request":"load_root"})");
  });

  ws->on_message([&](const std::string& msg) {
    auto j = json::parse(msg);
    std::string resp = j.value("response", "");
    if (resp == "root_response") {
      InstanceResponseJson data = j["root"].get<InstanceResponseJson>();
      netlist->createRootNode(
        data.name,
        data.design_ref,
        data.has_terms,
        data.has_primitives,
        data.has_instances
      );
    } else if (resp == "instances_response" or resp == "primitives_response") {
      InstancesResponseJson data = j.get<InstancesResponseJson>();
      auto parent = netlist->getNode(data.gui_id);
      if (!parent) {
        Console::Error("Cannot find node: " + std::to_string(data.gui_id));
      }
      if (parent->hasChildren()) {
        Console::Error("internal error");
      }
      parent->createChildren();
      for (auto instance: data.children) {
        parent->createInstanceNode(
          instance.name,
          instance.design_ref,
          instance.has_terms,
          instance.has_primitives,
          instance.has_instances
        );
      }
    } else if (resp == "terms_response") {
      TermsResponseJson data = j.get<TermsResponseJson>();
      auto parent = netlist->getNode(data.gui_id);
      if (!parent) {
        Console::Error("Cannot find node: " + std::to_string(data.gui_id));
      }
      if (parent->hasChildren()) {
        Console::Error("internal error");
      }
      parent->createChildren();
      for (auto term: data.children) {
        auto direction = Direction(term.direction);
        parent->createTermNode(term.name, direction, term.msb, term.lsb);
      }
    } else if (resp == "error") {
      std::cerr << "Backend error: " << j["message"] << std::endl;
    }
  });

  ws->on_error([&](const std::string& err) {
    Console::Error("⚠️ WebSocket error: " + err);
  });

  ws->on_close([&]() {
    Console::Error("❌ Connection closed");
  });
}

void mainLoopInternal() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL2_ProcessEvent(&event);
    if (event.type == SDL_QUIT) {
      //done = true;
    }
  }

  ImGuiIO& io = ImGui::GetIO();
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  // ==== Top Menu Bar ====
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("About")) {
        std::cout << "About clicked" << std::endl;
      }
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

    // === UI ===
  ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus |
                                  ImGuiWindowFlags_NoNavFocus;

  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("MainWindow", nullptr, window_flags);
  {
    // --- Left Side Panel ---
    ImGui::BeginChild("LeftPanel", ImVec2(300, 0), true);
    {
      ImGui::Text("Netlist Hierarchy");
      ImGui::Separator();

      ImGui::BeginChild("TreeScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
      {
        // Here you’d eventually render your NetlistTree
        if (netlist && connected) {
          netlist->render();
        } else {
          ImGui::Text("Root node not loaded yet...");
        }
      }
      ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("MainView", ImVec2(0, 0), true);
    {
      ImGui::Text("Main schematic area");
      //if (ImGui::Button("Quit")) done = true;
    }
    ImGui::EndChild();
  }
  ImGui::End();

  // === RENDER ===
  ImGui::Render();
  glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
  glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  SDL_GL_SwapWindow(window);
}

void mainLoop() {
  try {
    mainLoopInternal();
  } catch (const std::exception& e) {
    Console::Error("Exception in main loop: " + std::string(e.what()));
  }
}

int main() {
  SDL_Init(SDL_INIT_VIDEO);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

  window = SDL_CreateWindow("ImGui WASM",
                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  gl_context = SDL_GL_CreateContext(window);
  SDL_GL_MakeCurrent(window, gl_context);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
  ImGui_ImplOpenGL3_Init("#version 300 es");

  try {
    setupWebsocket();
  } catch (const std::exception& e) {
    Console::Error("Error setting up WebSocket: " + std::string(e.what()));
  }

  emscripten_set_main_loop(mainLoop, 0, true);

  // cleanup never reached under emscripten, but left for completeness
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(gl_context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}