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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace re::core {

namespace {

// Per-GLFWwindow* REContext map.
std::unordered_map<GLFWwindow*, std::unique_ptr<REContext>> g_windowMap;
std::mutex g_windowMutex;

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
}

void REContext::clearColor() noexcept { glClear(GL_COLOR_BUFFER_BIT); }
void REContext::clearDepth() noexcept { glClear(GL_DEPTH_BUFFER_BIT); }

void REContext::enableDepthTest() noexcept {
    if (cache_.hasDepthTest && cache_.depthEnabled) return;
    cache_.hasDepthTest = true;
    cache_.depthEnabled = true;
    ++spy_.enableDepthTest;
    glEnable(GL_DEPTH_TEST);
}
void REContext::disableDepthTest() noexcept {
    if (cache_.hasDepthTest && !cache_.depthEnabled) return;
    cache_.hasDepthTest = true;
    cache_.depthEnabled = false;
    ++spy_.disableDepthTest;
    glDisable(GL_DEPTH_TEST);
}

void REContext::enableBlend() noexcept {
    if (cache_.hasBlend && cache_.blendEnabled) return;
    cache_.hasBlend = true;
    cache_.blendEnabled = true;
    ++spy_.enableBlend;
    glEnable(GL_BLEND);
}
void REContext::disableBlend() noexcept {
    if (cache_.hasBlend && !cache_.blendEnabled) return;
    cache_.hasBlend = true;
    cache_.blendEnabled = false;
    ++spy_.disableBlend;
    glDisable(GL_BLEND);
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
    // No window has been set on this thread yet — use per-thread fallback.
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

// REContext::readRgba8 — test_utils façade via REContext (raw glReadPixels stays here, core/ only).
data::Result<void> REContext::readRgba8(std::uint32_t x, std::uint32_t y,
                                        std::uint32_t width, std::uint32_t height,
                                        std::vector<std::uint8_t>& out) const {
    if (glReadPixels == nullptr) {
        return data::makeError<void>(1, "readRgba8: no GL context (glReadPixels not loaded)");
    }
    const std::size_t bytesPerPixel = 4u;
    const std::size_t total = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * bytesPerPixel;
    out.resize(total);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(static_cast<GLint>(x), static_cast<GLint>(y),
                 static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                 GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    return data::Result<void>(data::value);
}

// Also provide fallback for old global invalidate etc. (already aliased in header)

} // namespace re::core
