#pragma once

// core/re_context.hpp — REContext: global state mirror per GL context (SPEC §3, T2).
//
// Formerly DrawContext (core/draw.hpp, T2 rename — not draw-only; also used by
// tests and readback, per user direction). REContext is the SOLE owner of raw GL
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

#include <glad/gl.h>

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

/// Memory barriers.
void memoryBarrierShaderStorage() noexcept;
void memoryBarrierBufferUpdate() noexcept;

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

    bool viewportRect(int& x, int& y, int& width, int& height) const noexcept {
        if (!cache_.hasViewport) return false;
        x = cache_.vpX;
        y = cache_.vpY;
        width = cache_.vpW;
        height = cache_.vpH;
        return true;
    }

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

    void clearColor() noexcept { glClear(GL_COLOR_BUFFER_BIT); }
    void clearDepth() noexcept { glClear(GL_DEPTH_BUFFER_BIT); }

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

    /// Begin one draw pass — THE pass prologue (exactly ONE definition).
    /// Binds framebuffer, sets viewport, installs clear color and clears,
    /// then sets depth/blend state. Delegates to this instance's cached wrappers.
    void beginPass(Framebuffer* /*borrow*/ framebuffer, std::uint32_t width,
                   std::uint32_t height, float clearR, float clearG,
                   float clearB, float clearA, bool depthTest = false) noexcept {
        if (framebuffer == nullptr) {
            bindDefaultFramebuffer();
        } else {
            framebuffer->bind();
        }
        setViewport(0, 0, static_cast<int>(width), static_cast<int>(height));
        setClearColor(clearR, clearG, clearB, clearA);
        clearColor();
        if (depthTest) {
            enableDepthTest();
            clearDepth();
        } else {
            disableDepthTest();
        }
        disableBlend();
    }

    SpyCounts getSpyCounts() const noexcept { return spy_; }
    void resetSpyCounts() noexcept { spy_ = SpyCounts{}; }
    void invalidate() noexcept {
        cache_ = Cache{};
        spy_ = SpyCounts{};
    }

    // ---- global per-GL-context API ----

    /// Return the REContext for the current thread's current GL context.
    /// Thread-local pointer set by loadCoreGl() / makeContextCurrent(GLFWwindow*).
    /// Each GLFWwindow* maps to its own REContextState; EGL/surfaceless contexts
    /// use a per-thread fallback. Single-threaded gate stays, but state is not
    /// process-global singleton — worker threads with private contexts get private
    /// mirrors with no lock.
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

    /// Explicit invalidation of the current context's cache (public for tests
    /// and SampleHarness post-ImGui boundary — no auto-guess).
    static void invalidateCurrent() noexcept { current().invalidate(); }

    /// Read RGBA8 pixels from the currently-bound read framebuffer (test-consumed
    /// readback). Thin wrapper that delegates to core::readRgba8 but is reachable
    /// via REContext::current().readRgba8 for test_utils façade discipline
    /// (raw glReadPixels stays in core/re_context.cpp).
    data::Result<void> readRgba8(std::uint32_t x, std::uint32_t y,
                                 std::uint32_t width, std::uint32_t height,
                                 std::vector<std::uint8_t>& out) const;

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
