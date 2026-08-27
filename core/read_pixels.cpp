// core/read_pixels.cpp — framebuffer pixel readback (test-consumed, SPEC §6
// guardrail no_production_readback: raw glReadPixels anchor is sole in
// core/re_context.cpp (REContext::readRgba8); this free function is a thin
// façade delegating to REContext::current() so tests/utils keep a stable API
// while the raw GL stays in one place (T2 per-GL-context REContext, T18
// test_utils façade discipline)).

#include "core/read_pixels.hpp"

#include "core/re_context.hpp"

namespace re::core {

data::Result<void> readRgba8(std::uint32_t x, std::uint32_t y,
                             std::uint32_t width, std::uint32_t height,
                             std::vector<std::uint8_t>& out) {
    return REContext::current().readRgba8(x, y, width, height, out);
}

} // namespace re::core
