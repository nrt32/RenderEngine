// app/mpr_slice.cpp — MPR layout + slice-state scaffolding implementation
// (SPEC §3, FR-app.2).

#include "app/mpr_slice.hpp"

#include <cmath>
#include <vector>

namespace re::app {

namespace {

/// Convert a straight RGBA color in [0, 1] to RGBA8 bytes (round-half-up,
/// matching the render/ convention: byte = round(c * 255 + 0.5)).
std::uint8_t toByte(float v) noexcept {
    return static_cast<std::uint8_t>(std::round(v * 255.0f));
}

} // namespace

std::array<MprViewport, 4> mprViewports(int windowWidth, int windowHeight) {
    // Split the window into four equal quadrants (SPEC FR-app.2). The 2x2 grid
    // order is T (top-left), C (top-right), S (bottom-left), 3D (bottom-right).
    // GL pixel coordinates: y = 0 is the bottom scanline, so the top half is
    // y = height/2 and the bottom half is y = 0.
    const int halfW = windowWidth / 2;
    const int halfH = windowHeight / 2;
    std::array<MprViewport, 4> views;
    views[0] = MprViewport{0, halfH, halfW, halfH};      // T  (top-left)
    views[1] = MprViewport{halfW, halfH, halfW, halfH};  // C  (top-right)
    views[2] = MprViewport{0, 0, halfW, halfH};          // S  (bottom-left)
    views[3] = MprViewport{halfW, 0, halfW, halfH};      // 3D (bottom-right)
    return views;
}

data::Image makeSliceImage(const data::VolumeDataset& dataset,
                           const volume::TransferFunction& tf, MprAxis axis,
                           std::uint32_t index) {
    // Dimensions of the slice rectangle over the two free axes (SPEC FR-app.2).
    std::int32_t width = 0;
    std::int32_t height = 0;
    switch (axis) {
        case MprAxis::Transverse: // over (X, Y) at constant Z
            width = static_cast<std::int32_t>(dataset.sizeX());
            height = static_cast<std::int32_t>(dataset.sizeY());
            break;
        case MprAxis::Coronal: // over (X, Z) at constant Y
            width = static_cast<std::int32_t>(dataset.sizeX());
            height = static_cast<std::int32_t>(dataset.sizeZ());
            break;
        case MprAxis::Sagittal: // over (Y, Z) at constant X
            width = static_cast<std::int32_t>(dataset.sizeY());
            height = static_cast<std::int32_t>(dataset.sizeZ());
            break;
    }

    std::vector<std::uint8_t> pixels;
    pixels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::int32_t py = 0; py < height; ++py) {
        for (std::int32_t px = 0; px < width; ++px) {
            // Sample the voxel at the axis-specific coordinates (the free axes
            // map to the image's (x, y), the held axis is `index`).
            float density = 0.0f;
            switch (axis) {
                case MprAxis::Transverse:
                    density = dataset.voxelAt(static_cast<std::uint32_t>(px),
                                              static_cast<std::uint32_t>(py),
                                              index);
                    break;
                case MprAxis::Coronal:
                    density = dataset.voxelAt(static_cast<std::uint32_t>(px),
                                              index,
                                              static_cast<std::uint32_t>(py));
                    break;
                case MprAxis::Sagittal:
                    density = dataset.voxelAt(index,
                                              static_cast<std::uint32_t>(px),
                                              static_cast<std::uint32_t>(py));
                    break;
            }
            const volume::RgbaColor c = tf.sample(density);
            pixels.push_back(toByte(c.r));
            pixels.push_back(toByte(c.g));
            pixels.push_back(toByte(c.b));
            pixels.push_back(toByte(c.a));
        }
    }
    return data::Image(width, height, 4, std::move(pixels));
}

} // namespace re::app
