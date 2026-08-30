#pragma once

// core/re_context.hpp — REContext: global state mirror per GL context (SPEC §3, T2).
//
// Formerly DrawContext (T2 rename — not draw-only; also used by tests and
// readback, per user direction). REContext is the SOLE owner of raw GL
// state calls in RenderEngine. render/, app/, tests/, broker/, scene/ use core/
// wrappers; never raw gl* (guardrail gpu_api_ownership). utils/ offscreen context
// delegates raw loading to core::loadCoreGl.
//
// Design (T2): global per GL context while preserving future multi-context /
// multi-threaded rendering. REContext::current() is a thread_local pointer set
// by loadCoreGl() / makeContextCurrent(GLFWwindow*) mapping GLFWwindow* →
// REContextState. Each GL context owns its own mirror (viewport, clearColor,
// depthTest, blend, blendFunc, cull, FBO bindings, VAO, program, image units).
// Single-threaded gate stays (SPEC §5), but state is not process-global
// singleton — worker threads with private contexts get private mirrors with no
// lock; shared resources noted out-of-scope (GL share groups). Explicit
// invalidation at boundaries (SampleHarness post-ImGui, invalidate() public for
// tests) — no auto-guess. Per-frame local ctx instances in renderers deleted;
// (void)ctx params dropped from IRenderable::drawLayer (T2).
//
// The REContext instance owns its own dirty-flag cache + spy (instance per
// FrameContext, SRP via instance). The global free-function API (core::setViewport
// etc.) delegates to REContext::current()'s cache, so 2 layers sharing state
// within the same GL context issue only 1 glViewport (cross-pass dedup spy).
// A fresh instance or a fresh GL context starts cold (no cross-context bleed).
// Worker threads with private contexts get private mirrors with no lock because
// t_current is thread_local and the GLFWwindow* → REContext map is mutex-guarded
// for creation only; per-frame access is lock-free via the thread_local pointer.
//
// T4 (single ledger): viewport/clear/depth/blend each have exactly one writer
// (REContext). LinkedListOIT now takes explicit REContext& from ViewCompositor
// (which already holds REContext::current() per view), so View prologues and
// OIT share one ledger (spy proves setViewport duplicate 2->1, analytic count 1
// not >0; no skipped-glEnable class bugs). Legacy global cache + spy + legacy
// cache-invalidate shim deleted (mechanical grep count 0). ImGui backends
// save/restore GL state; invalidate() is called explicitly after ImGui to keep
// the mirror in sync (no auto-guess).

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "core/vertex_array.hpp"
#include "data/result.hpp"

struct GLFWwindow;

namespace re::core {

/// Set the viewport rectangle (glViewport). Coordinates are in pixels.
/// Delegates to REContext::current().
void setViewport(int x, int y, int width, int height) noexcept;

/// Set the clear color used by clearColor (glClearColor). Components in
/// [0, 1]. Delegates to REContext::current().
void setClearColor(float r, float g, float b, float a) noexcept;

/// Clear the currently-bound draw framebuffer's color buffer to the clear
/// color (glClear GL_COLOR_BUFFER_BIT).
void clearColor() noexcept;

/// Bind the default framebuffer (0) as the draw+read framebuffer
/// (glBindFramebuffer(GL_FRAMEBUFFER, 0)).
void bindDefaultFramebuffer() noexcept;

/// Enable/disable the depth test (glEnable/glDisable GL_DEPTH_TEST).
/// Delegates to REContext::current().
void enableDepthTest() noexcept;
void disableDepthTest() noexcept;

/// Enable/disable blending (glEnable/glDisable GL_BLEND).
void enableBlend() noexcept;
void disableBlend() noexcept;

/// Bind `vao` and issue an indexed triangle draw.
data::Result<void> drawElements(const VertexArray& vao, std::size_t indexCount);

/// Bind `vao` and issue a non-indexed draw covering `vertexCount` vertices
/// (glDrawArrays GL_TRIANGLES). Used by the SSBO+gl_VertexID view-quad strip
/// of LineRenderer (V7 T5): 6 virtual verts per segment (a±n*wA,b±n*wB) with
/// n=perp(viewport*(b−a)), so drawLayer issues glDrawArrays(6*N,0). The VAO is
/// a dummy empty VAO (no attributes) because the vertex shader derives all
/// positions from the SSBO via gl_VertexID and the per-segment s cumulative
/// length(viewport*(b−a)) that the CPU populates. This keeps raw draw calls
/// confined to core/ (guardrail gpu_api_ownership, render is GL-call-free) and
/// mirrors the existing drawElements wrapper (SPEC §3, T5).
data::Result<void> drawArrays(const VertexArray& vao, std::uint32_t vertexCount);

/// Memory barriers.
void memoryBarrierShaderStorage() noexcept;
void memoryBarrierBufferUpdate() noexcept;
void memoryBarrierAll() noexcept;
void finish() noexcept;

/// Image binding.
void bindImageR32ui(const Texture2D& texture, std::uint32_t unit) noexcept;
void unbindImage(std::uint32_t unit) noexcept;

/// Premultiplied-over blend.
void enablePremultipliedOverBlend() noexcept;

// ---------------------------------------------------------------------------
// Draw-state cache + test-injectable GL-call spy (SPEC §9 V2.10).
// ---------------------------------------------------------------------------

/// Per-wrapper raw-GL call counts observed by the spy.
struct RESpyCounts {
    int viewport = 0;
    int clearColor = 0;
    int enableDepthTest = 0;
    int disableDepthTest = 0;
    int enableBlend = 0;
    int disableBlend = 0;
    int blendFunc = 0;
};

/// Backward-compatibility alias: old DrawSpyCounts name.
using DrawSpyCounts = RESpyCounts;

/// Return a snapshot of the current GL context's spy counts (via REContext::current()).
RESpyCounts getRESpyCounts() noexcept;
/// Alias for backward compat.
inline DrawSpyCounts getDrawSpyCounts() noexcept { return getRESpyCounts(); }

/// Reset spy counters to zero for the current GL context (cache intact).
void resetRESpyCounts() noexcept;
inline void resetDrawSpyCounts() noexcept { resetRESpyCounts(); }

/// Invalidate the current GL context's dirty-flag cache (also resets spy).
void invalidateRECache() noexcept;

/// Copy a pixel rectangle via glBlitFramebuffer.
data::Result<void> blit(const Framebuffer& source, int srcX, int srcY,
                        int srcWidth, int srcHeight,
                        const Framebuffer* destination, int dstX, int dstY,
                        int dstWidth, int dstHeight);

// ---------------------------------------------------------------------------
// REContext — global per GL context, instance per FrameContext (T2).
// ---------------------------------------------------------------------------

/// REContext — global state mirror per GL context, instance per FrameContext.
///
/// Each GL context (GLFWwindow*) owns its own mirror (viewport, clearColor,
/// depthTest, blend, blendFunc, cull, FBO bindings, VAO, program, image units).
/// REContext::current() is thread_local and set by loadCoreGl() /
/// makeContextCurrent(GLFWwindow*). Each context's mirror is independent —
/// worker threads with private contexts get private mirrors with no lock; shared
/// resources are out-of-scope (GL share groups). Explicit invalidation at
/// boundaries (SampleHarness post-ImGui, invalidate() public for tests) — no
/// auto-guess.
///
/// Instance API: per-object cache+spy value type. One REContext per frame on a
/// single GL context would have been the old DrawContext model; now the GLOBAL
/// current() is used so 2 layers sharing state within the same GL context issue
/// only 1 glViewport (dedup), while a fresh REContext instance or a different
/// GL context starts cold.
///
/// Header-only value type for instance methods; global per-context state lives
/// in re_context.cpp (thread_local + map).
class REContext {
   public:
    using SpyCounts = RESpyCounts;

    REContext() noexcept = default;

    // ---- instance state wrappers (operate on this instance's cache/spy) ----
    // All GL-call bodies live out-of-line in core/re_context.cpp so this
    // public header stays GL-call-free (no <glad/gl.h> leak). The header
    // only declares the interface; the cache/spy bookkeeping plus the raw
    // gl* calls are implemented in the .cpp where glad is included privately.

    void setViewport(int x, int y, int width, int height) noexcept;

    bool viewportRect(int& x, int& y, int& width, int& height) const noexcept;

    void setClearColor(float r, float g, float b, float a) noexcept;

    void clearColor() noexcept;
    void clearDepth() noexcept;

    void enableDepthTest() noexcept;
    void disableDepthTest() noexcept;

    void enableBlend() noexcept;
    void disableBlend() noexcept;

    void enablePremultipliedOverBlend() noexcept;

    /// Begin one draw pass — THE pass prologue (exactly ONE definition).
    /// Binds framebuffer, sets viewport, installs clear color and clears,
    /// then sets depth/blend state. Delegates to this instance's cached wrappers.
    /// Out-of-line in re_context.cpp (GL-call-free header).
    void beginPass(Framebuffer* /*borrow*/ framebuffer, std::uint32_t width,
                   std::uint32_t height, float clearR, float clearG,
                   float clearB, float clearA, bool depthTest = false) noexcept;

    SpyCounts getSpyCounts() const noexcept { return spy_; }
    void resetSpyCounts() noexcept { spy_ = SpyCounts{}; }
    void invalidate() noexcept {
        cache_ = Cache{};
        spy_ = SpyCounts{};
    }

    // ---- global per-GL-context API ----

    /// Return the REContext for the current thread's current GL context.
    /// Thread-local pointer `t_current` is set by `setCurrentWindow` /
    /// `setCurrentEgl` and points into one of two mutex-guarded maps:
    /// `g_windowMap` (GLFWwindow*) or `g_eglMap` (EGLContext). The mutex is
    /// held only for map insertion/lookup, not for `t_current` access — the
    /// single-threaded contract (nfr.md:24) keeps per-frame GL work
    /// thread-local and lock-free, while still isolating per-EGLContext state.
    /// EGL contexts therefore start cold (no viewport/clearColor bleed) even on
    /// the same thread; the fallback `t_fallback` is retained only for the
    /// non-EGL fallback path (no display, no EGL) where no EGLContext exists.
    /// When RE_HAS_EGL is not defined the EGL map is absent and fallback is used.
    static REContext& current() noexcept;

    /// Set the current REContext for this thread to the state belonging to
    /// `window` (creates if not present). Pass nullptr for the EGL/surfaceless
    /// fallback or when no window is current. No GL call — pure cache selection.
    static void setCurrentWindow(GLFWwindow* window) noexcept;

    /// Clear the mapping for `window` (called on window destruction).
    static void clearWindow(GLFWwindow* window) noexcept;

    /// Make `window`'s GL context current on this thread and set REContext::current()
    /// to its mirror (glfwMakeContextCurrent + setCurrentWindow). No-op if window null.
    static void makeCurrent(GLFWwindow* window) noexcept;

    /// Sync REContext::current() from the currently-current GLFW context
    /// (via glfwGetCurrentContext). Called by loadCoreGl() after glad loads.
    static void syncFromGLFW() noexcept;

    /// Set the current REContext for this thread to the state belonging to
    /// EGL `ctx` (creates if not present). `dpy` is kept for future display-
    /// scoped extensions but the key is `ctx` alone. No GL call — pure cache.
    /// Uses void* for EGL handles to keep the public header EGL-free (guardrail
    /// no EGL leak to core headers; typed EGLDisplay/EGLContext live only in
    /// utils/offscreen_context.hpp and core/re_context.cpp with RE_HAS_EGL guard).
    static void setCurrentEgl(void* dpy, void* ctx) noexcept;

    /// Clear the mapping for EGL `ctx` (called on EGL context destruction).
    static void clearEgl(void* ctx) noexcept;

    /// Explicit invalidation of the current context's cache (public for tests
    /// and SampleHarness post-ImGui boundary — no auto-guess).
    static void invalidateCurrent() noexcept { current().invalidate(); }

    /// Read RGBA8 pixels from the currently-bound read framebuffer (test-consumed
    /// readback). Thin wrapper reachable via REContext::current().readRgba8 for
    /// test_utils façade discipline (raw readback stays in core/re_context.cpp
    /// as the sole raw anchor per T18 — no second site in test_utils).
    data::Result<void> readRgba8(std::uint32_t x, std::uint32_t y,
                                 std::uint32_t width, std::uint32_t height,
                                 std::vector<std::uint8_t>& out) const;

    /// Buffer target for readback via REContext (avoids exposing glad types in
    /// the public header; maps to GL_SHADER_STORAGE_BUFFER /
    /// GL_TRANSFORM_FEEDBACK_BUFFER internally).
    enum class BufferTarget {
        ShaderStorage,
        TransformFeedback,
    };

    /// Read back buffer data via the REContext raw anchor (test-consumed
    /// readback, guardrail no_production_readback). The raw call lives only
    /// in core/re_context.cpp; test_utils and render delegate here so no
    /// second anchor exists. Every context-setting GL call still flows through
    /// T2 REContext — no test helper touches raw GL.
    data::Result<void> readBufferSubData(BufferTarget target,
                                         std::uint32_t bufferId,
                                         std::size_t byteOffset,
                                         std::size_t byteSize,
                                         void* out) const;

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
        // Extended per-context mirror (T2): cull, FBO bindings, VAO, program, image units.
        // Stored for completeness — future render passes may query them without raw GL.
        bool hasCull = false;
        bool cullEnabled = false;
        bool hasFbo = false;
        std::uint32_t fboId = 0u;
        bool hasVao = false;
        std::uint32_t vaoId = 0u;
        bool hasProgram = false;
        std::uint32_t programId = 0u;
        // Image unit bindings (0..7 tracked, 0 = unbound).
        bool hasImageUnit[8] = {false, false, false, false, false, false, false, false};
        std::uint32_t imageUnitTex[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    };
    Cache cache_{};
    SpyCounts spy_{};
};

/// Backward-compatibility alias: DrawContext was renamed to REContext in T2.
/// New code uses REContext; old tests and headers may still mention DrawContext.
using DrawContext = REContext;

} // namespace re::core
