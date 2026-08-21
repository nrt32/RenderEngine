// core/read_pixels.cpp — framebuffer pixel readback (test-consumed, SPEC §6
// guardrail no_production_readback: raw glReadPixels lives here, under core/).

#include "core/read_pixels.hpp"

#include <glad/gl.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace re::core {

data::Result<void> readRgba8(std::uint32_t x, std::uint32_t y,
                             std::uint32_t width, std::uint32_t height,
                             std::vector<std::uint8_t>& out) {
    if (glReadPixels == nullptr) {
        return data::makeError<void>(
            1, "readRgba8: no GL context (glReadPixels not loaded)");
    }
    const std::size_t bytesPerPixel = 4u;
    const std::size_t total = static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height) * bytesPerPixel;
    out.resize(total);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(static_cast<GLint>(x), static_cast<GLint>(y),
                 static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                 GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    return data::Result<void>(data::value);
}

} // namespace re::core
