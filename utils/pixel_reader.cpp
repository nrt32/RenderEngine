// utils/pixel_reader.cpp — pixel readback facade (test-support, SPEC §6
// guardrail no_production_readback: the raw readback call stays under core/,
// this file only delegates to it).

#include "utils/pixel_reader.hpp"

#include "core/read_pixels.hpp"

namespace re::utils {

data::Result<void> PixelReader::read(std::uint32_t x, std::uint32_t y,
                                     std::uint32_t width, std::uint32_t height,
                                     std::vector<std::uint8_t>& out) const {
    return core::readRgba8(x, y, width, height, out);
}

} // namespace re::utils
