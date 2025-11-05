#include <iostream>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <emscripten.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "WebSocketClient.h"

std::shared_ptr<WebSocketClient> ws;
bool connected = false;

SDL_Window* window;
SDL_GLContext gl_context;

void setup_websocket() {
    ws = std::make_shared<WebSocketClient>("ws://localhost:8081/ws");
    //netlist = std::make_shared<NetlistTree>(ws);

    ws->on_open = [&]() {
        connected = true;
        std::cout << "✅ Connected to backend" << std::endl;
        // Ask backend for root node
        ws->send(R"({"type":"LoadRoot"})");
    };

    ws->on_message = [&](const std::string& msg) {
        auto j = json::parse(msg);
        std::string resp = j.value("response", "");
        if (resp == "root_response") {
            //netlist->create_instance_node(j["root"]);
        } else if (resp == "instances_response") {
            //netlist->insert_instances(j["gui_id"], j["children"]);
        } else if (resp == "terms_response") {
            //netlist->insert_terms(j["gui_id"], j["children"]);
        } else if (resp == "instance_response") {
            //netlist->expand_instance(j["gui_id"], j["instance"]);
        } else if (resp == "error") {
            std::cerr << "Backend error: " << j["message"] << std::endl;
        }
    };

    ws->on_error = [](const std::string& err) {
        std::cerr << "⚠️ " << err << std::endl;
    };

    ws->on_close = []() {
        std::cerr << "❌ Connection closed" << std::endl;
    };
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
        // Left tree panel
        ImGui::BeginChild("LeftPanel", ImVec2(250, 0), true);
        {
            ImGui::Text("Tree View");
            ImGui::Separator();

            if (ImGui::TreeNode("Design")) {
                if (ImGui::TreeNode("Instances")) {
                    ImGui::BulletText("U1 : opamp");
                    ImGui::BulletText("U2 : resistor");
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode("Nets")) {
                    ImGui::BulletText("net_vcc");
                    ImGui::BulletText("net_gnd");
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
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