// tests/t14_mpr_test.cpp — T14 gate tests (FR-app.2, SPEC §4).
//
// Asserts:
//   (1) FR-app.2(1) — the MPR viewport layout equals the SPEC constants: for a
//       1280x960 window the 2x2 grid yields four 640x480 viewports at the
//       pinned grid positions (T top-left, C top-right, S bottom-left, 3D
//       bottom-right);
//   (2) FR-app.2(2) — each 2D slice view samples the volume along its pinned
//       axis per the SPEC convention (Transverse = constant Z, Coronal =
//       constant Y, Sagittal = constant X), verified by a pixel check per view
//       against a closed-form synthetic volume + transfer function;
//   (3) FR-app.1-style smoke: the MPR sample (`re_sample_mpr`) runs under
//       Xvfb, opens a GL 4.6 core window, and exits cleanly (exit code 0, no
//       sanitizer reports) within a timeout — "MPR runs" (T14 gate G).
//
// Analytic setup for (2) — the synthetic volume is 2x2x2 with voxel value
//   value(x, y, z) = x + 2*y + 4*z   (x-fastest), so voxels 0..7:
//     (0,0,0)=0 (1,0,0)=1 (0,1,0)=2 (1,1,0)=3
//     (0,0,1)=4 (1,0,1)=5 (0,1,1)=6 (1,1,1)=7.
// The transfer function maps each integer value v in [0,7] to the straight
// RGBA color {v/255, (255-v)/255, 0, 1}, so the sampled red byte equals the
// voxel value v (round(v/255*255)=v) and the green byte equals 255-v. Because
// the TF is exact at each control point (FR-vol.1), each slice pixel's red
// byte IS the underlying voxel value — so the per-view pixel red bytes read
// the axis coordinates directly (explainable closed-form constants).
//
// Per the GL-ownership + readback guardrails this file touches no GL for the
// layout/slice tests (pure CPU scaffolding in app/); the smoke test spawns a
// subprocess only.
//
//   Transverse (constant Z = 0): image over (X, Y), 2x2.
//     pixel(0,0)=voxel(0,0,0)=0  pixel(1,0)=voxel(1,0,0)=1
//     pixel(0,1)=voxel(0,1,0)=2  pixel(1,1)=voxel(1,1,0)=3
//   Coronal (constant Y = 0): image over (X, Z), 2x2.
//     pixel(0,0)=voxel(0,0,0)=0  pixel(1,0)=voxel(1,0,0)=1
//     pixel(0,1)=voxel(0,0,1)=4  pixel(1,1)=voxel(1,0,1)=5
//   Sagittal (constant X = 0): image over (Y, Z), 2x2.
//     pixel(0,0)=voxel(0,0,0)=0  pixel(1,0)=voxel(0,1,0)=2
//     pixel(0,1)=voxel(0,0,1)=4  pixel(1,1)=voxel(0,1,1)=6

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "app/mpr_slice.hpp"
#include "data/image.hpp"
#include "data/volume_dataset.hpp"
#include "volume/color.hpp"
#include "volume/transfer_function.hpp"

namespace re::tests {
namespace {

namespace app = re::app;
namespace data = re::data;
namespace volume = re::volume;

// ---------------------------------------------------------------------------
// Explainable constants (FR-app.2).
// ---------------------------------------------------------------------------

// The SPEC window + viewport dims (FR-app.2).
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 960;
constexpr int kViewportWidth = 640;
constexpr int kViewportHeight = 480;

// Pinned grid positions (GL pixel coordinates, y up from the bottom):
// T top-left, C top-right, S bottom-left, 3D bottom-right.
constexpr std::array<app::MprViewport, 4> kExpectedViewports = {
    app::MprViewport{0, 480, 640, 480},    // T  (top-left)
    app::MprViewport{640, 480, 640, 480},  // C  (top-right)
    app::MprViewport{0, 0, 640, 480},      // S  (bottom-left)
    app::MprViewport{640, 0, 640, 480},    // 3D (bottom-right)
};

// The synthetic volume is 2x2x2 voxels: the smallest volume that still
// exercises trilinear sampling and voxel-index plane math, at 32 bytes of
// voxel data — effectively free for any test environment.
constexpr std::uint32_t kVolSize = 2u;

/// value(x, y, z) = x + 2*y + 4*z  (x-fastest), the closed-form voxel field.
float voxelValue(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return static_cast<float>(x) + 2.0f * static_cast<float>(y) +
           4.0f * static_cast<float>(z);
}

/// Build the 2x2x2 synthetic volume with the closed-form field above.
data::VolumeDataset makeSyntheticVolume() {
    std::vector<float> voxels;
    voxels.reserve(static_cast<std::size_t>(kVolSize) * kVolSize * kVolSize);
    for (std::uint32_t z = 0u; z < kVolSize; ++z) {
        for (std::uint32_t y = 0u; y < kVolSize; ++y) {
            for (std::uint32_t x = 0u; x < kVolSize; ++x) {
                voxels.push_back(voxelValue(x, y, z));
            }
        }
    }
    return data::VolumeDataset(kVolSize, kVolSize, kVolSize, std::move(voxels));
}

/// The transfer function mapping each integer v in [0,7] to the straight RGBA
/// {v/255, (255-v)/255, 0, 1}: exact at control points (FR-vol.1), so the red
/// byte equals v and the green byte equals 255-v.
volume::TransferFunction makeAxisProbeTransferFunction() {
    std::vector<volume::TransferFunction::ControlPoint> points;
    for (int v = 0; v < 8; ++v) {
        points.push_back({static_cast<float>(v),
                          volume::RgbaColor{static_cast<float>(v) / 255.0f,
                                            static_cast<float>(255 - v) /
                                                255.0f,
                                            0.0f, 1.0f}});
    }
    return volume::TransferFunction(std::move(points));
}

/// Assert a slice pixel's RGBA bytes equal the expected red (== voxel value v)
/// and green (== 255 - v) bytes; blue == 0, alpha == 255.
void expectPixel(const data::Image& image, std::int32_t x, std::int32_t y,
                 std::uint8_t expectedR) {
    ASSERT_LT(x, image.width());
    ASSERT_LT(y, image.height());
    const std::uint8_t expectedG = static_cast<std::uint8_t>(255 - expectedR);
    EXPECT_EQ(image.pixel(x, y, 0), expectedR) << "R at (" << x << "," << y
                                               << ")";
    EXPECT_EQ(image.pixel(x, y, 1), expectedG) << "G at (" << x << "," << y
                                               << ")";
    EXPECT_EQ(image.pixel(x, y, 2), 0u) << "B at (" << x << "," << y << ")";
    EXPECT_EQ(image.pixel(x, y, 3), 255u) << "A at (" << x << "," << y << ")";
}

// ---------------------------------------------------------------------------
// Smoke-test constants (FR-app.1 / T14 gate G).
// ---------------------------------------------------------------------------
constexpr const char* kSampleBin = RE_SAMPLE_MPR_BIN;
constexpr int kMaxFrames = 20;
constexpr int kTimeoutSeconds = 120;
constexpr const char* kWindowOpenedMarker = "GL 4.6 core";
constexpr const char* kSanitizerSignatures[] = {
    "AddressSanitizer", "UndefinedBehaviorSanitizer", "runtime error:",
    "LeakSanitizer"};

bool fileExists(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) FR-app.2(1) — the 2x2 viewport grid matches the SPEC constants.
// ---------------------------------------------------------------------------

TEST(T14Mpr, ViewportLayoutMatchesSpecConstants) {
    const std::array<app::MprViewport, 4> views = app::mprViewports(kWindowWidth,
                                                          kWindowHeight);

    ASSERT_EQ(views.size(), kExpectedViewports.size());
    for (std::size_t i = 0u; i < views.size(); ++i) {
        SCOPED_TRACE("viewport " + std::to_string(i));
        EXPECT_EQ(views[i].x, kExpectedViewports[i].x);
        EXPECT_EQ(views[i].y, kExpectedViewports[i].y);
        EXPECT_EQ(views[i].width, kExpectedViewports[i].width);
        EXPECT_EQ(views[i].height, kExpectedViewports[i].height);
        // Each viewport must be exactly the standard section size (640x480):
        // the MPR grid is defined as four equal quadrants of a 1280x960
        // window, and the analytic probe coordinates assume these constants.
        EXPECT_EQ(views[i].width, kViewportWidth);
        EXPECT_EQ(views[i].height, kViewportHeight);
    }

    // The four quadrants exactly tile the 1280x960 window.
    EXPECT_EQ(kExpectedViewports[0].width + kExpectedViewports[1].width,
              kWindowWidth);
    EXPECT_EQ(kExpectedViewports[2].width + kExpectedViewports[3].width,
              kWindowWidth);
    EXPECT_EQ(kExpectedViewports[0].height + kExpectedViewports[2].height,
              kWindowHeight);
    EXPECT_EQ(kExpectedViewports[1].height + kExpectedViewports[3].height,
              kWindowHeight);
}

// ---------------------------------------------------------------------------
// (2) FR-app.2(2) — each 2D slice view samples the volume along its axis.
// ---------------------------------------------------------------------------

TEST(T14Mpr, TransverseSamplesConstantZ) {
    data::VolumeDataset volume = makeSyntheticVolume();
    volume::TransferFunction tf = makeAxisProbeTransferFunction();
    data::Image slice =
        app::makeSliceImage(volume, tf, app::MprAxis::Transverse, /*zIndex=*/0u);

    // Constant Z = 0: image over (X, Y), 2x2. Red byte reads the voxel value.
    EXPECT_EQ(slice.width(), 2);
    EXPECT_EQ(slice.height(), 2);
    expectPixel(slice, 0, 0, 0u); // voxel(0,0,0)=0
    expectPixel(slice, 1, 0, 1u); // voxel(1,0,0)=1
    expectPixel(slice, 0, 1, 2u); // voxel(0,1,0)=2
    expectPixel(slice, 1, 1, 3u); // voxel(1,1,0)=3
}

TEST(T14Mpr, CoronalSamplesConstantY) {
    data::VolumeDataset volume = makeSyntheticVolume();
    volume::TransferFunction tf = makeAxisProbeTransferFunction();
    data::Image slice =
        app::makeSliceImage(volume, tf, app::MprAxis::Coronal, /*yIndex=*/0u);

    // Constant Y = 0: image over (X, Z), 2x2.
    EXPECT_EQ(slice.width(), 2);
    EXPECT_EQ(slice.height(), 2);
    expectPixel(slice, 0, 0, 0u); // voxel(0,0,0)=0
    expectPixel(slice, 1, 0, 1u); // voxel(1,0,0)=1
    expectPixel(slice, 0, 1, 4u); // voxel(0,0,1)=4
    expectPixel(slice, 1, 1, 5u); // voxel(1,0,1)=5
}

TEST(T14Mpr, SagittalSamplesConstantX) {
    data::VolumeDataset volume = makeSyntheticVolume();
    volume::TransferFunction tf = makeAxisProbeTransferFunction();
    data::Image slice =
        app::makeSliceImage(volume, tf, app::MprAxis::Sagittal, /*xIndex=*/0u);

    // Constant X = 0: image over (Y, Z), 2x2.
    EXPECT_EQ(slice.width(), 2);
    EXPECT_EQ(slice.height(), 2);
    expectPixel(slice, 0, 0, 0u); // voxel(0,0,0)=0
    expectPixel(slice, 1, 0, 2u); // voxel(0,1,0)=2
    expectPixel(slice, 0, 1, 4u); // voxel(0,0,1)=4
    expectPixel(slice, 1, 1, 6u); // voxel(0,1,1)=6
}

// ---------------------------------------------------------------------------
// (3) FR-app.1 / T14 gate G — the MPR sample runs, opens a window, exits
// cleanly.
// ---------------------------------------------------------------------------

TEST(T14Mpr, SampleRunsOpenWindowExitClean) {
    ASSERT_TRUE(fileExists(kSampleBin)) << "MPR sample binary missing";

    const std::string logFile =
        std::string(RE_TEST_BIN_DIR) + "/t14_mpr_sample.log";
    const std::string cmd =
        "timeout " + std::to_string(kTimeoutSeconds) + " env " +
        "RE_SAMPLE_MAX_FRAMES=" + std::to_string(kMaxFrames) + " " +
        "ASAN_OPTIONS=detect_leaks=0 " +
        "GALLIUM_DRIVER=llvmpipe MESA_GL_VERSION_OVERRIDE=4.6 " +
        "xvfb-run -a '" + kSampleBin + "' > '" + logFile + "' 2>&1";

    const int rc = std::system(cmd.c_str());
    std::string output;
    readFile(logFile, output);

    int exitCode = -1;
    if (rc != -1) {
        if (WIFEXITED(rc)) {
            exitCode = WEXITSTATUS(rc);
        }
    }

    EXPECT_EQ(exitCode, 0)
        << "MPR sample did not exit cleanly; captured output:\n" << output;
    EXPECT_NE(output.find(kWindowOpenedMarker), std::string::npos)
        << "MPR sample did not open a GL 4.6 core window; captured output:\n"
        << output;
    for (const char* signature : kSanitizerSignatures) {
        EXPECT_EQ(output.find(signature), std::string::npos)
            << "MPR sample reported a sanitizer error ('" << signature
            << "'); captured output:\n"
            << output;
    }
}

} // namespace re::tests
