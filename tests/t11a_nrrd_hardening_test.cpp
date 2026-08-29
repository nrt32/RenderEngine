// tests/t11a_nrrd_hardening_test.cpp — T11a NRRD/Volume overflow & budgets
#include <gtest/gtest.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>
#include "core/caps.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "io/volume/nrrd_volume_loader.hpp"
#include "volume/ray_caster.hpp"
#include "volume/transfer_function.hpp"

namespace re::tests {
namespace {

constexpr float eps = 1e-6f; // 1e-6
constexpr float kOneOver255Tol = 1.0f / 255.0f;
constexpr std::uint64_t kHeaderSlack = 64ULL * 1024ULL;
constexpr std::uint64_t kMaxFileBytes = 512ULL * 512ULL * 512ULL * 8ULL + kHeaderSlack;

std::filesystem::path writeTemp(const std::string& tag, const std::string& contents) {
    auto p = std::filesystem::temp_directory_path() / ("re_t11a_" + tag + ".nrrd");
    std::ofstream out(p, std::ios::binary);
    out << contents;
    return p;
}

} // namespace

TEST(T11aHardening, HostileTruncatedReturnsError) {
    const std::string hdr = "NRRD0004\ntype: uint8\ndimension: 3\nsizes: 4294967296 1 1\nencoding: raw\n\n";
    auto path = writeTemp("hostile", hdr);
    core::resetCaps();
    core::injectCaps(core::Caps{0u, false});
    auto res = io::loadNrrdVolume(path.string());
    EXPECT_TRUE(res.failed());
    EXPECT_EQ(res.error().domain, data::ErrorDomain::VolumeIo); // BudgetExceeded 1/255 via Caps probe fail
    EXPECT_EQ(res.error().code, 8);
    std::filesystem::remove(path);
    core::resetCaps();
}

TEST(T11aHardening, GoldenSampleCtExact) {
    auto res = io::loadNrrdVolume(std::string(TEST_SOURCE_DIR) + "/data/volumes/sample_ct.nrrd");
    ASSERT_TRUE(res.ok()) << res.error().message;
    EXPECT_EQ(res->sizeX(), 128u);
    EXPECT_EQ(res->sizeY(), 128u);
    EXPECT_EQ(res->sizeZ(), 70u);
    EXPECT_EQ(res->voxelCount(), 128ULL * 128ULL * 70ULL);
    EXPECT_EQ(kMaxFileBytes, 1073807360ULL);
    // corner sample via trilinear at integer lattice reproduces voxel exactly
    EXPECT_NEAR(res->sampleTrilinear(0.0f, 0.0f, 0.0f), -3024.0f, eps);
    EXPECT_NEAR(res->sampleTrilinear(127.0f, 127.0f, 69.0f), -3024.0f, eps);
}

TEST(T11aHardening, TiledSyntheticWithinTol) {
    core::resetCaps();
    core::Caps small{32u, false};
    core::injectCaps(small);
    constexpr std::uint32_t sx = 256, sy = 128, sz = 64;
    std::vector<float> vox(static_cast<std::size_t>(sx) * sy * sz, 0.42f);
    data::VolumeDataset ds(sx, sy, sz, std::move(vox));
    // reference is same uniform value; tiled via Caps should stay within tol
    float v = ds.sampleTrilinear(128.0f, 64.0f, 32.0f);
    EXPECT_NEAR(v, 0.42f, kOneOver255Tol);
    // also check that arbitrary dims do not return error when caps non-zero
    const std::string hdr = "NRRD0004\ntype: uint8\ndimension: 3\nsizes: 256 128 64\nencoding: raw\n\n" + std::string(static_cast<std::size_t>(sx)*sy*sz, '\x01');
    auto path = writeTemp("tiled", hdr);
    auto res = io::loadNrrdVolume(path.string());
    ASSERT_TRUE(res.ok()) << res.error().message;
    EXPECT_EQ(res->sizeX(), 256u);
    std::filesystem::remove(path);
    core::resetCaps();
}

TEST(T11aHardening, SampleTrilinearNaNClampsAndSizeZeroGuard) {
    std::vector<float> vox(8, 1.0f);
    data::VolumeDataset ds(2, 2, 2, std::move(vox));
    float v = ds.sampleTrilinear(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);
    EXPECT_FALSE(std::isnan(v));
    EXPECT_NEAR(v, 1.0f, eps);
    // size==0 guard via empty dataset
    data::VolumeDataset empty(0, 0, 0, {});
    float e = empty.sampleTrilinear(0.0f, 0.0f, 0.0f);
    EXPECT_EQ(e, 0.0f);
    EXPECT_FALSE(std::isnan(e));
}

TEST(T11aHardening, TransferFunctionDuplicateTypedError) {
    using CP = volume::TransferFunction::ControlPoint;
    std::vector<CP> pts{{0.0f, {1,0,0,1}}, {0.5f, {0,1,0,0.5f}}, {0.5f, {0,0,1,0.25f}}};
    auto res = volume::TransferFunction::tryCreate(std::move(pts));
    EXPECT_TRUE(res.failed());
    // valid ramp passes
    std::vector<CP> good{{0.0f, {1,0,0,1}}, {0.5f, {0,1,0,0.5f}}, {1.0f, {0,0,1,0.25f}}};
    auto ok = volume::TransferFunction::tryCreate(std::move(good));
    ASSERT_TRUE(ok.ok());
    EXPECT_NEAR(ok->sample(0.25f).r, 0.5f, eps);
}

TEST(T11aHardening, RayAabbStepAnalytic) {
    volume::Ray ray{{-1.0f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}};
    volume::Aabb aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    auto steps = volume::computeRaySampleSteps(ray, aabb, 0.25f);
    EXPECT_NEAR(steps.tEntry, 1.0f, eps);
    EXPECT_NEAR(steps.tExit, 2.0f, eps);
    ASSERT_EQ(steps.positions.size(), 4u);
    EXPECT_NEAR(steps.positions[0], 1.125f, eps);
    EXPECT_NEAR(steps.positions[1], 1.375f, eps);
    EXPECT_NEAR(steps.positions[2], 1.625f, eps);
    EXPECT_NEAR(steps.positions[3], 1.875f, eps);
}

} // namespace re::tests
