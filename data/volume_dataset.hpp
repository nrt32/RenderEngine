#pragma once

// data/volume_dataset.hpp — CPU volumetric dataset container (SPEC §3,
// FR-data.3).
//
// data/ is GL-free: this container stores a 3D grid of scalar voxel values in
// row-major, x-fastest order (index = x + sx*y + sx*sy*z, exactly the layout
// the NRRD loader in io/volume/ produces) and provides deterministic sampling.
//
// Storage is a single std::vector<float>: the io/ loader converts the source
// scalar type to float32 at load time. For the v1 sample data this is exact —
// sample_ct.nrrd values are int32 in [-3024, 2529] (|v| < 2^24, so int32 ->
// float32 is lossless) and the golden fixtures are int16.
//
// Sampling convention (FR-data.3): sampleTrilinear(x, y, z) takes continuous
// *index* coordinates in [0, dim-1] per axis; voxel centers sit at integer
// coordinates and out-of-range coordinates are clamped to the nearest voxel
// center. At integer coordinates the interpolant reproduces the voxel value
// exactly (the weights collapse to a single 1.0).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace re::data {

/// CPU volumetric dataset: 3D scalar-voxel grid + trilinear sampling
/// (FR-data.3).
///
/// Built through its constructor from pre-validated data: the io/ loader
/// validates the NRRD header, the raw-block size and the v1 memory budget
/// before constructing a VolumeDataset, so a malformed file can never yield a
/// partially-built container (FR-io.4 "no partial state").
class VolumeDataset {
   public:
    /// Build a dataset with the given per-axis voxel counts and values.
    ///
    /// Preconditions (the io/ loader validates before calling):
    ///   - every size is >= 1;
    ///   - `voxels.size() == sizeX * sizeY * sizeZ` (x-fastest order:
    ///     index = x + sizeX*y + sizeX*sizeY*z);
    ///   - the product of the sizes fits in the memory budget (SPEC §5:
    ///     <= 128^3 for v1 sample data).
    VolumeDataset(std::uint32_t sizeX, std::uint32_t sizeY, std::uint32_t sizeZ,
                  std::vector<float> voxels);

    /// Voxel count along X.
    std::uint32_t sizeX() const noexcept {
        return sizeX_;
    }

    /// Voxel count along Y.
    std::uint32_t sizeY() const noexcept {
        return sizeY_;
    }

    /// Voxel count along Z.
    std::uint32_t sizeZ() const noexcept {
        return sizeZ_;
    }

    /// Total number of voxels (sizeX * sizeY * sizeZ).
    std::uint64_t voxelCount() const noexcept {
        return voxelCount_;
    }

    /// Size of the voxel storage in bytes (voxelCount * sizeof(float)).
    std::size_t byteSize() const noexcept {
        return voxels_.size() * sizeof(float);
    }

    /// The voxel values, x-fastest order (see class comment).
    const std::vector<float>& voxels() const noexcept {
        return voxels_;
    }

    /// Discrete accessor: the voxel value at integer index (x, y, z)
    /// (precondition: 0 <= x < sizeX(), 0 <= y < sizeY(), 0 <= z < sizeZ()).
    float voxelAt(std::uint32_t x, std::uint32_t y,
                  std::uint32_t z) const noexcept;

    /// Continuous trilinear sample at index coordinates (x, y, z)
    /// (FR-data.3): the weighted average of the 8 surrounding voxel centers
    /// with weights w000=(1-fx)(1-fy)(1-fz) ... w111=fx*fy*fz, where fx/fy/fz
    /// are the fractional parts of the clamped coordinates. Coordinates are
    /// clamped per axis into [0, dim-1], so lattice points reproduce their
    /// voxel value exactly.
    float sampleTrilinear(float x, float y, float z) const noexcept;

   private:
    std::uint32_t sizeX_{0};
    std::uint32_t sizeY_{0};
    std::uint32_t sizeZ_{0};
    std::uint64_t voxelCount_{0};
    std::vector<float> voxels_;
};

} // namespace re::data
