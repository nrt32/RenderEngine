#pragma once

// utils/pixel_reader.hpp — framebuffer pixel readback for test verification
// (moved from core/ by V2.1, SPEC §9: test-support lives in utils/).
//
// Guardrail no_production_readback: the raw pixel readback call lives ONLY
// under core/ (core::readRgba8, SPEC §6). This class is the utils/ facade tests
// consume; it delegates to the core/ raw-GL anchor. It reads RGBA8 pixels from
// the currently-bound read framebuffer, so a test binds its target
// Framebuffer and then asks the reader for a pixel block.

#include <cstdint>
#include <vector>

#include "data/result.hpp"

namespace re::utils {

/// Reads RGBA8 pixels from the currently-bound read framebuffer (test
/// support).
///
/// The raw readback call stays under core/ (core::readRgba8, guardrail
/// no_production_readback); this class is the utils/ facade tests consume so
/// that render/, app/ and tests/ never touch raw GL.
class PixelReader {
   public:
    /// Read a `width` x `height` RGBA8 block from the currently-bound read
    /// framebuffer, starting at pixel (x, y), into `out`.
    ///
    /// `out` is resized to `width * height * 4` bytes; the layout is row-major,
    /// bottom-up (GL convention): pixel (px, py) sits at byte offset
    /// `((py * width) + px) * 4` in `out`, with bytes [R, G, B, A].
    ///
    /// Returns an error if no GL context is current (the raw readback entry
    /// point is not loaded) or if the requested size overflows. Delegates to
    /// the core/ raw-GL anchor core::readRgba8 (SPEC §6).
    data::Result<void> read(std::uint32_t x, std::uint32_t y,
                            std::uint32_t width, std::uint32_t height,
                            std::vector<std::uint8_t>& out) const;
};

} // namespace re::utils
