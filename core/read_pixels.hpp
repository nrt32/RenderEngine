#pragma once

// core/read_pixels.hpp — framebuffer pixel readback for test verification.
//
// Guardrail no_production_readback: raw readback calls (glReadPixels) live
// ONLY under core/, consumed by tests to verify rendered output (SPEC §6).
// This wrapper reads RGBA8 pixels from the currently-bound read framebuffer,
// so a test binds its target Framebuffer and then calls readRgba8.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "data/result.hpp"

namespace re::core {

/// Read a `width` x `height` block of RGBA8 pixels from the currently-bound
/// read framebuffer, starting at pixel (`x`, `y`), into `out`.
///
/// `out` is resized to `width * height * 4` bytes; the layout is row-major,
/// bottom-up (GL convention): row 0 is the bottom scanline. Pixel `(px, py)`
/// sits at byte offset `((py * width) + px) * 4` in `out`, with bytes
/// `[R, G, B, A]`.
///
/// Returns an error if no GL context is current (glReadPixels not loaded) or
/// if the requested size overflows.
data::Result<void> readRgba8(std::uint32_t x, std::uint32_t y,
                             std::uint32_t width, std::uint32_t height,
                             std::vector<std::uint8_t>& out);

} // namespace re::core
