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

#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "core/vertex_array.hpp"
#include "data/result.hpp"

#include <glad/gl.h>

namespace re::core {

/// Set the viewport rectangle (glViewport). Coordinates are in pixels.
void setViewport(int x, int y, int width, int height) noexcept;

/// Set the clear color used by clearColor (glClearColor). Components in
/// [0, 1].
void setClearColor(float r, float g, float b, float a) noexcept;

/// Clear the currently-bound draw framebuffer's color buffer to the clear
/// color (glClear GL_COLOR_BUFFER_BIT).
void clearColor() noexcept;

/// Bind the default framebuffer (0) as the draw+read framebuffer
/// (glBindFramebuffer(GL_FRAMEBUFFER, 0)). Renderers bind this when
/// `RenderTarget::framebuffer` is null, i.e. when a sample renders into the
/// window's on-screen default framebuffer rather than an offscreen FBO (T12).
void bindDefaultFramebuffer() noexcept;

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

/// ---------------------------------------------------------------------------
/// Draw-state cache + test-injectable GL-call spy (SPEC §9 V2.10).
///
/// `core/draw.cpp` keeps an internal dirty-flag cache for the draw-state
/// wrappers (`setViewport`, `setClearColor`, `enable*`/`disable*`,
/// `enablePremultipliedOverBlend`). A second call with identical values is a
/// cache hit and issues **no** raw `gl*` call (motivator: OIT mid-frame
/// toggles). The free-function `core::Draw` API and the audit anchors
/// (`core::loadCoreGl`, `core::readRgba8`) are unchanged.
///
/// The spy records how many times each wrapper *actually* issued its raw GL
/// call — a cached call does not increment the counter. Tests can reset the
/// counters and invalidate the cache between cases.
/// ---------------------------------------------------------------------------

/// Per-wrapper raw-GL call counts observed by the spy.
struct DrawSpyCounts {
    /// Number of `glViewport` calls issued via `setViewport`.
    int viewport = 0;
    /// Number of `glClearColor` calls issued via `setClearColor`.
    int clearColor = 0;
    /// Number of `glEnable(GL_DEPTH_TEST)` calls via `enableDepthTest`.
    int enableDepthTest = 0;
    /// Number of `glDisable(GL_DEPTH_TEST)` calls via `disableDepthTest`.
    int disableDepthTest = 0;
    /// Number of `glEnable(GL_BLEND)` calls via `enableBlend` or
    /// `enablePremultipliedOverBlend`.
    int enableBlend = 0;
    /// Number of `glDisable(GL_BLEND)` calls via `disableBlend` or the
    /// post-composite restore in `LinkedListOIT::end`.
    int disableBlend = 0;
    /// Number of `glBlendFunc` calls via `enablePremultipliedOverBlend`.
    int blendFunc = 0;
};

/// Return a snapshot of the current spy counts.
DrawSpyCounts getDrawSpyCounts() noexcept;

/// Reset all spy counters to zero (cache is left intact).
void resetDrawSpyCounts() noexcept;

/// Invalidate the internal dirty-flag cache so the next wrapper call always
/// issues its raw GL call (also resets the spy counters for convenience in
/// tests — call `resetDrawSpyCounts` separately if you want to keep the
/// cache).
void invalidateDrawCache() noexcept;

/// ---------------------------------------------------------------------------
/// DrawContext — instance per FrameContext (SPEC §11.6 EOL-5, Q43:B SRP via
/// instance, V3.2a). Replaces the global static cache in `core/draw.cpp`
/// (`invalidateDrawCache()`) with a value-type instance that owns its own
/// dirty-flag cache + spy. One DrawContext per FrameContext — no global
/// `static Cache g_cache`; SRP via instance (one reason to change per frame),
/// test determinism via spy per context (not global).
///
/// Intended use: `core::DrawContext ctx; ctx.setViewport(...);` per frame;
/// duplicate `setViewport` on the SAME instance is a cache hit (exactly 1
/// glViewport), but a fresh `DrawContext` starts cold (no cross-frame bleed).
/// Header-only value type, no behavior change yet for the global free-function
/// API (which remains for V2 regression lock); new code migrates to instance.
/// ---------------------------------------------------------------------------

/// Instance draw-state cache + spy (header-only value type).
class DrawContext {
   public:
    /// Per-wrapper raw-GL call counts observed by the per-instance spy.
    using SpyCounts = DrawSpyCounts;

    DrawContext() noexcept = default;

    /// Set viewport — caches last rect, duplicate is no-op (no glViewport).
    void setViewport(int x, int y, int width, int height) noexcept {
        if (cache_.hasViewport && cache_.vpX == x && cache_.vpY == y &&
            cache_.vpW == width && cache_.vpH == height) {
            return;
        }
        cache_.hasViewport = true;
        cache_.vpX = x;
        cache_.vpY = y;
        cache_.vpW = width;
        cache_.vpH = height;
        ++spy_.viewport;
        glViewport(x, y, width, height);
    }

    /// Query the cached viewport rectangle (pure cache read, no GL call).
    /// Returns false when no setViewport happened on this context yet (cold
    /// cache); renderers whose draw needs the pixel size (ContourRenderer's
    /// screen-space thick-line expansion) read it from here instead of raw
    /// glGetIntegerv(GL_VIEWPORT), keeping render/ GL-call-free.
    bool viewportRect(int& x, int& y, int& width, int& height) const noexcept {
        if (!cache_.hasViewport) {
            return false;
        }
        x = cache_.vpX;
        y = cache_.vpY;
        width = cache_.vpW;
        height = cache_.vpH;
        return true;
    }

    /// Set clear color — exact float equality cache.
    void setClearColor(float r, float g, float b, float a) noexcept {
        if (cache_.hasClearColor && cache_.ccR == r && cache_.ccG == g &&
            cache_.ccB == b && cache_.ccA == a) {
            return;
        }
        cache_.hasClearColor = true;
        cache_.ccR = r;
        cache_.ccG = g;
        cache_.ccB = b;
        cache_.ccA = a;
        ++spy_.clearColor;
        glClearColor(r, g, b, a);
    }

    /// Clear color buffer to the last clear color.
    void clearColor() noexcept { glClear(GL_COLOR_BUFFER_BIT); }

    /// Depth test toggle — cached per capability.
    void enableDepthTest() noexcept {
        if (cache_.hasDepthTest && cache_.depthEnabled) return;
        cache_.hasDepthTest = true;
        cache_.depthEnabled = true;
        ++spy_.enableDepthTest;
        glEnable(GL_DEPTH_TEST);
    }
    void disableDepthTest() noexcept {
        if (cache_.hasDepthTest && !cache_.depthEnabled) return;
        cache_.hasDepthTest = true;
        cache_.depthEnabled = false;
        ++spy_.disableDepthTest;
        glDisable(GL_DEPTH_TEST);
    }

    /// Blend toggle — cached.
    void enableBlend() noexcept {
        if (cache_.hasBlend && cache_.blendEnabled) return;
        cache_.hasBlend = true;
        cache_.blendEnabled = true;
        ++spy_.enableBlend;
        glEnable(GL_BLEND);
    }
    void disableBlend() noexcept {
        if (cache_.hasBlend && !cache_.blendEnabled) return;
        cache_.hasBlend = true;
        cache_.blendEnabled = false;
        ++spy_.disableBlend;
        glDisable(GL_BLEND);
    }

    /// Premultiplied-over blend — caches both GL_BLEND enable and glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA).
    void enablePremultipliedOverBlend() noexcept {
        const bool needEnable = !(cache_.hasBlend && cache_.blendEnabled);
        const bool needFunc = !(cache_.hasBlendFunc &&
                                cache_.blendSrc == GL_ONE &&
                                cache_.blendDst == GL_ONE_MINUS_SRC_ALPHA);
        if (!needEnable && !needFunc) return;
        if (needEnable) {
            cache_.hasBlend = true;
            cache_.blendEnabled = true;
            ++spy_.enableBlend;
            glEnable(GL_BLEND);
        }
        if (needFunc) {
            cache_.hasBlendFunc = true;
            cache_.blendSrc = GL_ONE;
            cache_.blendDst = GL_ONE_MINUS_SRC_ALPHA;
            ++spy_.blendFunc;
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    /// Snapshot of per-instance spy counts.
    SpyCounts getSpyCounts() const noexcept { return spy_; }
    /// Reset spy counters to zero (cache intact).
    void resetSpyCounts() noexcept { spy_ = SpyCounts{}; }
    /// Invalidate per-instance cache (also resets spy) — instance analogue of global invalidateDrawCache().
    void invalidate() noexcept {
        cache_ = Cache{};
        spy_ = SpyCounts{};
    }

   private:
    struct Cache {
        bool hasViewport = false;
        int vpX = 0;
        int vpY = 0;
        int vpW = 0;
        int vpH = 0;
        bool hasClearColor = false;
        float ccR = 0.0f;
        float ccG = 0.0f;
        float ccB = 0.0f;
        float ccA = 0.0f;
        bool hasDepthTest = false;
        bool depthEnabled = false;
        bool hasBlend = false;
        bool blendEnabled = false;
        bool hasBlendFunc = false;
        unsigned int blendSrc = 0;
        unsigned int blendDst = 0;
    };
    Cache cache_{};
    SpyCounts spy_{};
};

/// Copy a `srcWidth` x `srcHeight` color pixel rectangle from the bottom-left
/// corner `(srcX, srcY)` of the `source` framebuffer into the `destination`
/// framebuffer (nullptr = the window's default framebuffer 0) at the
/// bottom-left corner `(dstX, dstY)`, scaled to `dstWidth` x `dstHeight`
/// (glBlitFramebuffer with GL_COLOR_BUFFER_BIT and GL_NEAREST). Coordinates are
/// in GL pixel convention (y = 0 is the bottom scanline), matching
/// core::setViewport and the ViewRect convention of the multi-view compositor
/// (render/view.hpp, SPEC §3.2).
///
/// v1 framebuffers are color-only (docs/core.md), so only the color buffer is
/// blitted. With equal source and destination sizes the copy is exact —
/// GL_NEAREST does not filter, so each source pixel lands pixel-for-pixel at
/// its destination position; the multi-view gate (V2 T2) relies on this to
/// read each view's scene color at the center of its pinned window rect.
///
/// Returns an error if no GL context is current (glBlitFramebuffer not
/// loaded).
data::Result<void> blit(const Framebuffer& source, int srcX, int srcY,
                        int srcWidth, int srcHeight,
                        const Framebuffer* destination, int dstX, int dstY,
                        int dstWidth, int dstHeight);

} // namespace re::core
