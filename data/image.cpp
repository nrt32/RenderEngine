// data/image.cpp — Image container implementation (FR-io.3).

#include "data/image.hpp"

namespace re::data {

Image::Image(std::int32_t width, std::int32_t height, std::int32_t channels,
             std::vector<std::uint8_t> pixels)
    : width_(width),
      height_(height),
      channels_(channels),
      pixels_(std::move(pixels)) {}

std::uint8_t Image::pixel(std::int32_t x, std::int32_t y,
                          std::int32_t c) const noexcept {
    // Row-major, top-left origin: pixel (x, y) starts at byte
    // (y * width + x) * channels.
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
         static_cast<std::size_t>(x)) *
            static_cast<std::size_t>(channels_) +
        static_cast<std::size_t>(c);
    return pixels_[offset];
}

std::size_t Image::byteSize() const noexcept {
    return pixels_.size();
}

} // namespace re::data