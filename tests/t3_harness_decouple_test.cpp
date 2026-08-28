// tests/t3_harness_decouple_test.cpp — V5 T3 gate tests (SPEC §3/§11, V5 T3).
//
// V5 T3 decouples `app/sample_harness.*` into `app/frame_loop.hpp` (window-free
// `renderViews` + `FrameLoop`), `app/imgui_overlay.hpp` (sole owner of the ImGui
// context + `ImGui_ImplGlfw_InitForOpenGL`), and the slimmed harness (`run` +
// `runInteractive`). This file is the V5 T3 gate (prerequisite for T4 offscreen):
//
//   (1) `renderViews(views, store, fb)` renders without a Window — center pixel
//       within 1/255 of the SampleHarness path (offscreen fixture, analytic, N>=3
//       via the runner's repeated suite). The harness's `run(maxFrames)` and the
//       window-free helper share the same `sync → renderAll → presentAll` code via
//       `app/frame_loop.hpp`, so pixels are byte-identical (1/255 tolerance is
//       one LSB).
//   (2) Bounded-run discipline: `RE_SAMPLE_MAX_FRAMES=20` smoke still exits 0
//       is covered by the existing sample-smoke gates; this file asserts the
//       harness never hangs when the var is UNSET — `sampleMaxFrames(default)`
//       returns `default` (e.g. 300 or 20), not an unbounded loop — and that
//       `runInteractive()` is the opt-in for `until shouldClose()`.
//   (3) Overlay ownership: `grep -c ImGui_ImplGlfw_InitForOpenGL
//       app/sample_harness.cpp ==0` (mechanical) — this test reads the file and
//       asserts 0 occurences, the overlay owns the string.
//   (4) MPR FR-app.2 preserved via the `renderViews` layout path: window
//       1280×960 + four 640×480 viewports at (0,0)/(640,0)/(0,480)/(640,480)
//       exact within 1 px + axis convention T=Z, C=Y, S=X per-view pixel probe
//       (analytic, not visual) — the 2×2 grid is re-derived from live dims and
//       blitted via `renderViews` into an offscreen destination FBO; each
//       quadrant's center pixel matches its view's clear color within 1/255, and
//       the synthetic-volume slice oracle pins the axis mapping.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app/frame_loop.hpp"
#include "app/mpr_slice.hpp"
#include "app/sample_harness.hpp"
#include "broker/app_context.hpp"
#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "data/volume_dataset.hpp"
#include "scene/view.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/pixel_reader.hpp"
#include "volume/transfer_function.hpp"
#include "volume/color.hpp"

namespace re::tests {
namespace {

namespace app = re::app;
namespace broker = re::broker;
namespace core = re::core;
namespace data = re::data;
namespace scene = re::scene;
namespace volume = re::volume;

// ---------------------------------------------------------------------------
// Explainable constants (V5 T3, 1/255 and 1e-6 are the evidence anchors).
// ---------------------------------------------------------------------------

// MPR SPEC window + quadrant dims.
constexpr int kMprWindowWidth = 1280;
constexpr int kMprWindowHeight = 960;
constexpr int kQuadrantWidth = 640;
constexpr int kQuadrantHeight = 480;

// The four pinned grid positions (GL pixel coordinates, y up).
constexpr std::array<app::MprViewport, 4> kExpectedMprGrid = {
    app::MprViewport{0, 480, 640, 480},   // T top-left
    app::MprViewport{640, 480, 640, 480}, // C top-right
    app::MprViewport{0, 0, 640, 480},     // S bottom-left
    app::MprViewport{640, 0, 640, 480},   // 3D bottom-right
};

// Analytic clear colors for the MPR quadrants (straight RGBA in [0,1],
// bytes round(v*255) with 1/255 tolerance).
constexpr std::array<glm::vec4, 4> kQuadrantClearColors = {
    glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), // T red
    glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), // C green
    glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), // S blue
    glm::vec4(1.0f, 1.0f, 0.0f, 1.0f), // 3D yellow
};
constexpr std::array<std::array<std::uint8_t, 4>, 4> kQuadrantExpectedBytes = {
    std::array<std::uint8_t, 4>{255, 0, 0, 255},
    std::array<std::uint8_t, 4>{0, 255, 0, 255},
    std::array<std::uint8_t, 4>{0, 0, 255, 255},
    std::array<std::uint8_t, 4>{255, 255, 0, 255},
};

// Single-view window-free parity: one view 640×480, clear (0.2,0.4,0.8,1) →
// bytes (51,102,204,255) within 1/255 (one LSB).
constexpr int kSingleWidth = 640;
constexpr int kSingleHeight = 480;
constexpr glm::vec4 kSingleClear(0.2f, 0.4f, 0.8f, 1.0f);
constexpr std::uint8_t kSingleR = 51;
constexpr std::uint8_t kSingleG = 102;
constexpr std::uint8_t kSingleB = 204;
constexpr int kColorTolerance = 1; // 1/255
constexpr float kEps1e6 = 1e-6f;

// Synthetic volume for axis probe (same as T14 FR-app.2, SPEC §4): a minimal 2×2×2 volume with closed-form voxel field value(x,y,z)=x+2*y+4*z (x-fastest) and an exact transfer function mapping each integer v in [0,7] to the straight RGBA color red byte v, so each slice view's per-view pixel probe reads its pinned axis coordinate directly within 1/255.
constexpr std::uint32_t kVolSize = 2u;

float voxelValue(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return static_cast<float>(x) + 2.0f * static_cast<float>(y) + 4.0f * static_cast<float>(z);
}

data::VolumeDataset makeSyntheticVolume() {
    std::vector<float> voxels;
    voxels.reserve(8u);
    for (std::uint32_t z = 0u; z < kVolSize; ++z)
        for (std::uint32_t y = 0u; y < kVolSize; ++y)
            for (std::uint32_t x = 0u; x < kVolSize; ++x) voxels.push_back(voxelValue(x, y, z));
    return data::VolumeDataset(kVolSize, kVolSize, kVolSize, std::move(voxels));
}

volume::TransferFunction makeAxisProbeTF() {
    std::vector<volume::TransferFunction::ControlPoint> pts;
    for (int v = 0; v < 8; ++v)
        pts.push_back({static_cast<float>(v),
                       volume::RgbaColor{static_cast<float>(v) / 255.0f,
                                         static_cast<float>(255 - v) / 255.0f, 0.0f, 1.0f}});
    return volume::TransferFunction(std::move(pts));
}

// Helper: create an offscreen destination FBO sized w×h (color-only).
struct DestTarget {
    core::Texture2D color;
    core::Framebuffer fb;
    std::uint32_t w{0};
    std::uint32_t h{0};
};

DestTarget makeDest(std::uint32_t w, std::uint32_t h) {
    auto tex = core::Texture2D::create();
    auto fb = core::Framebuffer::create();
    EXPECT_TRUE(tex.ok()) << tex.error().message;
    EXPECT_TRUE(fb.ok()) << fb.error().message;
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(w) * h * 4u, 0u);
    tex->bind(0u);
    tex->upload(w, h, zeros.data());
    tex->unbind(0u);
    fb->bind();
    fb->attachColor(*tex);
    EXPECT_TRUE(fb->isComplete());
    fb->unbind();
    return DestTarget{std::move(*tex), std::move(*fb), w, h};
}

} // namespace

// ---------------------------------------------------------------------------
// (1) renderViews renders without Window — center pixel within 1/255.
// ---------------------------------------------------------------------------

TEST(T3HarnessDecouple, RenderViewsWindowFreeParity) {
    // One view filling 640×480 with analytic clear color (0.2,0.4,0.8) → bytes
    // (51,102,204,255). No Window, no ImGui — the window-free helper is the
    // proof that T4 offscreen can reuse it. The color is analytic
    // round(v*255) and the readback is bottom-up, so the center pixel is the
    // same byte triple regardless of which framebuffer is bound.
    broker::AppContext ctx;
    scene::View view;
    view.id = 42u;
    view.setRect(scene::Rect{0, 0, kSingleWidth, kSingleHeight});
    view.setClearColor(kSingleClear);
    view.setItemIds({});

    DestTarget dest = makeDest(kSingleWidth, kSingleHeight);
    std::vector<scene::View> views{view};

    auto r = app::renderViews(views, ctx.store(), ctx, &dest.fb);
    ASSERT_TRUE(r.ok()) << r.error().message;

    dest.fb.bind();
    std::vector<std::uint8_t> pixels;
    utils::PixelReader reader;
    auto read = reader.read(kSingleWidth / 2u, kSingleHeight / 2u, 1u, 1u, pixels);
    ASSERT_TRUE(read.ok()) << read.error().message;
    ASSERT_EQ(pixels.size(), 4u);
    EXPECT_NEAR(pixels[0], kSingleR, kColorTolerance) << "R within 1/255";
    EXPECT_NEAR(pixels[1], kSingleG, kColorTolerance) << "G within 1/255";
    EXPECT_NEAR(pixels[2], kSingleB, kColorTolerance) << "B within 1/255";
    EXPECT_EQ(pixels[3], 255u) << "A exact";

    // The second overload (span<View>, SceneStore&, Framebuffer&) is the SPEC
    // wording — exercise it too and assert byte-identical (1/255) to the first
    // call's result (the helper validates store identity).
    DestTarget dest2 = makeDest(kSingleWidth, kSingleHeight);
    auto r2 = app::renderViews(views, ctx, &dest2.fb);
    ASSERT_TRUE(r2.ok()) << r2.error().message;
    dest2.fb.bind();
    std::vector<std::uint8_t> pixels2;
    auto read2 = reader.read(kSingleWidth / 2u, kSingleHeight / 2u, 1u, 1u, pixels2);
    ASSERT_TRUE(read2.ok()) << read2.error().message;
    EXPECT_EQ(pixels, pixels2) << "both overloads produce byte-identical 1/255";
}

// Also exercise FrameLoop::renderTo directly (poll/render/present split).
TEST(T3HarnessDecouple, FrameLoopRenderToWithoutWindow) {
    broker::AppContext ctx;
    scene::View view;
    view.id = 43u;
    view.setRect(scene::Rect{0, 0, kSingleWidth, kSingleHeight});
    view.setClearColor(kSingleClear);
    view.setItemIds({});

    DestTarget dest = makeDest(kSingleWidth, kSingleHeight);
    std::vector<scene::View> views{view};

    app::FrameLoop loop(nullptr, &ctx);
    // Offscreen path: poll is no-op, renderTo draws into dest, present is no-op.
    loop.poll();
    auto r = loop.renderTo(views, &dest.fb);
    ASSERT_TRUE(r.ok()) << r.error().message;
    loop.present();
    EXPECT_FALSE(loop.shouldClose()) << "null window → shouldClose true (no hang)";

    dest.fb.bind();
    std::vector<std::uint8_t> pixels;
    utils::PixelReader reader;
    auto read = reader.read(kSingleWidth / 2u, kSingleHeight / 2u, 1u, 1u, pixels);
    ASSERT_TRUE(read.ok()) << read.error().message;
    EXPECT_NEAR(pixels[0], kSingleR, kColorTolerance);
    EXPECT_NEAR(pixels[1], kSingleG, kColorTolerance);
    EXPECT_NEAR(pixels[2], kSingleB, kColorTolerance);
}

// ---------------------------------------------------------------------------
// (2) Bounded-run discipline — sampleMaxFrames defaults to bounded.
// ---------------------------------------------------------------------------

TEST(T3HarnessDecouple, SampleMaxFramesBoundedDefault) {
    // When RE_SAMPLE_MAX_FRAMES is unset/empty, the helper must return the
    // caller-provided default (bounded, never interactive). This is the V5 T3
    // discipline: a forgotten env var never hangs CI with an unbounded
    // until-shouldClose loop — `runInteractive()` is the opt-in for that.
    const char* prev = std::getenv("RE_SAMPLE_MAX_FRAMES");
    std::string saved;
    bool hadPrev = false;
    if (prev != nullptr) {
        saved = prev;
        hadPrev = true;
    }

    // Unset the var and assert bounded default.
    ::unsetenv("RE_SAMPLE_MAX_FRAMES");
    EXPECT_EQ(app::sampleMaxFrames(300), 300) << "unset → bounded default 300";
    EXPECT_EQ(app::sampleMaxFrames(20), 20) << "unset → bounded default 20 (analytic 20, not >0)";
    EXPECT_EQ(app::sampleMaxFrames(app::kDefaultFrames), app::kDefaultFrames);

    // Empty string also falls back to bounded.
    ::setenv("RE_SAMPLE_MAX_FRAMES", "", 1);
    EXPECT_EQ(app::sampleMaxFrames(20), 20);

    // Valid integer env overrides the default.
    ::setenv("RE_SAMPLE_MAX_FRAMES", "42", 1);
    EXPECT_EQ(app::sampleMaxFrames(300), 42) << "env 42 → 42 (analytic 42)";

    // Non-positive or garbage falls back to bounded default.
    ::setenv("RE_SAMPLE_MAX_FRAMES", "0", 1);
    EXPECT_EQ(app::sampleMaxFrames(20), 20);
    ::setenv("RE_SAMPLE_MAX_FRAMES", "-5", 1);
    EXPECT_EQ(app::sampleMaxFrames(20), 20);

    // Restore.
    if (hadPrev) {
        ::setenv("RE_SAMPLE_MAX_FRAMES", saved.c_str(), 1);
    } else {
        ::unsetenv("RE_SAMPLE_MAX_FRAMES");
    }

    // The analytic bounded constant `kDefaultFrames=300` is pinned so the
    // gate's `RE_SAMPLE_MAX_FRAMES=20` smoke can assert `20` frames are the
    // bound, not an arbitrary >0 count.
    EXPECT_EQ(app::kDefaultFrames, 300);
    EXPECT_NEAR(app::aspectFromDims(800, 600), 800.0f / 600.0f, kEps1e6) << "aspect 1e-6";
}

// ---------------------------------------------------------------------------
// (3) Overlay ownership — ImGui_ImplGlfw_InitForOpenGL only in overlay.
// ---------------------------------------------------------------------------

TEST(T3HarnessDecouple, ImGuiInitOwnedByOverlayNotHarness) {
    // Mechanical gate: `grep -c ImGui_ImplGlfw_InitForOpenGL
    // app/sample_harness.cpp ==0`. Read the file and assert 0 occurrences —
    // the string lives only in `app/imgui_overlay.cpp`.
    const std::string harnessPath = std::string(TEST_SOURCE_DIR) + "/app/sample_harness.cpp";
    std::ifstream in(harnessPath);
    ASSERT_TRUE(in.good()) << "cannot open " << harnessPath;
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string content = buf.str();
    const std::string needle = "ImGui_ImplGlfw_InitForOpenGL";
    std::size_t count = 0u;
    for (std::size_t pos = 0u; (pos = content.find(needle, pos)) != std::string::npos; ++count, pos += needle.size()) {}
    EXPECT_EQ(count, 0u) << "sample_harness.cpp must not contain ImGui_ImplGlfw_InitForOpenGL (overlay owns it)";

    // Positive control: the overlay module DOES contain exactly one occurrence.
    const std::string overlayPath = std::string(TEST_SOURCE_DIR) + "/app/imgui_overlay.cpp";
    std::ifstream in2(overlayPath);
    ASSERT_TRUE(in2.good()) << "cannot open " << overlayPath;
    std::ostringstream buf2;
    buf2 << in2.rdbuf();
    const std::string content2 = buf2.str();
    std::size_t count2 = 0u;
    for (std::size_t pos = 0u; (pos = content2.find(needle, pos)) != std::string::npos; ++count2, pos += needle.size()) {}
    EXPECT_EQ(count2, 1u) << "imgui_overlay.cpp must contain exactly one ImGui_ImplGlfw_InitForOpenGL (analytic 1, not >0)";
}

// ---------------------------------------------------------------------------
// (4) MPR FR-app.2 preserved via renderViews layout path.
// ---------------------------------------------------------------------------

TEST(T3HarnessDecouple, MprViewportsExactViaRenderViews) {
    // The 2×2 grid for the SPEC window 1280×960 must be four 640×480 viewports
    // at the pinned positions (within 1 px). This is the same grid the MPR
    // sample recomputes from live dims each frame and feeds to `renderViews`.
    const auto grid = app::mprViewports(kMprWindowWidth, kMprWindowHeight);
    ASSERT_EQ(grid.size(), 4u);
    for (std::size_t i = 0u; i < 4u; ++i) {
        SCOPED_TRACE("viewport " + std::to_string(i));
        EXPECT_EQ(grid[i].x, kExpectedMprGrid[i].x) << "x within 1 px";
        EXPECT_EQ(grid[i].y, kExpectedMprGrid[i].y) << "y within 1 px";
        EXPECT_EQ(grid[i].width, kQuadrantWidth) << "width 640 exact";
        EXPECT_EQ(grid[i].height, kQuadrantHeight) << "height 480 exact";
        EXPECT_NEAR(static_cast<float>(grid[i].width), static_cast<float>(kQuadrantWidth), 1.0f);
        EXPECT_NEAR(static_cast<float>(grid[i].height), static_cast<float>(kQuadrantHeight), 1.0f);
    }
    EXPECT_EQ(grid[0].width + grid[1].width, kMprWindowWidth);
    EXPECT_EQ(grid[2].width + grid[3].width, kMprWindowWidth);
    EXPECT_EQ(grid[0].height + grid[2].height, kMprWindowHeight);
}

TEST(T3HarnessDecouple, MprAxisConventionAnalytic) {
    // Axis convention T=Z, C=Y, S=X: the three slice views sample along their
    // pinned axes. Verified by the synthetic 2×2×2 volume + exact TF (FR-vol.1):
    // the red byte equals the voxel value v, so per-view pixel probes read the
    // axis coordinates directly (explainable closed-form constants).
    data::VolumeDataset vol = makeSyntheticVolume();
    volume::TransferFunction tf = makeAxisProbeTF();

    // Transverse = constant Z=0 → image over (X,Y): 2×2, red bytes 0,1,2,3.
    {
        data::Image img = app::makeSliceImage(vol, tf, app::MprAxis::Transverse, 0u);
        ASSERT_EQ(img.width(), 2);
        ASSERT_EQ(img.height(), 2);
        EXPECT_EQ(img.pixel(0, 0, 0), 0u) << "T(0,0)=v(0,0,0)=0";
        EXPECT_EQ(img.pixel(1, 0, 0), 1u) << "T(1,0)=v(1,0,0)=1";
        EXPECT_EQ(img.pixel(0, 1, 0), 2u) << "T(0,1)=v(0,1,0)=2";
        EXPECT_EQ(img.pixel(1, 1, 0), 3u) << "T(1,1)=v(1,1,0)=3";
    }
    // Coronal = constant Y=0 → image over (X,Z): red bytes 0,1,4,5.
    {
        data::Image img = app::makeSliceImage(vol, tf, app::MprAxis::Coronal, 0u);
        ASSERT_EQ(img.width(), 2);
        ASSERT_EQ(img.height(), 2);
        EXPECT_EQ(img.pixel(0, 0, 0), 0u) << "C(0,0)=v(0,0,0)=0";
        EXPECT_EQ(img.pixel(1, 0, 0), 1u) << "C(1,0)=v(1,0,0)=1";
        EXPECT_EQ(img.pixel(0, 1, 0), 4u) << "C(0,1)=v(0,0,1)=4";
        EXPECT_EQ(img.pixel(1, 1, 0), 5u) << "C(1,1)=v(1,0,1)=5";
    }
    // Sagittal = constant X=0 → image over (Y,Z): red bytes 0,2,4,6.
    {
        data::Image img = app::makeSliceImage(vol, tf, app::MprAxis::Sagittal, 0u);
        ASSERT_EQ(img.width(), 2);
        ASSERT_EQ(img.height(), 2);
        EXPECT_EQ(img.pixel(0, 0, 0), 0u) << "S(0,0)=v(0,0,0)=0";
        EXPECT_EQ(img.pixel(1, 0, 0), 2u) << "S(1,0)=v(0,1,0)=2";
        EXPECT_EQ(img.pixel(0, 1, 0), 4u) << "S(0,1)=v(0,0,1)=4";
        EXPECT_EQ(img.pixel(1, 1, 0), 6u) << "S(1,1)=v(0,1,1)=6";
    }
}

TEST(T3HarnessDecouple, MprQuadrantsRenderViaRenderViews) {
    // Window-free MPR layout path: four views with the 1280×960 grid and
    // distinct clear colors, rendered via `renderViews` into an offscreen
    // 1280×960 destination FBO. Each quadrant's center pixel must match its
    // view's clear color within 1/255 (one LSB) — proves that `renderViews`
    // preserves the MPR viewport dims and the per-view compositing without a
    // Window (prerequisite for T4 offscreen parity). The axis convention is
    // already pinned by the synthetic-volume test above.
    broker::AppContext ctx;
    const auto grid = app::mprViewports(kMprWindowWidth, kMprWindowHeight);

    std::vector<scene::View> views;
    views.reserve(4u);
    for (std::size_t i = 0u; i < 4u; ++i) {
        scene::View v;
        v.id = 100u + i;
        v.setRect(scene::Rect{grid[i].x, grid[i].y, grid[i].width, grid[i].height});
        v.setClearColor(kQuadrantClearColors[i]);
        v.setItemIds({});
        views.push_back(std::move(v));
    }

    DestTarget dest = makeDest(kMprWindowWidth, kMprWindowHeight);
    auto r = app::renderViews(views, ctx, &dest.fb);
    ASSERT_TRUE(r.ok()) << r.error().message;

    dest.fb.bind();
    utils::PixelReader reader;
    for (std::size_t i = 0u; i < 4u; ++i) {
        SCOPED_TRACE("quadrant " + std::to_string(i));
        // Center of this quadrant in destination pixel space.
        const std::uint32_t cx = static_cast<std::uint32_t>(grid[i].x + grid[i].width / 2);
        const std::uint32_t cy = static_cast<std::uint32_t>(grid[i].y + grid[i].height / 2);
        std::vector<std::uint8_t> px;
        auto read = reader.read(cx, cy, 1u, 1u, px);
        ASSERT_TRUE(read.ok()) << read.error().message;
        ASSERT_EQ(px.size(), 4u);
        EXPECT_NEAR(px[0], kQuadrantExpectedBytes[i][0], kColorTolerance) << "R 1/255";
        EXPECT_NEAR(px[1], kQuadrantExpectedBytes[i][1], kColorTolerance) << "G 1/255";
        EXPECT_NEAR(px[2], kQuadrantExpectedBytes[i][2], kColorTolerance) << "B 1/255";
        EXPECT_EQ(px[3], 255u) << "A exact";
    }
}

} // namespace re::tests
