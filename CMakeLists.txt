cmake_minimum_required(VERSION 3.20)
project(naja-schematic)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------
# Detect Emscripten
# ------------------------------------------------------------
if("${CMAKE_CXX_COMPILER}" MATCHES "em\\+\\+")
  message(STATUS "Configuring for Emscripten (WASM) build")
  set(EMSCRIPTEN_BUILD TRUE)
endif()

# ------------------------------------------------------------
# ImGui sources (shared by both targets)
# ------------------------------------------------------------
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/thirdparty/imgui)

set(IMGUI_SOURCES
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

include_directories(
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
  ${CMAKE_SOURCE_DIR}/thirdparty
)

# ------------------------------------------------------------
# Core application sources (no platform-specific I/O)
# ------------------------------------------------------------
set(CORE_SOURCES
  src/Console.cpp
  src/Types.cpp
  src/GUIData.cpp
  src/NetlistTree.cpp
  src/EquipotentialView.cpp
  src/SchematicView.cpp
  src/AppLogic.cpp
  ${IMGUI_SOURCES}
)

# ============================================================
# Target 1: naja-schematic  — Browser / VSCode webview (WASM)
# ============================================================
if(EMSCRIPTEN_BUILD)
  message(STATUS "Building WASM target: naja-schematic")

  add_executable(naja-schematic
    ${CORE_SOURCES}
    src/WebSocketClient.cpp
    src/WebSocketProvider.cpp
    src/main_wasm.cpp
  )

  set(EM_COMPILE_FLAGS
    "-sUSE_SDL=2"
    "-fexceptions"
    "-sEXCEPTION_CATCHING_ALLOWED=['main','naja-schematic']"
    "--shell-file=${CMAKE_SOURCE_DIR}/src/shell_minimal.html"
  )

  set(EM_LINK_FLAGS
    "-sUSE_SDL=2"
    "-sUSE_WEBGL2=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sFULL_ES3=1"
    "-sASSERTIONS=1"
    "-sEXIT_RUNTIME=0"
    "-sWASM=1"
    "--shell-file=${CMAKE_SOURCE_DIR}/src/shell_minimal.html"
    "-lwebsocket.js"
    "-lembind"
    "-fexceptions"
    "-sEXCEPTION_STACK_TRACES=1"
    "-sEXCEPTION_CATCHING_ALLOWED=['main','naja-schematic']"
  )

  target_compile_options(naja-schematic PRIVATE ${EM_COMPILE_FLAGS})
  target_link_options(naja-schematic    PRIVATE ${EM_LINK_FLAGS})
  set_target_properties(naja-schematic  PROPERTIES SUFFIX ".html")

# ============================================================
# Target 2: naja-schematic-standalone — Native desktop app
# ============================================================
else()
  message(STATUS "Building native standalone target: naja-schematic-standalone")

  find_package(SDL2 REQUIRED)
  find_package(OpenGL REQUIRED)

  add_executable(naja-schematic-standalone
    ${CORE_SOURCES}
    src/LocalSNLProvider.cpp
    src/main_native.cpp
  )

  target_include_directories(naja-schematic-standalone PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(naja-schematic-standalone PRIVATE
    ${SDL2_LIBRARIES}
    OpenGL::GL
  )

  # Enable compile_commands.json for clangd / VS Code IntelliSense
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()
