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

#include "core/texture2d.hpp"
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

/// Insert a memory barrier so SSBO writes from the capture pass are
/// visible to the composite pass (glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT
/// | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT)).
void memoryBarrierShaderStorage() noexcept;

/// Insert a memory barrier so buffer-object writes (the OIT node-allocator
/// counter) are visible to a subsequent client readback via
/// glGetBufferSubData (glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT)). Used by
/// the test-consumed counter readback (guardrail no_production_readback).
void memoryBarrierBufferUpdate() noexcept;

/// Bind `texture` as a read-write image on `unit` with GL_R32UI format
/// (glBindImageTexture, level 0, non-layered). Used by the OIT pipeline to
/// atomically update the per-pixel head pointers from the capture pass
/// (imageAtomicExchange) and read them in the composite pass (SPEC §3,
/// render/linked_list_oit.cpp).
void bindImageR32ui(const Texture2D& texture, std::uint32_t unit) noexcept;

/// Unbind the image on `unit` (glBindImageTexture with texture 0).
void unbindImage(std::uint32_t unit) noexcept;

/// Enable fixed-function blending configured for the v1 premultiplied-alpha
/// "over" compositing operator:
///   dst = src + (1 - src.a) * dst
/// i.e. GL_ONE, GL_ONE_MINUS_SRC_ALPHA with premultiplied source colors
/// (SPEC §3 OIT; render/linked_list_oit.cpp composite pass).
void enablePremultipliedOverBlend() noexcept;

} // namespace re::core
