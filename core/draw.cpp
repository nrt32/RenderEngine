// core/draw.cpp — thin draw/state API implementation (SPEC §3).
//
// This module is the SOLE owner of the raw draw-state GL calls
// (glViewport / glClearColor / glClear / glEnable / glDisable /
// glDrawElements / glBlitFramebuffer). render/, app/, and tests/ consume GL
// only through these wrappers (guardrail gpu_api_ownership).

#include "core/draw.hpp"

#include <glad/gl.h>

#include <cstddef>
#include <cstdint>

namespace re::core {

void setViewport(int x, int y, int width, int height) noexcept {
    glViewport(x, y, width, height);
}

void setClearColor(float r, float g, float b, float a) noexcept {
    glClearColor(r, g, b, a);
}

void clearColor() noexcept {
    glClear(GL_COLOR_BUFFER_BIT);
}

void bindDefaultFramebuffer() noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void enableDepthTest() noexcept {
    glEnable(GL_DEPTH_TEST);
}

void disableDepthTest() noexcept {
    glDisable(GL_DEPTH_TEST);
}

void enableBlend() noexcept {
    glEnable(GL_BLEND);
}

void disableBlend() noexcept {
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
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
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

} // namespace re::core
