cmake_minimum_required(VERSION 3.20)
project(naja-schematic)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------
# Detect Emscripten build
# --------------------------------------------
if("${CMAKE_CXX_COMPILER}" MATCHES "em\\+\\+")
    message(STATUS "Configuring for Emscripten (WASM) build")
    set(EMSCRIPTEN TRUE)
endif()

# --------------------------------------------
# ImGui sources
# --------------------------------------------
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
    ${CMAKE_SOURCE_DIR}/backend
    ${CMAKE_SOURCE_DIR}/thirdparty
)

# --------------------------------------------
# Your own sources
# --------------------------------------------
set(SOURCES
    src/Console.cpp
    src/Types.cpp
    src/GUIData.cpp
    src/NetlistTree.cpp
    src/WebSocketClient.cpp
    src/EquipotentialView.cpp
    src/main.cpp
    src/SchematicView.h
    src/SchematicView.cpp
    ${IMGUI_SOURCES}
)

add_executable(naja-schematic ${SOURCES})

# --------------------------------------------
# Platform-specific settings
# --------------------------------------------

if(EMSCRIPTEN)
    message(STATUS "Emscripten build detected — enabling SDL2 and WebGL2")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sEXCEPTION_CATCHING_ALLOWED=['main','naja-schematic'] -fexceptions")

    set(EM_COMPILE_FLAGS
        "-sUSE_SDL=2"
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
        "-sEXCEPTION_STACK_TRACES=1"
        "-sEXCEPTION_CATCHING_ALLOWED=['main','naja-schematic']"
        "-fexceptions"
    )

    # Apply to both compile and link steps
    target_compile_options(naja-schematic PRIVATE ${EM_COMPILE_FLAGS})
    target_link_options(naja-schematic PRIVATE ${EM_LINK_FLAGS})

    set_target_properties(naja-schematic PROPERTIES SUFFIX ".html")
else()
    find_package(SDL2 REQUIRED)
    include_directories(${SDL2_INCLUDE_DIRS})
    target_link_libraries(naja-schematic PRIVATE ${SDL2_LIBRARIES} GL)
endif()
