#pragma once

// core/draw.hpp — thin draw/state API (SPEC §3, guardrail gpu_api_ownership).
//
// core/ is the SOLE owner of raw GL calls in RenderEngine. render/, app/, and
// tests/ must not call glXxx directly; they issue draws and change draw state
// ONLY through these wrappers (plus the RAII objects + this module). The raw
// glViewport / glClearColor / glClear / glEnable / glDisable / glDrawElements
// calls all live in draw.cpp.
//
// This is the "thin core::Draw API" of the SPEC §3 module blueprint: a single
// anchor for the draw-state calls every renderer needs, kept GL-call-free in
// the header and implemented with raw GL under core/.

#include <cstddef>
#include <cstdint>

#include "core/vertex_array.hpp"
#include "data/result.hpp"

namespace re::core {

/// Set the viewport rectangle (glViewport). Coordinates are in pixels.
void setViewport(int x, int y, int width, int height) noexcept;

/// Set the clear color used by clearColor (glClearColor). Components in
/// [0, 1].
void setClearColor(float r, float g, float b, float a) noexcept;

/// Clear the currently-bound draw framebuffer's color buffer to the clear
/// color (glClear GL_COLOR_BUFFER_BIT).
void clearColor() noexcept;

/// Enable/disable the depth test (glEnable/glDisable GL_DEPTH_TEST).
void enableDepthTest() noexcept;
void disableDepthTest() noexcept;

/// Enable/disable blending (glEnable/glDisable GL_BLEND). v1 uses standard
/// premultiplied-alpha compositing (SPEC §3 OIT); see docs/render.md.
void enableBlend() noexcept;
void disableBlend() noexcept;

/// Bind `vao` and issue an indexed triangle draw of `indexCount` indices
/// (GL_TRIANGLES, GL_UNSIGNED_INT; the element buffer is captured by the VAO).
///
/// Returns an error if no GL context is current (the draw function pointer is
/// not loaded). The program must already be installed (ShaderProgram::use).
data::Result<void> drawElements(const VertexArray& vao, std::size_t indexCount);

} // namespace re::core
