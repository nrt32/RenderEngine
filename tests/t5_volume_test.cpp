// tests/t5_volume_test.cpp — T5 gate tests (io/ NRRD loader + data/
// VolumeDataset).
//
// Asserts the io/ + data/ volume requirements from TASKS.md T5:
//   (1) the committed sample_ct.nrrd loads with expected dims <=128^3 and
//       matching voxel values at indexed corners (FR-io.2);
//   (2) an interior sample equals the closed-form trilinear interpolant of the
//       8 corner values within 1e-6 (FR-data.3);
//   (3) malformed NRRD returns a typed error, no partial state (FR-io.4);
//   (4) memory stays within the v1 budget cap (<= 128^3).
//
// All acceptance constants are hand-counted from the committed golden files
// and recorded in data/README.md and docs/io-data.md:
//   - sample_ct.nrrd: 128 x 128 x 70 int32 (values in [-3024, 2529], so
//     int32 -> float32 is exact); the 8 indexed corners all hold the
//     background fill value -3024; interior indexed voxels are read directly
//     from the committed file (indexing proof);
//   - golden_volume.nrrd: 2x2x2 int16 with voxel(x,y,z) = x + 2*y + 4*z
//     (values 0..7), x-fastest;
//   - v1 budget cap (SPEC S5): every axis <= 128, voxel count <= 128^3
//     (= 2,097,152), float32 storage <= 128^3 * 4 bytes (= 8,388,608).

#include <gtest/gtest.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "data/volume_dataset.hpp"
#include "io/volume/nrrd_volume_loader.hpp"

namespace re::tests {
namespace {

// Repo-root-relative path resolution (tests run from the build dir).
std::string assetPath(const std::string& rel) {
    return std::string(TEST_SOURCE_DIR) + "/" + rel;
}

// Write binary `contents` to a uniquely-named scratch file under the system
// temp dir and return its path (used for malformed-input fixtures, FR-io.4).
std::filesystem::path writeTempNrrd(const std::string& contents) {
    auto path = std::filesystem::temp_directory_path() /
                ("re_t5_fixture_" + std::to_string(::getpid()) + ".nrrd");
    std::ofstream out(path, std::ios::binary);
    out << contents;
    return path;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) FR-io.2 — sample_ct.nrrd loads with expected dims + corner voxel values.
// ---------------------------------------------------------------------------
TEST(T5Volume, SampleCtNrrdLoadsWithExpectedDimsAndCorners) {
    auto result = io::loadNrrdVolume(assetPath("data/volumes/sample_ct.nrrd"));
    ASSERT_TRUE(result.ok()) << result.error().message;

    const data::VolumeDataset& volume = *result;
    // SPEC S7 / data/README.md: the downsampled CT is 128 x 128 x 70.
    EXPECT_EQ(volume.sizeX(), 128u);
    EXPECT_EQ(volume.sizeY(), 128u);
    EXPECT_EQ(volume.sizeZ(), 70u);

    // Voxel count and float32 storage size (exact, deterministic).
    EXPECT_EQ(volume.voxelCount(), 128u * 128u * 70u);
    EXPECT_EQ(volume.byteSize(), 128u * 128u * 70u * sizeof(float));

    // Indexed corners (x-fastest: index = x + 128*y + 128*128*z): the corners
    // lie outside the patient's body and all hold the dataset's background
    // fill value -3024 (read from the committed golden file).
    const std::uint32_t corners[][3] = {
        {0, 0, 0},  {127, 0, 0},  {0, 127, 0},  {127, 127, 0},
        {0, 0, 69}, {127, 0, 69}, {0, 127, 69}, {127, 127, 69},
    };
    for (const auto& c : corners) {
        EXPECT_FLOAT_EQ(volume.voxelAt(c[0], c[1], c[2]), -3024.0f)
            << "corner (" << c[0] << "," << c[1] << "," << c[2] << ")";
    }

    // Interior indexed voxels (values read from the committed file): prove the
    // byte decoding and the x-fastest indexing, which the (uniform) corner
    // values alone cannot.
    struct Voxel {
        std::uint32_t x, y, z;
        float value;
    };
    const Voxel voxels[] = {
        {64, 64, 35, 26.0f},   {64, 64, 36, 27.0f},   {63, 64, 35, 28.0f},
        {64, 63, 35, 29.0f},   {64, 64, 34, 30.0f},   {32, 80, 20, 31.0f},
        {96, 48, 50, -885.0f}, {20, 30, 10, -949.0f}, {100, 100, 60, -910.0f},
    };
    for (const auto& v : voxels) {
        EXPECT_FLOAT_EQ(volume.voxelAt(v.x, v.y, v.z), v.value)
            << "voxel (" << v.x << "," << v.y << "," << v.z << ")";
    }

    // The continuous sampler reproduces lattice points exactly: sampling at
    // integer coordinates returns the voxel value (weights collapse to one).
    for (const auto& v : voxels) {
        EXPECT_NEAR(volume.sampleTrilinear(static_cast<float>(v.x),
                                           static_cast<float>(v.y),
                                           static_cast<float>(v.z)),
                    v.value, 1e-6f)
            << "sampleTrilinear at lattice (" << v.x << "," << v.y << "," << v.z
            << ")";
    }
}

// ---------------------------------------------------------------------------
// (2) FR-data.3 — interior sample equals the closed-form trilinear
//     interpolant of the 8 corner values, within 1e-6.
// ---------------------------------------------------------------------------
TEST(T5Volume, TrilinearSampleMatchesClosedFormInterpolant) {
    auto result =
        io::loadNrrdVolume(assetPath("data/fixtures/golden_volume.nrrd"));
    ASSERT_TRUE(result.ok()) << result.error().message;

    const data::VolumeDataset& volume = *result;
    EXPECT_EQ(volume.sizeX(), 2u);
    EXPECT_EQ(volume.sizeY(), 2u);
    EXPECT_EQ(volume.sizeZ(), 2u);
    EXPECT_EQ(volume.voxelCount(), 8u);
    EXPECT_EQ(volume.byteSize(), 8u * sizeof(float));

    // Closed form (data/README.md): voxel(x,y,z) = x + 2*y + 4*z, so the 8
    // values are 0..7 in x-fastest order.
    for (std::uint32_t z = 0; z < 2; ++z) {
        for (std::uint32_t y = 0; y < 2; ++y) {
            for (std::uint32_t x = 0; x < 2; ++x) {
                const float expected = static_cast<float>(x + 2 * y + 4 * z);
                EXPECT_FLOAT_EQ(volume.voxelAt(x, y, z), expected)
                    << "voxel (" << x << "," << y << "," << z << ")";
            }
        }
    }

    // v(x,y,z) = x + 2y + 4z is multilinear on the unit cell, so the trilinear
    // interpolant of the 8 corner values equals v itself everywhere. Hand-
    // computed closed forms at interior points (index space):
    //   (0.50, 0.50, 0.50) -> 0.5 + 2*0.5 + 4*0.5 = 3.5
    //   (0.25, 0.75, 0.50) -> 0.25 + 2*0.75 + 4*0.5 = 3.75
    //   (0.70, 0.20, 0.90) -> 0.7 + 2*0.2 + 4*0.9 = 4.7
    EXPECT_NEAR(volume.sampleTrilinear(0.5f, 0.5f, 0.5f), 3.5f, 1e-6f);
    EXPECT_NEAR(volume.sampleTrilinear(0.25f, 0.75f, 0.5f), 3.75f, 1e-6f);
    EXPECT_NEAR(volume.sampleTrilinear(0.7f, 0.2f, 0.9f), 4.7f, 1e-6f);
}

// FR-data.3, direct construction: a NON-multilinear corner field where the
// interpolant is a genuine weighted average. Corner values
// c(x,y,z) = (x+1)(y+2)(z+3) on the 2x2x2 cell; the closed-form interpolant
// at (0.25, 0.75, 0.5) is hand-computed below.
TEST(T5Volume, TrilinearSampleWeightsEightCorners) {
    // x-fastest order: c000=6, c100=12, c010=9, c110=18, c001=8, c101=16,
    // c011=12, c111=24.
    data::VolumeDataset volume(
        2, 2, 2, {6.0f, 12.0f, 9.0f, 18.0f, 8.0f, 16.0f, 12.0f, 24.0f});

    // Closed-form weighted average at (0.25, 0.75, 0.5), weights
    // w000=(1-fx)(1-fy)(1-fz) ... w111=fx*fy*fz:
    //   = .09375*6 + .03125*12 + .28125*9 + .09375*18
    //   + .09375*8 + .03125*16 + .28125*12 + .09375*24
    //   = 12.03125
    EXPECT_NEAR(volume.sampleTrilinear(0.25f, 0.75f, 0.5f), 12.03125f, 1e-6f);

    // Any permutation of the weights that is not the 8-corner trilinear set
    // would change this value; a sample at a lattice point stays exact.
    EXPECT_NEAR(volume.sampleTrilinear(0.0f, 0.0f, 0.0f), 6.0f, 1e-6f);
    EXPECT_NEAR(volume.sampleTrilinear(1.0f, 1.0f, 1.0f), 24.0f, 1e-6f);

    // Out-of-range coordinates clamp to the nearest voxel center.
    EXPECT_NEAR(volume.sampleTrilinear(-0.5f, 0.5f, 0.5f),
                volume.sampleTrilinear(0.0f, 0.5f, 0.5f), 1e-6f);
    EXPECT_NEAR(volume.sampleTrilinear(2.5f, 0.5f, 0.5f),
                volume.sampleTrilinear(1.0f, 0.5f, 0.5f), 1e-6f);
}

// Big-endian decoding: the same golden volume byte-swapped and declared
// "endian: big" must decode to the identical values (endianness is part of the
// NRRD header; the committed files are little-endian).
TEST(T5Volume, BigEndianNrrdDecodesIdentically) {
    // int16 values 0..7, big-endian bytes.
    std::string contents =
        "NRRD0004\ntype: int16\ndimension: 3\nsizes: 2 2 2\nendian: "
        "big\nencoding: raw\n\n";
    for (int v = 0; v < 8; ++v) {
        contents.push_back(static_cast<char>(0));
        contents.push_back(static_cast<char>(v));
    }
    const auto path = writeTempNrrd(contents);
    auto result = io::loadNrrdVolume(path.string());
    ASSERT_TRUE(result.ok()) << result.error().message;
    std::filesystem::remove(path);

    const data::VolumeDataset& volume = *result;
    for (std::uint32_t z = 0; z < 2; ++z) {
        for (std::uint32_t y = 0; y < 2; ++y) {
            for (std::uint32_t x = 0; x < 2; ++x) {
                EXPECT_FLOAT_EQ(volume.voxelAt(x, y, z),
                                static_cast<float>(x + 2 * y + 4 * z));
            }
        }
    }
    EXPECT_NEAR(volume.sampleTrilinear(0.5f, 0.5f, 0.5f), 3.5f, 1e-6f);
}

// ---------------------------------------------------------------------------
// (3) FR-io.4 — malformed NRRD: typed error, no exception, no partial state.
// ---------------------------------------------------------------------------
TEST(T5Volume, MalformedNrrdReturnsTypedError) {
    using E = io::VolumeLoadError;
    const int fileOpen = static_cast<int>(E::FileOpen);
    const int badMagic = static_cast<int>(E::BadMagic);
    const int malformedHeader = static_cast<int>(E::MalformedHeader);
    const int unsupportedDimension = static_cast<int>(E::UnsupportedDimension);
    const int unsupportedType = static_cast<int>(E::UnsupportedType);
    const int unsupportedEncoding = static_cast<int>(E::UnsupportedEncoding);
    const int voxelBlockSize = static_cast<int>(E::VoxelBlockSize);
    const int budgetExceeded = static_cast<int>(E::BudgetExceeded);

    // (a) Nonexistent file -> FileOpen.
    {
        auto result =
            io::loadNrrdVolume(assetPath("data/volumes/does_not_exist.nrrd"));
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, fileOpen);
    }

    // (b) Bad magic -> BadMagic.
    {
        const auto path = writeTempNrrd("NOTANRRD\ntype: int16\n\n1234");
        auto result = io::loadNrrdVolume(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, badMagic);
        std::filesystem::remove(path);
    }

    // (c) Header not terminated by a blank line -> MalformedHeader.
    {
        const auto path = writeTempNrrd(
            "NRRD0004\ntype: int16\ndimension: 3\nsizes: 2 2 2\n");
        auto result = io::loadNrrdVolume(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, malformedHeader);
        std::filesystem::remove(path);
    }

    // (d) Missing required 'type' field -> MalformedHeader.
    {
        const auto path = writeTempNrrd(
            "NRRD0004\ndimension: 3\nsizes: 2 2 2\nencoding: raw\n\n1234");
        auto result = io::loadNrrdVolume(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, malformedHeader);
        std::filesystem::remove(path);
    }

    // (e) dimension != 3 -> UnsupportedDimension.
    {
        const auto path = writeTempNrrd(
            "NRRD0004\ntype: int16\ndimension: 2\nsizes: 2 2\nencoding: "
            "raw\n\n1234");
        auto result = io::loadNrrdVolume(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, unsupportedDimension);
        std::filesystem::remove(path);
    }

    // (f) 'sizes' with the wrong token count -> MalformedHeader.
    {
        const auto path = writeTempNrrd(
            "NRRD0004\ntype: int16\ndimension: 3\nsizes: 2 2\nencoding: "
            "raw\n\n1234");
        auto result = io::loadNrrdVolume(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, malformedHeader);
        std::filesystem::remove(path);
    }

    // (g) Unsupported type -> UnsupportedType.
    {
        const auto path = writeTempNrrd(
            "NRRD0004\ntype: complex\ndimension: 3\nsizes: 2 2 2\nencoding: "
            "raw\n\n1234");
        auto result = io::loadNrrdVolume(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, unsupportedType);
        std::filesystem::remove(path);
    }

    // (h) Non-raw encoding -> UnsupportedEncoding (v1 is raw-only, SPEC S7).
    {
        const auto path = writeTempNrrd(
            "NRRD0004\ntype: int16\ndimension: 3\nsizes: 2 2 2\nencoding: "
            "gzip\n\n1234");
        auto result = io::loadNrrdVolume(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, unsupportedEncoding);
        std::filesystem::remove(path);
    }

    // (i) Invalid endian value -> MalformedHeader.
    {
        const auto path = writeTempNrrd(
            "NRRD0004\ntype: int16\ndimension: 3\nsizes: 2 2 2\nendian: "
            "middle\nencoding: raw\n\n1234");
        auto result = io::loadNrrdVolume(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, malformedHeader);
        std::filesystem::remove(path);
    }

    // (j) Truncated raw block (10 of the required 16 bytes) -> VoxelBlockSize.
    {
        const std::string tenBytes(10, '\x01');
        const auto path = writeTempNrrd(
            "NRRD0004\ntype: int16\ndimension: 3\nsizes: 2 2 2\nencoding: "
            "raw\n\n" +
            tenBytes);
        auto result = io::loadNrrdVolume(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, voxelBlockSize);
        std::filesystem::remove(path);
    }

    // (k) Dims beyond the v1 budget cap (axis 129 > 128) -> BudgetExceeded.
    {
        const std::string oneTwentyNineBytes(129, '\x00');
        const auto path = writeTempNrrd(
            "NRRD0004\ntype: int8\ndimension: 3\nsizes: 129 1 1\nencoding: "
            "raw\n\n" +
            oneTwentyNineBytes);
        auto result = io::loadNrrdVolume(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, budgetExceeded);
        std::filesystem::remove(path);
    }

    // FR-io.4 "no exception escape": every loader call above (including all
    // the successful loads in the other tests) must not throw. Re-run a sample
    // of the malformed cases through ASSERT_NO_THROW to make it explicit.
    ASSERT_NO_THROW(
        io::loadNrrdVolume(assetPath("data/volumes/does_not_exist.nrrd")));
    {
        const auto path = writeTempNrrd("NOTANRRD\n\n1234");
        ASSERT_NO_THROW(io::loadNrrdVolume(path.string()));
        std::filesystem::remove(path);
    }
    {
        const auto path = writeTempNrrd(
            "NRRD0004\ntype: int16\ndimension: 3\nsizes: 2 2 2\nencoding: "
            "raw\n\n1234");
        ASSERT_NO_THROW(io::loadNrrdVolume(path.string()));
        std::filesystem::remove(path);
    }

    // "No partial state": the failed Result carries no VolumeDataset at all —
    // there is nothing to dereference, and the loader builds the dataset only
    // after the whole file validates.
    auto result =
        io::loadNrrdVolume(assetPath("data/volumes/does_not_exist.nrrd"));
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, fileOpen);
}

// ---------------------------------------------------------------------------
// (4) Memory stays within the v1 budget cap (<= 128^3, SPEC S5).
// ---------------------------------------------------------------------------
TEST(T5Volume, VolumeMemoryWithinBudgetCap) {
    auto result = io::loadNrrdVolume(assetPath("data/volumes/sample_ct.nrrd"));
    ASSERT_TRUE(result.ok()) << result.error().message;

    const data::VolumeDataset& volume = *result;
    // Budget constants (SPEC S5): 128^3 = 2,097,152 voxels;
    // float32 storage 128^3 * 4 = 8,388,608 bytes (8 MiB).
    constexpr std::uint64_t kBudgetVoxels = 128ULL * 128ULL * 128ULL;
    constexpr std::size_t kBudgetBytes = 8'388'608;

    EXPECT_LE(volume.sizeX(), 128u);
    EXPECT_LE(volume.sizeY(), 128u);
    EXPECT_LE(volume.sizeZ(), 128u);
    EXPECT_LE(volume.voxelCount(), kBudgetVoxels);
    EXPECT_LE(volume.byteSize(), kBudgetBytes);
    EXPECT_EQ(volume.voxelCount(), 128ULL * 128ULL * 70ULL);
    EXPECT_EQ(volume.byteSize(), 128u * 128u * 70u * sizeof(float));

    // The golden fixture is far below the cap (2x2x2).
    auto golden =
        io::loadNrrdVolume(assetPath("data/fixtures/golden_volume.nrrd"));
    ASSERT_TRUE(golden.ok()) << golden.error().message;
    EXPECT_EQ(golden->voxelCount(), 8u);
    EXPECT_LE(golden->voxelCount(), kBudgetVoxels);
    EXPECT_LE(golden->byteSize(), kBudgetBytes);
}

} // namespace re::tests
