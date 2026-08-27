// tests/t13_nrrd_preprobe_test.cpp — T13 gate: NRRD loader size pre-probe.
//
// T13 checks file size via std::filesystem::file_size before the whole-file
// slurp, compares against the derived ceiling (128^3 * max element width 8 +
// 64 KiB header slack) and the absolute cap, and returns typed
// BudgetExceeded with spdlog::warn on exceed, avoiding multi-gigabyte
// allocation. The loader must not allocate the hostile file, valid volumes
// must load byte-identical, and synthetic huge dims still fail with the same
// code.
//
// Evidence rule (R4): every expected value is an explainable analytic
// constant — 128^3 = 2,097,152, 128^3*8 = 16,777,216, cap = 16,777,216 + 65,536
// = 16,842,752, 129 exceeds max axis 128 by exactly 1, sample_ct 128x128x70
// with corner -3024, golden 2x2x2 voxels 0..7 via x+2*y+4*z.

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

constexpr std::uint64_t kMaxVoxels = 128ULL * 128ULL * 128ULL;
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
TEST(T13NrrdPreProbe, HostFileSizeExceedsCapReturnsBudgetExceeded) {
    constexpr std::uint64_t cap = kMaxFileBytes;
    // Analytic: cap = 128^3 * 8 + 64 KiB = 16,777,216 + 65,536 = 16,842,752.
    EXPECT_EQ(kMaxVoxels, 2097152ULL);
    EXPECT_EQ(kMaxVoxels * 8ULL, 16777216ULL);
    EXPECT_EQ(kHeaderSlack, 65536ULL);
    EXPECT_EQ(cap, 16842752ULL);

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
    // Verify host size is indeed cap+1 (analytic 16,842,753).
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

// Synthetic header with dims >128^3 (axis 129) -> BudgetExceeded, still OOM-safe.
TEST(T13NrrdPreProbe, HugeDimsExceedBudgetBeforeAlloc) {
    // 129 exceeds max axis 128 by exactly 1; voxelCount 129*1*1 =129 <=2M but
    // per-axis check fails. Also 256^3=16,777,216 > 2,097,152.
    const std::string contents =
        "NRRD0004\ntype: int8\ndimension: 3\nsizes: 129 1 1\nencoding: raw\n\n" +
        std::string(129, '\x00');
    auto path = writeTemp("huge_axis", contents);
    auto result = io::loadNrrdVolume(path.string());
    EXPECT_TRUE(result.failed());
    EXPECT_EQ(result.error().code,
              static_cast<int>(io::VolumeLoadError::BudgetExceeded));
    EXPECT_EQ(result.error().domain, data::ErrorDomain::VolumeIo);
    std::filesystem::remove(path);

    // Product exceed: 256^3 = 16,777,216 voxels = 8 * budget (2,097,152)
    const std::string contents2 =
        "NRRD0004\ntype: uint8\ndimension: 3\nsizes: 256 256 256\nencoding: "
        "raw\n\n";
    auto path2 = writeTemp("huge_product", contents2);
    auto result2 = io::loadNrrdVolume(path2.string());
    EXPECT_TRUE(result2.failed());
    EXPECT_EQ(result2.error().code,
              static_cast<int>(io::VolumeLoadError::BudgetExceeded));
    std::filesystem::remove(path2);
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
