#pragma once

// Large designs push a single ImDrawList (one window/viewport) past 65536
// vertices -- easily reached by a schematic with a few hundred instances,
// each contributing box/port/label geometry, plus wires. Desktop OpenGL
// (3.2+) dodges the resulting assert via ImGuiBackendFlags_RendererHasVtxOffset,
// but WebGL/GL ES have no glDrawElementsBaseVertex equivalent, so the WASM
// build has no such escape hatch (see imgui_impl_opengl3.cpp's
// IMGUI_IMPL_OPENGL_MAY_HAVE_VTX_OFFSET guard, which excludes ES/WebGL).
// 32-bit indices remove the ceiling on both targets; the OpenGL3 backend
// already branches on sizeof(ImDrawIdx) to pick GL_UNSIGNED_INT vs
// GL_UNSIGNED_SHORT, so no backend changes are needed.
#define ImDrawIdx unsigned int
