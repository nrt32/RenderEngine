// data/volume_dataset.cpp — VolumeDataset container implementation
// (FR-data.3).

#include "data/volume_dataset.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace re::data {

VolumeDataset::VolumeDataset(std::uint32_t sizeX, std::uint32_t sizeY,
                             std::uint32_t sizeZ, std::vector<float> voxels)
    : sizeX_(sizeX),
      sizeY_(sizeY),
      sizeZ_(sizeZ),
      voxelCount_(static_cast<std::uint64_t>(sizeX) *
                  static_cast<std::uint64_t>(sizeY) *
                  static_cast<std::uint64_t>(sizeZ)),
      voxels_(std::move(voxels)) {}

float VolumeDataset::voxelAt(std::uint32_t x, std::uint32_t y,
                             std::uint32_t z) const noexcept {
    const std::uint64_t index = static_cast<std::uint64_t>(x) +
                                static_cast<std::uint64_t>(sizeX_) *
                                    (static_cast<std::uint64_t>(y) +
                                     static_cast<std::uint64_t>(sizeY_) *
                                         static_cast<std::uint64_t>(z));
    return voxels_[index];
}

float VolumeDataset::sampleTrilinear(float x, float y, float z) const noexcept {
    // Clamp continuous index coordinates into [0, dim-1] per axis (a lattice
    // coordinate beyond the last center clamps to the last center, fx = 0).
    const float cx = std::clamp(x, 0.0f, static_cast<float>(sizeX_ - 1u));
    const float cy = std::clamp(y, 0.0f, static_cast<float>(sizeY_ - 1u));
    const float cz = std::clamp(z, 0.0f, static_cast<float>(sizeZ_ - 1u));

    // Voxel-center lattice indices around the sample; x1 = x0 when cx sits on
    // the last center (fx = 0 then, so x0 == x1 is harmless).
    const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(cx));
    const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(cy));
    const std::uint32_t z0 = static_cast<std::uint32_t>(std::floor(cz));
    const std::uint32_t x1 = std::min(x0 + 1u, sizeX_ - 1u);
    const std::uint32_t y1 = std::min(y0 + 1u, sizeY_ - 1u);
    const std::uint32_t z1 = std::min(z0 + 1u, sizeZ_ - 1u);

    const float fx = cx - static_cast<float>(x0);
    const float fy = cy - static_cast<float>(y0);
    const float fz = cz - static_cast<float>(z0);

    // The 8 corners of the unit cell (closed-form trilinear weights).
    const float v000 = voxelAt(x0, y0, z0);
    const float v100 = voxelAt(x1, y0, z0);
    const float v010 = voxelAt(x0, y1, z0);
    const float v110 = voxelAt(x1, y1, z0);
    const float v001 = voxelAt(x0, y0, z1);
    const float v101 = voxelAt(x1, y0, z1);
    const float v011 = voxelAt(x0, y1, z1);
    const float v111 = voxelAt(x1, y1, z1);

    const float w000 = (1.0f - fx) * (1.0f - fy) * (1.0f - fz);
    const float w100 = fx * (1.0f - fy) * (1.0f - fz);
    const float w010 = (1.0f - fx) * fy * (1.0f - fz);
    const float w110 = fx * fy * (1.0f - fz);
    const float w001 = (1.0f - fx) * (1.0f - fy) * fz;
    const float w101 = fx * (1.0f - fy) * fz;
    const float w011 = (1.0f - fx) * fy * fz;
    const float w111 = fx * fy * fz;

    return w000 * v000 + w100 * v100 + w010 * v010 + w110 * v110 + w001 * v001 +
           w101 * v101 + w011 * v011 + w111 * v111;
}

} // namespace re::data
