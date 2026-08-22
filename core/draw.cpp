// core/draw.cpp — thin draw/state API implementation (SPEC §3).
//
// This module is the SOLE owner of the raw draw-state GL calls
// (glViewport / glClearColor / glClear / glEnable / glDisable /
// glDrawElements / glBlitFramebuffer). render/, app/, and tests/ consume GL
// only through these wrappers (guardrail gpu_api_ownership).
//
// SPEC §9 V2.10: internal dirty-flag cache for setViewport / setClearColor /
// enable* / disable* — redundant calls are skipped and do not issue a raw
// gl* call. A test-injectable spy records how many times each wrapper
// actually issued its gl* call.

#include "core/draw.hpp"

#include <glad/gl.h>

#include <cstddef>
#include <cstdint>

namespace re::core {

namespace {

// ---------------------------------------------------------------------------
// Dirty-flag cache (internal, not exposed beyond the spy API).
// ---------------------------------------------------------------------------
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

Cache g_cache;

// ---------------------------------------------------------------------------
// Spy counters — incremented only when the wrapper actually issues the gl*.
// ---------------------------------------------------------------------------
DrawSpyCounts g_spy;

} // namespace

void setViewport(int x, int y, int width, int height) noexcept {
    if (g_cache.hasViewport && g_cache.vpX == x && g_cache.vpY == y &&
        g_cache.vpW == width && g_cache.vpH == height) {
        return;
    }
    g_cache.hasViewport = true;
    g_cache.vpX = x;
    g_cache.vpY = y;
    g_cache.vpW = width;
    g_cache.vpH = height;
    ++g_spy.viewport;
    glViewport(x, y, width, height);
}

void setClearColor(float r, float g, float b, float a) noexcept {
    if (g_cache.hasClearColor && g_cache.ccR == r && g_cache.ccG == g &&
        g_cache.ccB == b && g_cache.ccA == a) {
        return;
    }
    g_cache.hasClearColor = true;
    g_cache.ccR = r;
    g_cache.ccG = g;
    g_cache.ccB = b;
    g_cache.ccA = a;
    ++g_spy.clearColor;
    glClearColor(r, g, b, a);
}

void clearColor() noexcept {
    glClear(GL_COLOR_BUFFER_BIT);
}

void bindDefaultFramebuffer() noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void enableDepthTest() noexcept {
    if (g_cache.hasDepthTest && g_cache.depthEnabled) {
        return;
    }
    g_cache.hasDepthTest = true;
    g_cache.depthEnabled = true;
    ++g_spy.enableDepthTest;
    glEnable(GL_DEPTH_TEST);
}

void disableDepthTest() noexcept {
    if (g_cache.hasDepthTest && !g_cache.depthEnabled) {
        return;
    }
    g_cache.hasDepthTest = true;
    g_cache.depthEnabled = false;
    ++g_spy.disableDepthTest;
    glDisable(GL_DEPTH_TEST);
}

void enableBlend() noexcept {
    if (g_cache.hasBlend && g_cache.blendEnabled) {
        return;
    }
    g_cache.hasBlend = true;
    g_cache.blendEnabled = true;
    ++g_spy.enableBlend;
    glEnable(GL_BLEND);
}

void disableBlend() noexcept {
    if (g_cache.hasBlend && !g_cache.blendEnabled) {
        return;
    }
    g_cache.hasBlend = true;
    g_cache.blendEnabled = false;
    ++g_spy.disableBlend;
    glDisable(GL_BLEND);
}

data::Result<void> drawElements(const VertexArray& vao,
                                 std::size_t indexCount) {
    if (glDrawElements == nullptr) {
        return data::makeError<void>(
            1, "drawElements: no GL context (glDrawElements not loaded)");
    }
    vao.bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount),
                   GL_UNSIGNED_INT, nullptr);
    return data::Result<void>(data::value);
}

void memoryBarrierShaderStorage() noexcept {
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                    GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void memoryBarrierBufferUpdate() noexcept {
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
}

void bindImageR32ui(const Texture2D& texture, std::uint32_t unit) noexcept {
    glBindImageTexture(static_cast<GLuint>(unit), texture.id(), 0, GL_FALSE, 0,
                       GL_READ_WRITE, GL_R32UI);
}

void unbindImage(std::uint32_t unit) noexcept {
    glBindImageTexture(static_cast<GLuint>(unit), 0u, 0, GL_FALSE, 0,
                       GL_READ_WRITE, GL_R32UI);
}

void enablePremultipliedOverBlend() noexcept {
    const bool needEnable = !(g_cache.hasBlend && g_cache.blendEnabled);
    const bool needFunc = !(g_cache.hasBlendFunc &&
                            g_cache.blendSrc == GL_ONE &&
                            g_cache.blendDst == GL_ONE_MINUS_SRC_ALPHA);
    if (!needEnable && !needFunc) {
        return;
    }
    if (needEnable) {
        g_cache.hasBlend = true;
        g_cache.blendEnabled = true;
        ++g_spy.enableBlend;
        glEnable(GL_BLEND);
    }
    if (needFunc) {
        g_cache.hasBlendFunc = true;
        g_cache.blendSrc = GL_ONE;
        g_cache.blendDst = GL_ONE_MINUS_SRC_ALPHA;
        ++g_spy.blendFunc;
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
}

data::Result<void> blit(const Framebuffer& source, int srcX, int srcY,
                         int srcWidth, int srcHeight,
                         const Framebuffer* destination, int dstX, int dstY,
                         int dstWidth, int dstHeight) {
    if (glBlitFramebuffer == nullptr) {
        return data::makeError<void>(
            1, "blit: no GL context (glBlitFramebuffer not loaded)");
    }
    // Bind the read and draw framebuffer targets explicitly: the source is
    // always an FBO (v1 never blits from the default framebuffer), the
    // destination is either an FBO or the window's default framebuffer (0).
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source.id());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                      destination == nullptr ? 0u : destination->id());
    glBlitFramebuffer(srcX, srcY, srcX + srcWidth, srcY + srcHeight, dstX, dstY,
                      dstX + dstWidth, dstY + dstHeight, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    return data::Result<void>(data::value);
}

// ---------------------------------------------------------------------------
// Spy / cache control (SPEC §9 V2.10).
// ---------------------------------------------------------------------------

DrawSpyCounts getDrawSpyCounts() noexcept {
    return g_spy;
}

void resetDrawSpyCounts() noexcept {
    g_spy = DrawSpyCounts{};
}

void invalidateDrawCache() noexcept {
    g_cache = Cache{};
    g_spy = DrawSpyCounts{};
}

} // namespace re::core
