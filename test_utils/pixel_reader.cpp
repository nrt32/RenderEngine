// test_utils/pixel_reader.cpp — pixel readback façade (test-support, SPEC §6
// guardrail no_production_readback: the raw readback call stays under core/
// via REContext::readRgba8, this file only delegates to it and touches no raw
// GL — every context-setting GL call still flows through T2 REContext).

#include "test_utils/pixel_reader.hpp"

#include "core/re_context.hpp"

namespace re::test_utils {

data::Result<void> PixelReader::read(std::uint32_t x, std::uint32_t y,
                                     std::uint32_t width, std::uint32_t height,
                                     std::vector<std::uint8_t>& out) const {
    return core::REContext::current().readRgba8(x, y, width, height, out);
}

} // namespace re::test_utils
