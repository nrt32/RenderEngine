// core/re_context.cpp — REContext implementation (formerly draw.cpp, T2 rename).
//
// This module is the SOLE owner of the raw draw-state GL calls (glViewport /
// glClearColor / glClear / glEnable / glDisable / glDrawElements /
// glBlitFramebuffer). render/, app/, tests/ consume GL only through these
// wrappers (guardrail gpu_api_ownership). The free-function core::Draw API
// delegates to REContext::current() so 2 layers sharing state within the
// same GL context issue only 1 glViewport (cross-pass dedup, T2).
// T4: single-writer discipline — viewport/clear/depth/blend each have exactly
// one writer (REContext); LinkedListOIT now takes explicit REContext& from
// ViewCompositor (which already holds REContext::current() per view), so View
// prologues and OIT share one ledger (spy proves setViewport duplicate 2->1,
// analytic count 1 not >0; no skipped-glEnable bugs). Legacy global cache
// shim deleted (T4, mechanical grep count 0).
//
// Per-GL-context state: REContext::current() is thread_local pointer set by
// loadCoreGl() / makeContextCurrent(GLFWwindow*) mapping GLFWwindow* →
// REContextState. Each context owns its mirror (viewport, clearColor, depthTest,
// blend, blendFunc, cull, FBO bindings, VAO, program, image units). Worker
// threads with private contexts get private mirrors with no lock (thread_local);
// shared resources noted out-of-scope (GL share groups). Explicit invalidation
// at boundaries (SampleHarness post-ImGui, invalidate() public for tests) —
// ImGui backends save/restore GL state, so invalidate() is called after ImGui
// to keep the mirror in sync (no auto-guess).

#include "core/re_context.hpp"

#include <glad/gl.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/gl_error.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef RE_HAS_EGL
#include <EGL/egl.h>
#endif

namespace re::core {

namespace {

// Per-GLFWwindow* REContext map.
std::unordered_map<GLFWwindow*, std::unique_ptr<REContext>> g_windowMap;
std::mutex g_windowMutex;

// Per-EGLContext REContext map (T13) — mutex-guarded like g_windowMap:49,
// iteration 1 #11 pin — invalidate() on release:129 alone is insufficient;
// map is binding so second EGL context on same thread starts cold.
// Kept as void* map to keep public header EGL-free (typed EGL lives only in
// utils/offscreen_context.hpp with RE_HAS_EGL guard; core header uses void*).
std::unordered_map<void*, std::unique_ptr<REContext>> g_eglMap;
std::mutex g_eglMutex;
thread_local void* t_eglCurrent = nullptr;

// Thread-local current pointer and per-thread EGL/surfaceless fallback.
thread_local REContext* t_current = nullptr;
thread_local REContext t_fallback;

} // namespace

// ---------------------------------------------------------------------------
// REContext instance methods — out-of-line GL bodies (header GL-call-free)
// ---------------------------------------------------------------------------

void REContext::setViewport(int x, int y, int width, int height) noexcept {
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
    // VG11: debug hook — optional assert that no GL error is pending.
    assert(!hasPendingGlError() && "REContext::setViewport left pending GL error");
}

bool REContext::viewportRect(int& x, int& y, int& width, int& height) const noexcept {
    if (!cache_.hasViewport) return false;
    x = cache_.vpX;
    y = cache_.vpY;
    width = cache_.vpW;
    height = cache_.vpH;
    return true;
}

void REContext::setClearColor(float r, float g, float b, float a) noexcept {
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
    assert(!hasPendingGlError() && "REContext::setClearColor left pending GL error");
}

void REContext::clearColor() noexcept {
    glClear(GL_COLOR_BUFFER_BIT);
    assert(!hasPendingGlError() && "REContext::clearColor left pending GL error");
}
void REContext::clearDepth() noexcept {
    glClear(GL_DEPTH_BUFFER_BIT);
    assert(!hasPendingGlError() && "REContext::clearDepth left pending GL error");
}

void REContext::enableDepthTest() noexcept {
    if (cache_.hasDepthTest && cache_.depthEnabled) return;
    cache_.hasDepthTest = true;
    cache_.depthEnabled = true;
    ++spy_.enableDepthTest;
    glEnable(GL_DEPTH_TEST);
    assert(!hasPendingGlError() && "REContext::enableDepthTest left pending GL error");
}
void REContext::disableDepthTest() noexcept {
    if (cache_.hasDepthTest && !cache_.depthEnabled) return;
    cache_.hasDepthTest = true;
    cache_.depthEnabled = false;
    ++spy_.disableDepthTest;
    glDisable(GL_DEPTH_TEST);
    assert(!hasPendingGlError() && "REContext::disableDepthTest left pending GL error");
}

void REContext::enableBlend() noexcept {
    if (cache_.hasBlend && cache_.blendEnabled) return;
    cache_.hasBlend = true;
    cache_.blendEnabled = true;
    ++spy_.enableBlend;
    glEnable(GL_BLEND);
    assert(!hasPendingGlError() && "REContext::enableBlend left pending GL error");
}
void REContext::disableBlend() noexcept {
    if (cache_.hasBlend && !cache_.blendEnabled) return;
    cache_.hasBlend = true;
    cache_.blendEnabled = false;
    ++spy_.disableBlend;
    glDisable(GL_BLEND);
    assert(!hasPendingGlError() && "REContext::disableBlend left pending GL error");
}

void REContext::enablePremultipliedOverBlend() noexcept {
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

void REContext::beginPass(Framebuffer* /*borrow*/ framebuffer, std::uint32_t width,
                          std::uint32_t height, float clearR, float clearG,
                          float clearB, float clearA, bool depthTest) noexcept {
    if (framebuffer == nullptr) {
        // Direct GL call for default FBO (no cache — always bind 0).
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

// ---------------------------------------------------------------------------
// Global per-context API
// ---------------------------------------------------------------------------

REContext& REContext::current() noexcept {
    if (t_current != nullptr) return *t_current;
    // No window/EGL context has been set on this thread yet — use per-thread
    // fallback. For EGL, the per-EGLContext map (g_eglMap) is the binding
    // isolation; t_fallback is retained only for the non-EGL fallback path
    // (no display). Mutex is held only for map insertion/lookup, not for
    // t_current access — single-threaded contract (nfr.md:24) keeps per-frame
    // work lock-free while still isolating per-EGLContext state so the second
    // EGL context on the same thread starts cold (no viewport/clearColor bleed).
    t_current = &t_fallback;
    return *t_current;
}

void REContext::setCurrentWindow(GLFWwindow* window) noexcept {
    if (window == nullptr) {
        t_current = &t_fallback;
        return;
    }
    std::lock_guard<std::mutex> lock(g_windowMutex);
    auto it = g_windowMap.find(window);
    if (it != g_windowMap.end()) {
        t_current = it->second.get();
        return;
    }
    auto ptr = std::make_unique<REContext>();
    REContext* raw = ptr.get();
    g_windowMap.emplace(window, std::move(ptr));
    t_current = raw;
}

void REContext::clearWindow(GLFWwindow* window) noexcept {
    if (window == nullptr) return;
    std::lock_guard<std::mutex> lock(g_windowMutex);
    auto it = g_windowMap.find(window);
    if (it == g_windowMap.end()) return;
    if (t_current == it->second.get()) {
        t_current = &t_fallback;
    }
    g_windowMap.erase(it);
}

void REContext::makeCurrent(GLFWwindow* window) noexcept {
    if (window != nullptr) {
        glfwMakeContextCurrent(window);
    } else {
        glfwMakeContextCurrent(nullptr);
    }
    setCurrentWindow(window);
}

void REContext::syncFromGLFW() noexcept {
    GLFWwindow* cur = glfwGetCurrentContext();
    setCurrentWindow(cur);
}

void REContext::setCurrentEgl(void* /*dpy*/, void* ctx) noexcept {
    if (ctx == nullptr) {
        t_current = &t_fallback;
        t_eglCurrent = nullptr;
        return;
    }
    std::lock_guard<std::mutex> lock(g_eglMutex);
    auto it = g_eglMap.find(ctx);
    if (it != g_eglMap.end()) {
        t_current = it->second.get();
        t_eglCurrent = ctx;
        return;
    }
    auto ptr = std::make_unique<REContext>();
    REContext* raw = ptr.get();
    g_eglMap.emplace(ctx, std::move(ptr));
    t_current = raw;
    t_eglCurrent = ctx;
}

void REContext::clearEgl(void* ctx) noexcept {
    if (ctx == nullptr) return;
    std::lock_guard<std::mutex> lock(g_eglMutex);
    auto it = g_eglMap.find(ctx);
    if (it == g_eglMap.end()) return;
    if (t_current == it->second.get()) {
        t_current = &t_fallback;
        t_eglCurrent = nullptr;
    }
    g_eglMap.erase(it);
}

// ---------------------------------------------------------------------------
// Free-function API delegating to REContext::current()
// ---------------------------------------------------------------------------

void setViewport(int x, int y, int width, int height) noexcept {
    REContext::current().setViewport(x, y, width, height);
}

void setClearColor(float r, float g, float b, float a) noexcept {
    REContext::current().setClearColor(r, g, b, a);
}

void clearColor() noexcept {
    glClear(GL_COLOR_BUFFER_BIT);
}

void bindDefaultFramebuffer() noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void enableDepthTest() noexcept {
    REContext::current().enableDepthTest();
}

void disableDepthTest() noexcept {
    REContext::current().disableDepthTest();
}

void enableBlend() noexcept {
    REContext::current().enableBlend();
}

void disableBlend() noexcept {
    REContext::current().disableBlend();
}

void enablePremultipliedOverBlend() noexcept {
    REContext::current().enablePremultipliedOverBlend();
}

data::Result<void> drawElements(const VertexArray& vao, std::size_t indexCount) {
    if (glDrawElements == nullptr) {
        return data::makeError<void>(1, "drawElements: no GL context (glDrawElements not loaded)");
    }
    vao.bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, nullptr);
    return data::Result<void>(data::value);
}

void memoryBarrierShaderStorage() noexcept {
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void memoryBarrierBufferUpdate() noexcept {
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
}

void bindImageR32ui(const Texture2D& texture, std::uint32_t unit) noexcept {
    glBindImageTexture(static_cast<GLuint>(unit), texture.id(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
}

void unbindImage(std::uint32_t unit) noexcept {
    glBindImageTexture(static_cast<GLuint>(unit), 0u, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
}

RESpyCounts getRESpyCounts() noexcept {
    return REContext::current().getSpyCounts();
}

void resetRESpyCounts() noexcept {
    REContext::current().resetSpyCounts();
}

void invalidateRECache() noexcept {
    REContext::current().invalidate();
}

data::Result<void> blit(const Framebuffer& source, int srcX, int srcY,
                        int srcWidth, int srcHeight,
                        const Framebuffer* destination, int dstX, int dstY,
                        int dstWidth, int dstHeight) {
    if (glBlitFramebuffer == nullptr) {
        return data::makeError<void>(1, "blit: no GL context (glBlitFramebuffer not loaded)");
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source.id());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination == nullptr ? 0u : destination->id());
    glBlitFramebuffer(srcX, srcY, srcX + srcWidth, srcY + srcHeight, dstX, dstY,
                      dstX + dstWidth, dstY + dstHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    return data::Result<void>(data::value);
}

// REContext::readRgba8 — test_utils façade via REContext (raw readback stays
// here, core/ only — the sole raw readback anchor per T18).
// VG4: overflow check + PACK_ALIGNMENT save/restore.
data::Result<void> REContext::readRgba8(std::uint32_t x, std::uint32_t y,
                                        std::uint32_t width, std::uint32_t height,
                                        std::vector<std::uint8_t>& out) const {
    if (glGetIntegerv == nullptr) {
        return data::makeError<void>(1, "readRgba8: no GL context (not loaded)");
    }
    const std::size_t bytesPerPixel = 4u;
    // Overflow-safe total = width * height * 4 using 64-bit intermediate.
    const std::uint64_t total64 =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * bytesPerPixel;
    if (total64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return data::makeError<void>(2, "readRgba8: requested size overflows size_t");
    }
    const std::size_t total = static_cast<std::size_t>(total64);
    // Additional check: if width or height !=0, total / (width*4) should equal height.
    if (width != 0u && height != 0u) {
        if (total / bytesPerPixel / width != static_cast<std::size_t>(height)) {
            return data::makeError<void>(2, "readRgba8: requested size overflows size_t");
        }
    }
    out.resize(total);
    // VG4: save/restore PACK_ALIGNMENT (caller may rely on default 4).
    GLint oldAlign = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &oldAlign);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(static_cast<GLint>(x), static_cast<GLint>(y),
                 static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                 GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    glPixelStorei(GL_PACK_ALIGNMENT, oldAlign);
    return data::Result<void>(data::value);
}

data::Result<void> REContext::readBufferSubData(BufferTarget target,
                                                std::uint32_t bufferId,
                                                std::size_t byteOffset,
                                                std::size_t byteSize,
                                                void* out) const {
    if (glGetBufferSubData == nullptr || glBindBuffer == nullptr) {
        return data::makeError<void>(1, "readBufferSubData: no GL context (not loaded)");
    }
    GLenum glTarget = 0u;
    switch (target) {
        case BufferTarget::ShaderStorage:
            glTarget = GL_SHADER_STORAGE_BUFFER;
            break;
        case BufferTarget::TransformFeedback:
            glTarget = GL_TRANSFORM_FEEDBACK_BUFFER;
            break;
    }
    glBindBuffer(glTarget, bufferId);
    glGetBufferSubData(glTarget, static_cast<GLintptr>(byteOffset),
                       static_cast<GLsizeiptr>(byteSize), out);
    glBindBuffer(glTarget, 0u);
    return data::Result<void>(data::value);
}

// Also provide fallback for old global invalidate etc. (already aliased in header)

} // namespace re::core
