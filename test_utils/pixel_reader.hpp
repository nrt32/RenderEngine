#pragma once

// test_utils/pixel_reader.hpp — framebuffer pixel readback for test verification
// (T18 test-support extraction — keeps RE critical code lean).
//
// Guardrail no_production_readback: the raw readback call lives ONLY under
// core/ (core::REContext::readRgba8 in core/re_context.cpp, SPEC §6). This
// class is the test_utils/ façade tests consume; it delegates to the core/
// REContext anchor via REContext::current().readRgba8 so higher layers never
// touch raw GL. test_utils::PixelReader calls REContext::current().readRgba8
// (not a second raw anchor) and contains no raw GL string.
//
// This façade is the peer-lib counterpart of the former utils::PixelReader
// (V2.1) — utils/offscreen_context.* stays in utils/ (T15 GlfwRuntime owns
// the offscreen lifetime, T18 owns only PixelReader/read_pixels/capture
// helpers). Every context-setting GL call still flows through T2 REContext;
// no test helper touches raw GL.

#include <cstdint>
#include <vector>

#include "data/result.hpp"

namespace re::test_utils {

/// Reads RGBA8 pixels from the currently-bound read framebuffer (test support).
///
/// The raw readback call stays under core/ (core::REContext::readRgba8,
/// guardrail no_production_readback); this class is the test_utils/ façade
/// tests consume so that render/, app/ and tests/ never touch raw GL. It
/// delegates to REContext::current().readRgba8 (SPEC §6, T18).
class PixelReader {
   public:
    /// Read a `width` x `height` RGBA8 block from the currently-bound read
    /// framebuffer, starting at pixel (x, y), into `out`.
    ///
    /// `out` is resized to `width * height * 4` bytes; the layout is row-major,
    /// bottom-up (GL convention): pixel (px, py) sits at byte offset
    /// `((py * width) + px) * 4` in `out`, with bytes [R, G, B, A].
    ///
    /// Returns an error if no GL context is current (the raw entry point is
    /// not loaded) or if the requested size overflows. Delegates to the core/
    /// REContext anchor (SPEC §6, T18).
    data::Result<void> read(std::uint32_t x, std::uint32_t y,
                            std::uint32_t width, std::uint32_t height,
                            std::vector<std::uint8_t>& out) const;
};

} // namespace re::test_utils
