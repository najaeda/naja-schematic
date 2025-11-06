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

void setup_websocket() {
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
      netlist->createRoot(
        data.name.value_or(std::string()),
        data.design_ref,
        data.has_primitives,
        data.has_instances,
        data.has_terms
      );
    } else if (resp == "instances_response") {
      //InstanceResponseJson data = j["instance"].get<InstanceResponseJson>();
      //auto parent = netlist->getRoot()->getNodeByGUID(data.gui_id);
      //netlist->insertInstances(j["gui_id"], j["children"]);
    } else if (resp == "terms_response") {
      //netlist->insertTerms(j["gui_id"], j["children"]);
    } else if (resp == "instance_response") {
      //netlist->expandInstance(j["gui_id"], j["instance"]);
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

void main_loop() {
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
        Console::Log("Rendering NetlistTree: " + std::to_string(connected));
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

  setup_websocket();

  emscripten_set_main_loop(main_loop, 0, true);

  // cleanup never reached under emscripten, but left for completeness
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(gl_context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}