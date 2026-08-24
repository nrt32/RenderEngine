#pragma once

// data/image.hpp — CPU image container (SPEC §3, FR-io.3).
//
// data/ is GL-free: this container holds decoded pixel data only. Images are
// produced by the io/ image loader (stb) and consumed by core/ textures and
// render/ (PlaneRenderer, FR-render.5).
//
// Pixel layout: row-major with a top-left origin (stb's native convention),
// `channels` bytes per pixel. pixel(x, y, c) is the value of channel c of the
// pixel at column x, row y.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace re::data {

/// CPU image: dimensions + channel count + decoded pixel bytes (FR-io.3).
class Image {
   public:
    /// Build an image from decoded pixels. `pixels` must hold
    /// `width * height * channels` bytes, row-major top-left origin.
    Image(std::int32_t width, std::int32_t height, std::int32_t channels,
          std::vector<std::uint8_t> pixels);

    /// Width in pixels.
    std::int32_t width() const noexcept {
        return width_;
    }

    /// Height in pixels.
    std::int32_t height() const noexcept {
        return height_;
    }

    /// Bytes per pixel (e.g. 3 for RGB, 4 for RGBA).
    std::int32_t channels() const noexcept {
        return channels_;
    }

    /// The decoded pixel bytes (row-major, top-left origin).
    const std::vector<std::uint8_t>& pixels() const noexcept {
        return pixels_;
    }

    /// Value of channel `c` of the pixel at `(x, y)` (precondition: valid
    /// coordinates, i.e. `0 <= x < width()`, `0 <= y < height()`,
    /// `0 <= c < channels()`).
    std::uint8_t pixel(std::int32_t x, std::int32_t y,
                       std::int32_t c) const noexcept;

    /// Total size of the decoded pixel buffer in bytes
    /// (`width * height * channels`).
    std::size_t byteSize() const noexcept;

   private:
    std::int32_t width_{0};
    std::int32_t height_{0};
    std::int32_t channels_{0};
    std::vector<std::uint8_t> pixels_;
};

} // namespace re::data