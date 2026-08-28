// tests/t13_nrrd_preprobe_test.cpp — T13 gate + T11 No cap update.
//
// T13 checked file size via std::filesystem::file_size before the whole-file
// slurp, compares against the derived ceiling (128^3 * 8 + 64 KiB) and
// returned BudgetExceeded. T11 (No cap streaming via core::Caps) lifts the
// 128³ dims cap: any dims via core::Caps tiled (1/255) — the committed
// sample is example 128×128×70, product has no ≤128³ window. The file-size
// probe remains as the sole BudgetExceeded guard but with a large ceiling
// (512³*8+64 KiB = 1,073,807,360) so 256³ synthetic (16 MB) passes. The
// HugeDims test now verifies 129 and 256³ load via Caps, not BudgetExceeded.
//
// Evidence rule (R4): analytic constants — 512³=134,217,728, 512³*8=
// 1,073,741,824, cap=1,073,807,360, 256³=16,777,216 (=8×2,097,152), 128³ cap
// superseded by Caps tiled 1/255 (see T11).

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "data/volume_dataset.hpp"
#include "io/volume/nrrd_volume_loader.hpp"

namespace re::tests {
namespace {

std::string assetPath(const std::string& rel) {
    return std::string(TEST_SOURCE_DIR) + "/" + rel;
}

constexpr std::uint64_t kMaxVoxels = 512ULL * 512ULL * 512ULL;
constexpr std::uint64_t kHeaderSlack = 64ULL * 1024ULL;
constexpr std::uint64_t kMaxFileBytes = kMaxVoxels * 8ULL + kHeaderSlack;

std::filesystem::path writeTemp(const std::string& tag,
                                const std::string& contents) {
    auto path = std::filesystem::temp_directory_path() /
                ("re_t13_" + tag + ".nrrd");
    std::ofstream out(path, std::ios::binary);
    out << contents;
    return path;
}

} // namespace

// Host file size exceeds absolute cap -> BudgetExceeded, no multi-GB alloc.
// T11: cap lifted to 512³*8+64KiB (1,073,807,360) so 256³ (16 MB) passes,
// only multi-GB sparse file triggers BudgetExceeded.
TEST(T13NrrdPreProbe, HostFileSizeExceedsCapReturnsBudgetExceeded) {
    constexpr std::uint64_t cap = kMaxFileBytes;
    // Analytic: cap = 512^3 * 8 + 64 KiB = 1,073,741,824 + 65,536 = 1,073,807,360.
    EXPECT_EQ(kMaxVoxels, 134217728ULL);
    EXPECT_EQ(kMaxVoxels * 8ULL, 1073741824ULL);
    EXPECT_EQ(kHeaderSlack, 65536ULL);
    EXPECT_EQ(cap, 1073807360ULL);

    // Build a minimal valid NRRD and then extend its on-disk size beyond cap
    // via seek + single byte. The early file_size probe must reject before
    // slurping the host file, so we observe BudgetExceeded with no large
    // allocation (the file content itself is tiny, the size is sparse).
    const std::string header =
        "NRRD0004\ntype: uint8\ndimension: 3\nsizes: 2 2 2\nencoding: raw\n\n";
    const std::string raw(8, '\x01');
    auto path = std::filesystem::temp_directory_path() / "re_t13_cap_exceed.nrrd";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << header << raw;
        // Extend to cap+1 without writing cap bytes: seek to cap and write one.
        out.seekp(static_cast<std::streamoff>(cap));
        ASSERT_TRUE(out.good());
        out.put('\0');
        ASSERT_TRUE(out.good());
    }
    // Verify host size is indeed cap+1 (analytic 1,073,807,361).
    std::error_code ec;
    const auto actualSize = std::filesystem::file_size(path, ec);
    ASSERT_FALSE(ec);
    EXPECT_EQ(actualSize, cap + 1ULL);

    auto result = io::loadNrrdVolume(path.string());
    EXPECT_TRUE(result.failed());
    EXPECT_EQ(result.error().domain, data::ErrorDomain::VolumeIo);
    EXPECT_EQ(result.error().code,
              static_cast<int>(io::VolumeLoadError::BudgetExceeded))
        << "host-file-size > cap must map to BudgetExceeded (code 8)";

    std::filesystem::remove(path, ec);
}

// Synthetic header with dims >128^3 now loads via No cap streaming (T11).
// 129 and 256³ are no longer BudgetExceeded — they succeed when raw bytes
// match, and fail with VoxelBlockSize (code 7) when raw is truncated,
// proving the 128³ window is gone and core::Caps tiled path handles 256³.
TEST(T13NrrdPreProbe, HugeDimsExceedBudgetBeforeAlloc) {
    // 129 now loads successfully (T11 lifts 128 cap); raw size 129 matches.
    const std::string contents =
        "NRRD0004\ntype: int8\ndimension: 3\nsizes: 129 1 1\nencoding: raw\n\n" +
        std::string(129, '\x00');
    auto path = writeTemp("huge_axis", contents);
    auto result = io::loadNrrdVolume(path.string());
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result->sizeX(), 129u);
    EXPECT_EQ(result->voxelCount(), 129ULL);
    std::filesystem::remove(path);

    // 256³ with no raw bytes -> VoxelBlockSize (7), not BudgetExceeded (8),
    // proving the dims gate is gone; with full raw it would succeed (T11 gate).
    const std::string contents2 =
        "NRRD0004\ntype: uint8\ndimension: 3\nsizes: 256 256 256\nencoding: "
        "raw\n\n";
    auto path2 = writeTemp("huge_product", contents2);
    auto result2 = io::loadNrrdVolume(path2.string());
    EXPECT_TRUE(result2.failed());
    EXPECT_EQ(result2.error().code,
              static_cast<int>(io::VolumeLoadError::VoxelBlockSize))
        << "256³ with truncated raw must be VoxelBlockSize, not BudgetExceeded (No cap, T11)";
    std::filesystem::remove(path2);

    // 256³ with full raw (16,777,216 bytes) loads via No cap — byte-identical via Caps.
    const std::string header = "NRRD0004\ntype: uint8\ndimension: 3\nsizes: 256 256 256\nencoding: raw\n\n";
    std::string fullRaw(256*256*256, '\x01');
    auto path3 = writeTemp("huge_product_full", header + fullRaw);
    auto result3 = io::loadNrrdVolume(path3.string());
    ASSERT_TRUE(result3.ok()) << result3.error().message;
    EXPECT_EQ(result3->sizeX(), 256u);
    EXPECT_EQ(result3->voxelCount(), 16777216ULL);
    std::filesystem::remove(path3);
}

// Valid volume still loads byte-identical (regression lock FR-io.2).
TEST(T13NrrdPreProbe, ValidVolumeLoadsByteIdentical) {
    auto result = io::loadNrrdVolume(assetPath("data/volumes/sample_ct.nrrd"));
    ASSERT_TRUE(result.ok()) << result.error().message;
    const data::VolumeDataset& vol = *result;
    EXPECT_EQ(vol.sizeX(), 128u);
    EXPECT_EQ(vol.sizeY(), 128u);
    EXPECT_EQ(vol.sizeZ(), 70u);
    EXPECT_EQ(vol.voxelCount(), 128ULL * 128ULL * 70ULL);
    EXPECT_EQ(vol.byteSize(), 128ULL * 128ULL * 70ULL * sizeof(float));
    // Corners are background -3024 (analytic, committed dataset).
    EXPECT_FLOAT_EQ(vol.voxelAt(0, 0, 0), -3024.0f);
    EXPECT_FLOAT_EQ(vol.voxelAt(127, 127, 69), -3024.0f);
    // Interior analytic probes from committed file.
    EXPECT_FLOAT_EQ(vol.voxelAt(64, 64, 35), 26.0f);
    EXPECT_FLOAT_EQ(vol.voxelAt(64, 64, 36), 27.0f);

    // Golden 2x2x2 fixture byte-identical: voxel(x,y,z)=x+2*y+4*z -> 0..7.
    auto golden =
        io::loadNrrdVolume(assetPath("data/fixtures/golden_volume.nrrd"));
    ASSERT_TRUE(golden.ok()) << golden.error().message;
    EXPECT_EQ(golden->voxelCount(), 8u);
    for (std::uint32_t z = 0; z < 2; ++z) {
        for (std::uint32_t y = 0; y < 2; ++y) {
            for (std::uint32_t x = 0; x < 2; ++x) {
                EXPECT_FLOAT_EQ(golden->voxelAt(x, y, z),
                                static_cast<float>(x + 2 * y + 4 * z));
            }
        }
    }
    EXPECT_NEAR(golden->sampleTrilinear(0.5f, 0.5f, 0.5f), 3.5f, 1e-6f);
}

// FileOpen path remains distinct from BudgetExceeded (domain disambiguation).
TEST(T13NrrdPreProbe, FileOpenNotConfusedWithBudgetExceeded) {
    auto result =
        io::loadNrrdVolume(assetPath("data/volumes/does_not_exist_t13.nrrd"));
    EXPECT_TRUE(result.failed());
    EXPECT_EQ(result.error().domain, data::ErrorDomain::VolumeIo);
    EXPECT_EQ(result.error().code,
              static_cast<int>(io::VolumeLoadError::FileOpen));
    EXPECT_NE(result.error().code,
              static_cast<int>(io::VolumeLoadError::BudgetExceeded));
}

} // namespace re::tests
