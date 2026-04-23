#include <iostream>

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <emscripten.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "NetlistTree.h"
#include "GUIData.h"
#include "Types.h"
#include "Console.h"
#include "WebSocketClient.h"
#include "EquipotentialView.h"

WebSocketClient* ws;
bool connected = false;

SDL_Window* window;
SDL_GLContext gl_context;
GUIData* guiData;

void setupWebsocket() {
  ws = new WebSocketClient("ws://localhost:8081/ws");
  guiData->netlist_ = new NetlistTree(ws);

  ws->on_open([&]() {
    connected = true;
    Console::Log("✅ Connected to backend");
    ws->send(R"({"request":"load_root"})");
  });

  ws->on_message([&](const std::string& msg) {
    Console::Log("📩 Message received: " + msg);
    std::string clean = msg;
    auto nullPos = clean.find('\0');
    if (nullPos != std::string::npos) {
      clean.resize(nullPos);
    }
    while (!clean.empty() && (clean.back() == '\n' || clean.back() == '\r' || clean.back() == ' ' || clean.back() == '\t')) {
      clean.pop_back();
    }
    json j;
    try {
      j = json::parse(clean);
    } catch (const std::exception& e) {
      Console::Error("Failed to parse JSON message: " + std::string(e.what()));
      return;
    }
    std::string resp = j.value("response", "");
    if (resp.empty()) {
      Console::Error("Missing response field in message.");
    }
    if (resp == "root_response" || resp == "root_loaded") {
      Console::Log("✅ Root node data received");
      const auto& root = j["root"];
      if (root.contains("has_terms") || root.contains("has_primitives") || root.contains("has_instances")) {
        InstanceResponseJson data = root.get<InstanceResponseJson>();
        guiData->netlist_->createRootNode(
          data.name,
          data.design_ref,
          data.has_terms,
          data.has_primitives,
          data.has_instances
        );
      } else {
        DesignRef designRef{};
        if (root.contains("design_ref")) {
          designRef = root["design_ref"].get<DesignRef>();
        }
        const bool hasChildren = root.value("has_children", false);
        guiData->netlist_->createRootNode(
          root.value("name", std::string("<unnamed root>")),
          designRef,
          false,
          false,
          hasChildren
        );
      }
    } else if (resp == "instances_response" || resp == "primitives_response" || resp == "children_loaded") {
      unsigned gui_id = 0;
      std::vector<InstanceResponseJson> children;
      if (resp == "children_loaded") {
        gui_id = j.value("node_gui_id", 0);
        const auto& rawChildren = j["children"];
        if (rawChildren.is_array()) {
          for (const auto& child : rawChildren) {
            InstanceResponseJson item;
            item.name = child.value("name", "");
            item.model_name = child.value("model_name", "");
            item.child_id = child.value("instance_id", 0);
            if (child.contains("design_ref")) {
              item.design_ref = child["design_ref"].get<DesignRef>();
            }
            bool hasChildren = child.value("has_children", false);
            item.has_terms = false;
            item.has_primitives = false;
            item.has_instances = hasChildren;
            children.push_back(std::move(item));
          }
        }
      } else {
        InstancesResponseJson data = j.get<InstancesResponseJson>();
        gui_id = data.gui_id;
        children = std::move(data.children);
      }
      auto parent = guiData->netlist_->getNode(gui_id);
      if (!parent) {
        Console::Error("Cannot find node: " + std::to_string(gui_id));
        return;
      }
      if (parent->hasChildren()) {
        Console::Error("internal error");
      }
      parent->createChildren();
      for (auto instance: children) {
        parent->createInstanceNode(
          instance.name,
          instance.child_id,
          instance.design_ref,
          instance.has_terms,
          instance.has_primitives,
          instance.has_instances
        );
      }
    } else if (resp == "terms_response") {
      TermsResponseJson data = j.get<TermsResponseJson>();
      auto parent = guiData->netlist_->getNode(data.gui_id);
      if (!parent) {
        Console::Error("Cannot find node: " + std::to_string(data.gui_id));
        return;
      }
      if (parent->hasChildren()) {
        Console::Error("internal error");
      }
      parent->createChildren();
      for (auto term: data.children) {
        auto direction = Direction(term.direction);
        parent->createTermNode(term.name, term.child_id, direction, term.msb, term.lsb);
      }
    } else if (resp == "equipotential_response") {
      Console::Log("✅ Equipotential data received");
      Equipotential equipotential = j.get<Equipotential>();
      guiData->equipotential_ = new Equipotential(equipotential);
      //auto terms = j["terms"];
      //for (const auto& termJson : terms) {
      //  BitTerm term;
      //  term.name = termJson.value("name", "");
      //  term.child_id = termJson.value("child_id", 0);
      //  char dirChar = termJson.value("direction", 'I');
      //  switch (dirChar) {
      //    case 'I':
      //      term.direction = Direction::Input;
      //      break;
      //    case 'O':
      //      term.direction = Direction::Output;
      //      break;
      //    case 'B':
      //      term.direction = Direction::Inout;
      //      break;
      //    default:
      //      term.direction = Direction::Input;
      //      break;
      //  }
      //  term.bit = termJson.value("bit", std::optional<int>{});
      //  equipotential.addTerm(term);
      //}
      // Handle equipotential data here
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
    if (ImGui::BeginMenu("View")) {
      if (ImGui::MenuItem("Zoom In", "Ctrl++")) {
        EquipotentialView::zoomIn();
      }
      if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) {
        EquipotentialView::zoomOut();
      }
      if (ImGui::MenuItem("Fit", "Ctrl+0")) {
        EquipotentialView::fitView();
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
                                  ImGuiWindowFlags_NoNavFocus |
                                  ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse;

  float menuBarHeight = ImGui::GetFrameHeight();
  ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight));
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - menuBarHeight));
  ImGui::Begin("MainWindow", nullptr, window_flags);
  {
    static float leftWidthTop    = 300.0f;
    static float leftWidthBottom = 300.0f;
    static float schematicHeight = -1.0f;
    const float minPanelW = 0.0f;
    const float maxPanelW = io.DisplaySize.x - 100.0f;
    const float minPanelH = 40.0f;
    float totalH = ImGui::GetContentRegionAvail().y;
    if (schematicHeight < 0.0f) schematicHeight = totalH * 0.65f;

    // Helper: draw a vertical splitter and adjust the given width
    auto vSplitter = [&](const char* id, float& width) {
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f,0.3f,0.3f,0.5f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.2f,0.2f,0.2f,0.5f));
      ImGui::Button(id, ImVec2(4.0f, -1));
      ImGui::PopStyleColor(3);
      if (ImGui::IsItemActive()) {
        width += ImGui::GetIO().MouseDelta.x;
        if (width < minPanelW) width = minPanelW;
        if (width > maxPanelW) width = maxPanelW;
      }
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    };

    // === Top row: tree | vsplitter | schematic ===
    ImGui::BeginChild("SchematicRow", ImVec2(0, schematicHeight), false,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
      ImGui::BeginChild("TreePanel", ImVec2(leftWidthTop, 0), true);
      {
        ImGui::Text("Netlist Hierarchy");
        ImGui::Separator();
        ImGui::BeginChild("TreeScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        {
          Console::Log(guiData->getString());
          if (guiData->netlist_ && connected) {
            guiData->netlist_->render();
          } else {
            ImGui::Text("Root node not loaded yet...");
          }
        }
        ImGui::EndChild();
      }
      ImGui::EndChild();

      vSplitter("##VSplitTop", leftWidthTop);

      ImGui::SameLine();
      ImGui::BeginChild("SchematicPanel", ImVec2(0, 0), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      {
        EquipotentialView::renderSchematic(guiData->equipotential_);
      }
      ImGui::EndChild();
    }
    ImGui::EndChild();

    // === Horizontal splitter ===
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f,0.3f,0.3f,0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.2f,0.2f,0.2f,0.5f));
    ImGui::Button("##HSplitter", ImVec2(-1, 4.0f));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemActive()) {
      schematicHeight += ImGui::GetIO().MouseDelta.y;
      float maxH = totalH - minPanelH - 4.0f;
      if (schematicHeight < minPanelH) schematicHeight = minPanelH;
      if (schematicHeight > maxH)      schematicHeight = maxH;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    // === Bottom row: left panel | vsplitter | table ===
    ImGui::BeginChild("TableRow", ImVec2(0, 0), false,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
      if (leftWidthBottom > 0.0f) {
        ImGui::BeginChild("TableLeftPanel", ImVec2(leftWidthBottom, 0), true);
        ImGui::EndChild();
      }

      vSplitter("##VSplitBottom", leftWidthBottom);

      ImGui::SameLine();
      ImGui::BeginChild("TablePanel", ImVec2(0, 0), true,
        ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_HorizontalScrollbar);
      {
        EquipotentialView::renderTable(guiData->equipotential_);
      }
      ImGui::EndChild();
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

  window = SDL_CreateWindow("najaeda Netlist Viewer",
                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  gl_context = SDL_GL_CreateContext(window);
  SDL_GL_MakeCurrent(window, gl_context);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
  ImGui_ImplOpenGL3_Init("#version 300 es");

  guiData = new GUIData();

  try {
    setupWebsocket();
  } catch (const std::exception& e) {
    Console::Error("Error setting up WxebSocket: " + std::string(e.what()));
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
