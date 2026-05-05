#include "AppLogic.h"

#include <iostream>

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>
#include <SDL_opengl.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "INetlistProvider.h"
#include "NetlistTree.h"
#include "GUIData.h"
#include "Types.h"
#include "Console.h"
#include "EquipotentialView.h"

// ---------------------------------------------------------------------------
// Provider setup — identical message dispatch for both WASM and native modes.
// ---------------------------------------------------------------------------

void setupProvider(AppState& state) {
  state.guiData->netlist_ = new NetlistTree(state.provider);

  state.provider->on_open([&state]() {
    state.connected = true;
    Console::Log("Connected to netlist provider");
    state.provider->send(R"({"request":"load_root"})");
  });

  state.provider->on_message([&state](const std::string& msg) {
    Console::Log("Message received: " + msg);
    std::string clean = msg;
    auto nullPos = clean.find('\0');
    if (nullPos != std::string::npos) clean.resize(nullPos);
    while (!clean.empty() &&
           (clean.back() == '\n' || clean.back() == '\r' ||
            clean.back() == ' '  || clean.back() == '\t')) {
      clean.pop_back();
    }

    json j;
    try {
      j = json::parse(clean);
    } catch (const std::exception& e) {
      Console::Error("Failed to parse JSON: " + std::string(e.what()));
      return;
    }

    std::string resp = j.value("response", "");
    if (resp.empty()) {
      Console::Error("Missing response field.");
      return;
    }

    if (resp == "root_response" || resp == "root_loaded") {
      Console::Log("Root node data received");
      const auto& root = j["root"];
      if (root.contains("has_terms") || root.contains("has_primitives") || root.contains("has_instances")) {
        InstanceResponseJson data = root.get<InstanceResponseJson>();
        state.guiData->netlist_->createRootNode(
          data.name, data.design_ref,
          data.has_terms, data.has_primitives, data.has_instances);
      } else {
        DesignRef designRef{};
        if (root.contains("design_ref")) {
          designRef = root["design_ref"].get<DesignRef>();
        }
        state.guiData->netlist_->createRootNode(
          root.value("name", std::string("<unnamed root>")),
          designRef, false, false, root.value("has_children", false));
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
            item.name       = child.value("name", "");
            item.model_name = child.value("model_name", "");
            item.child_id   = child.value("instance_id", 0);
            if (child.contains("design_ref"))
              item.design_ref = child["design_ref"].get<DesignRef>();
            bool hasChildren   = child.value("has_children", false);
            item.has_terms     = false;
            item.has_primitives = false;
            item.has_instances  = hasChildren;
            children.push_back(std::move(item));
          }
        }
      } else {
        InstancesResponseJson data = j.get<InstancesResponseJson>();
        gui_id   = data.gui_id;
        children = std::move(data.children);
      }
      auto parent = state.guiData->netlist_->getNode(gui_id);
      if (!parent) {
        Console::Error("Cannot find node: " + std::to_string(gui_id));
        return;
      }
      if (parent->hasChildren()) {
        Console::Error("internal error: node already has children");
        return;
      }
      parent->createChildren();
      for (auto& instance : children) {
        parent->createInstanceNode(
          instance.name, instance.child_id, instance.design_ref,
          instance.has_terms, instance.has_primitives, instance.has_instances);
      }
    } else if (resp == "terms_response") {
      TermsResponseJson data = j.get<TermsResponseJson>();
      auto parent = state.guiData->netlist_->getNode(data.gui_id);
      if (!parent) {
        Console::Error("Cannot find node: " + std::to_string(data.gui_id));
        return;
      }
      if (parent->hasChildren()) {
        Console::Error("internal error: node already has children");
        return;
      }
      parent->createChildren();
      for (auto& term : data.children) {
        parent->createTermNode(term.name, term.child_id,
                               Direction(term.direction), term.msb, term.lsb);
      }
    } else if (resp == "equipotential_response") {
      Console::Log("Equipotential data received");
      state.guiData->equipotential_ = new Equipotential(j.get<Equipotential>());
    } else if (resp == "error") {
      std::cerr << "Backend error: " << j["message"] << std::endl;
    }
  });

  state.provider->on_error([](const std::string& err) {
    Console::Error("Provider error: " + err);
  });

  state.provider->on_close([]() {
    Console::Error("Provider connection closed");
  });

  state.provider->start();
}

// ---------------------------------------------------------------------------
// Per-frame rendering — identical for both WASM and native entry points.
// ---------------------------------------------------------------------------

bool appFrame(AppState& state) {
  SDL_Event event;
  bool quit = false;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL2_ProcessEvent(&event);
    if (event.type == SDL_QUIT) {
      quit = true;
    }
  }

  ImGuiIO& io = ImGui::GetIO();
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  // ==== Top Menu Bar ====
  static bool openDialogVisible = false;
  static char openPathBuf[1024] = {};

  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open...", "Ctrl+O")) {
        openDialogVisible = true;
        openPathBuf[0] = '\0';
      }
      if (ImGui::MenuItem("About")) {
        std::cout << "About clicked" << std::endl;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      if (ImGui::MenuItem("Zoom In",  "Ctrl++")) EquipotentialView::zoomIn();
      if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) EquipotentialView::zoomOut();
      if (ImGui::MenuItem("Fit",      "Ctrl+0")) EquipotentialView::fitView();
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

  // Ctrl+O shortcut
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) {
    openDialogVisible = true;
    openPathBuf[0] = '\0';
  }

  // ==== Open file dialog ====
  if (openDialogVisible) {
    ImGui::OpenPopup("Open Netlist");
    openDialogVisible = false;
  }
  ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Always);
  if (ImGui::BeginPopupModal("Open Netlist", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Netlist file path:");
    ImGui::SetNextItemWidth(-1);
    bool confirmed = ImGui::InputText("##path", openPathBuf, sizeof(openPathBuf),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();
    if (ImGui::Button("Open", ImVec2(120, 0)) || confirmed) {
      std::string path(openPathBuf);
      if (!path.empty()) {
        state.provider->loadFile(path);
        delete state.guiData->netlist_;
        state.guiData->netlist_ = new NetlistTree(state.provider);
        state.provider->send(R"({"request":"load_root"})");
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // ==== Main layout ====
  ImGuiWindowFlags window_flags =
    ImGuiWindowFlags_NoTitleBar     | ImGuiWindowFlags_NoResize   |
    ImGuiWindowFlags_NoMove         | ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
    ImGuiWindowFlags_NoScrollbar    | ImGuiWindowFlags_NoScrollWithMouse;

  float menuBarHeight = ImGui::GetFrameHeight();
  ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight));
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - menuBarHeight));
  ImGui::Begin("MainWindow", nullptr, window_flags);
  {
    static float leftWidthTop    = 300.0f;
    static float leftWidthBottom = 300.0f;
    static float schematicHeight = -1.0f;
    const float  minPanelW = 0.0f;
    const float  maxPanelW = io.DisplaySize.x - 100.0f;
    const float  minPanelH = 40.0f;
    float totalH = ImGui::GetContentRegionAvail().y;
    if (schematicHeight < 0.0f) schematicHeight = totalH * 0.65f;

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
        ImGui::BeginChild("TreeScroll", ImVec2(0, 0), false,
          ImGuiWindowFlags_HorizontalScrollbar);
        {
          Console::Log(state.guiData->getString());
          if (state.guiData->netlist_ && state.connected) {
            state.guiData->netlist_->render();
          } else {
            ImGui::Text("Connecting to netlist provider...");
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
        EquipotentialView::renderSchematic(state.guiData->equipotential_);
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
        EquipotentialView::renderTable(state.guiData->equipotential_);
      }
      ImGui::EndChild();
    }
    ImGui::EndChild();
  }
  ImGui::End();

  // === GL render ===
  ImGui::Render();
  glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
  glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  SDL_GL_SwapWindow(state.window);

  return !quit;
}
