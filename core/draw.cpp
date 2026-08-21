// core/draw.cpp — thin draw/state API implementation (SPEC §3).
//
// This module is the SOLE owner of the raw draw-state GL calls
// (glViewport / glClearColor / glClear / glEnable / glDisable /
// glDrawElements). render/, app/, and tests/ consume GL only through these
// wrappers (guardrail gpu_api_ownership).

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

} // namespace re::core
