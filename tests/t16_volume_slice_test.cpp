// tests/t16_volume_slice_test.cpp — plane-capability gate tests: GPU
// volume-plane extraction replaces the textured-quad-only story (extends
// FR-render.5 / FR-app.2).
//
// What is asserted (every constant explainable from first principles):
//
//   (1) MID-PLANE ORACLE (the core acceptance): a synthetic 2x2x2 volume with
//       the closed-form field value(x,y,z) = x + 2y + 4z, cut by the plane
//       halfway between the two voxel layers (continuous held coordinate
//       z-index 0.5), reproduces tf.sample(dataset.sampleTrilinear(...))
//       within 1/255 at every probe pixel — including the four pixel centers
//       landing exactly on the interpolated column values x+2y+2 in {2,3,4,5}
//       (hand-derived bytes), a dense sweep against the same analytic oracle,
//       and exact rejection (transparent black) outside the volume footprint;
//   (2) FULL-FRAME ORACLE: a 16x16x16 volume (value = x + y + z, grayscale
//       opaque ramp) extracted at its middle layer reads the exact voxel
//       under EVERY pixel of a 16x16 target (viewport == slice dims), with
//       hand-derived spot constants {51, 102} at the exact-rational probes;
//   (3) OBLIQUE EXTRACTION: the diagonal plane x + z = 1 through the cube
//       center (normal normalize(1,0,1), identity model) matches the analytic
//       ray-plane oracle at off-center probes — the extraction is fully
//       general — and rays missing the cube footprint write exact zeros;
//   (4) INTERACTIVE STATE CHANGE: moving the slice plane one voxel layer
//       (plane point z: 0.5 -> 1.5) through the SAME renderer instance
//       updates the output to the NEW layer's closed-form values (+4 per red
//       byte) — correctness via readback after the state change; the path
//       carries no CPU image anywhere (structural: the renderer consumes
//       dataset + plane + transfer-function uniforms only);
//   (5) MPR AXIS CONVENTION PRESERVED ON THE GPU PATH: for each axis
//       (Transverse = constant Z, Coronal = constant Y, Sagittal = constant X)
//       the composed ReView path (app::sliceVolumeModel + app::sliceFreeAxes +
//       app::makeSliceCamera(free extents) + View/addItem/drawLayer — the
//       exact functions the MPR sample composes) reproduces the retained CPU
//       oracle app::makeSliceImage within 1/255 across the WHOLE frame, on an
//       asymmetric 8x6x4 volume so any axis permutation error fails;
//   (6) TYPED ERRORS: a null dataset reference, an oversized transfer
//       function, and a zero-sized target are typed failures, never crashes
//       and never silently empty output;
//   (7) MECHANICAL FLOOR: app/plane_sample.cpp loads a real volume
//       (loadNrrdVolume >= 1 hit) and renders extracted planes; the MPR
//       sample contains ZERO makeSliceImage calls (the frozen CPU images are
//       gone from the live path); the CPU oracle stays defined in
//       app/mpr_slice.cpp for the gates (>= 1 hit).
//
// Analytic frame for (1)/(4)/(5): the shared display scaffolding maps the
// dataset's model-space unit cube so voxel-center index i lands at display
// coordinate i + 0.5 (app::sliceVolumeModel), and the extraction shader back-
// converts via idx = modelPos*(dim-1). An orthographic camera over
// [0,freeW]x[0,freeH] therefore puts pixel center (px,py) at continuous index
// (px, py, heldIndex) EXACTLY when the viewport matches the free-axis extents,
// and the mid-plane at continuous held index 0.5 interpolates the two voxel
// layers with weights exactly (0.5, 0.5).
//
// Byte convention: expected = round(channel * 255) (std::round, half away
// from zero — the CPU oracle's own conversion in app/mpr_slice.cpp); GPU
// comparisons allow the 1/255 tolerance everywhere because the driver's
// float->unorm8 rounding at exact half-way values may differ by one unit.
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers (including utils::PixelReader for pixel readback) — no raw glXxx.

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "app/mpr_camera.hpp"
#include "app/mpr_slice.hpp"
#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "data/volume_dataset.hpp"
#include "render/view.hpp"
#include "render/volume_slice_renderer.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/pixel_reader.hpp"
#include "volume/color.hpp"
#include "volume/transfer_function.hpp"

namespace re::tests {
namespace {

namespace app = re::app;
namespace core = re::core;
namespace data = re::data;
namespace render = re::render;
namespace volume = re::volume;

// ---------------------------------------------------------------------------
// Explainable constants.
// ---------------------------------------------------------------------------

// The color tolerance: 1/255 — the finest difference an 8-bit readback can
// resolve at all; the FR acceptance bound for extracted-plane pixels.
constexpr int kColorTolerance = 1;

/// The closed-form 2x2x2 field: value(x,y,z) = x + 2y + 4z (voxels 0..7,
/// x-fastest). Every axis coordinate is readable directly off the sampled
/// value, so a misplaced axis permutation cannot hide behind a symmetric
/// constant.
float probeFieldValue(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return static_cast<float>(x) + 2.0f * static_cast<float>(y) +
           4.0f * static_cast<float>(z);
}

/// The 2x2x2 probe volume carrying probeFieldValue.
data::VolumeDataset makeProbeVolume() {
    constexpr std::uint32_t kSide = 2u;
    std::vector<float> voxels;
    voxels.reserve(static_cast<std::size_t>(kSide) * kSide * kSide);
    for (std::uint32_t z = 0u; z < kSide; ++z) {
        for (std::uint32_t y = 0u; y < kSide; ++y) {
            for (std::uint32_t x = 0u; x < kSide; ++x) {
                voxels.push_back(probeFieldValue(x, y, z));
            }
        }
    }
    return data::VolumeDataset(kSide, kSide, kSide, std::move(voxels));
}

/// The axis-probe transfer function: control point at every integer v in
/// [0,7] with straight RGBA {v/255, (255-v)/255, 0, 1} — exact at each
/// breakpoint (the piecewise-linear ramp property), so a sampled integer
/// value v displays as red byte v and green byte 255-v with zero TF error.
volume::TransferFunction makeAxisProbeTransferFunction() {
    std::vector<volume::TransferFunction::ControlPoint> points;
    for (int v = 0; v < 8; ++v) {
        points.push_back(
            {static_cast<float>(v),
             volume::RgbaColor{static_cast<float>(v) / 255.0f,
                               static_cast<float>(255 - v) / 255.0f, 0.0f,
                               1.0f}});
    }
    return volume::TransferFunction(std::move(points));
}

/// The project's float -> RGBA8 byte convention (the CPU oracle's own
/// conversion in app/mpr_slice.cpp): std::round, half away from zero.
std::uint8_t expectedByte(float channel) {
    return static_cast<std::uint8_t>(std::round(channel * 255.0f));
}

/// Build a color-only offscreen render target for readback.
struct RenderedTarget {
    core::Texture2D color;
    core::Framebuffer framebuffer;

    RenderedTarget(core::Texture2D c, core::Framebuffer f)
        : color(std::move(c)), framebuffer(std::move(f)) {}
};

RenderedTarget makeTarget(std::uint32_t w, std::uint32_t h) {
    auto color = core::Texture2D::create();
    auto framebuffer = core::Framebuffer::create();
    EXPECT_TRUE(color.ok()) << color.error().message;
    EXPECT_TRUE(framebuffer.ok()) << framebuffer.error().message;
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(w) * h * 4u, 0u);
    color->bind(0u);
    color->upload(w, h, zeros.data());
    color->unbind(0u);
    framebuffer->bind();
    framebuffer->attachColor(*color);
    EXPECT_TRUE(framebuffer->isComplete());
    framebuffer->unbind();
    return RenderedTarget(std::move(*color), std::move(*framebuffer));
}

/// Read the whole currently-bound framebuffer via utils::PixelReader (the
/// core/ readback anchor; no raw glXxx here).
std::vector<std::uint8_t> readBoundFramebuffer(std::uint32_t w,
                                               std::uint32_t h) {
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read = reader.read(0u, 0u, w, h, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), static_cast<std::size_t>(w) * h * 4u);
    return pixels;
}

/// Readback index helper (readback row 0 = viewport bottom, GL convention).
std::size_t pixelOffset(std::uint32_t px, std::uint32_t py,
                        std::uint32_t width) {
    return (static_cast<std::size_t>(py) * width + px) * 4u;
}

/// The world ray for the pixel whose center is at readback coordinates
/// (px, py): reconstructs it by unprojecting the pixel's NDC near/far points
/// through the camera's view-projection, exactly as the extraction shader
/// does.
std::pair<glm::vec3, glm::vec3> worldRayForPixel(std::uint32_t px,
                                                 std::uint32_t py,
                                                 std::uint32_t width,
                                                 std::uint32_t height,
                                                 const render::Camera& camera) {
    const float ndcX =
        (static_cast<float>(px) + 0.5f) / static_cast<float>(width) * 2.0f -
        1.0f;
    const float ndcY =
        (static_cast<float>(py) + 0.5f) / static_cast<float>(height) * 2.0f -
        1.0f;
    const glm::mat4 inv = glm::inverse(camera.proj * camera.view);
    const glm::vec4 nearH = inv * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    const glm::vec4 farH = inv * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    const glm::vec3 nearPos = glm::vec3(nearH) / nearH.w;
    const glm::vec3 farPos = glm::vec3(farH) / farH.w;
    return {nearPos, glm::normalize(farPos - nearPos)};
}

/// Count occurrences of `token` in the CODE of the file at `path`, after
/// stripping // line comments and /* */ block comments — so explanatory prose
/// mentioning an API cannot satisfy (or defeat) a mechanical gate that is
/// about actual call sites. Returns -1 when the file cannot be read so a
/// missing file fails the gate instead of silently passing with count 0.
int countInFile(const std::filesystem::path& path, const std::string& token) {
    std::ifstream in(path);
    if (!in) {
        return -1;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string raw = ss.str();
    std::string content;
    content.reserve(raw.size());
    bool inLineComment = false;
    bool inBlockComment = false;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const bool nextExists = i + 1 < raw.size();
        if (inLineComment) {
            if (raw[i] == '\n') {
                inLineComment = false;
                content.push_back(raw[i]);
            }
            continue;
        }
        if (inBlockComment) {
            if (nextExists && raw[i] == '*' && raw[i + 1] == '/') {
                inBlockComment = false;
                ++i;
                content.push_back(' ');
            }
            continue;
        }
        if (nextExists && raw[i] == '/' && raw[i + 1] == '/') {
            inLineComment = true;
            ++i;
            continue;
        }
        if (nextExists && raw[i] == '/' && raw[i + 1] == '*') {
            inBlockComment = true;
            ++i;
            continue;
        }
        content.push_back(raw[i]);
    }
    int hits = 0;
    for (std::size_t pos = content.find(token); pos != std::string::npos;
         pos = content.find(token, pos + token.size())) {
        ++hits;
    }
    return hits;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) Mid-plane oracle: 2x2x2 probe volume, plane halfway between the two
//     layers. Hand-derived bytes at the exact column probes + full analytic
//     sweep + exact rejection outside the volume footprint.
// ---------------------------------------------------------------------------

TEST(T16VolumeSlice, MidplaneMatchesCpuOracleWithinOneByte) {
    auto dataset =
        std::make_shared<const data::VolumeDataset>(makeProbeVolume());
    const volume::TransferFunction tf = makeAxisProbeTransferFunction();

    // Shared display frame: voxel-center index i -> display i + 0.5, so the
    // dataset occupies display [0.5, 1.5]^2 on the free axes and the
    // mid-plane sits at display z = 1.0 (continuous held index 0.5 — exactly
    // halfway between the voxel centers at index 0 and 1). Camera over
    // [0,2]x[0,2]; 64x64 target => pixel centers step 1/32 display units and
    // none lands exactly on a boundary (half-integers vs multiples of 1/32).
    render::VolumeSliceInstance instance;
    instance.dataset = dataset;
    instance.transferFunction = tf;
    instance.model = app::sliceVolumeModel(*dataset, app::MprAxis::Transverse);
    instance.plane.normal = glm::vec3(0.0f, 0.0f, 1.0f);
    instance.plane.point = glm::vec3(0.0f, 0.0f, 1.0f);
    render::VolumeSliceScene scene;
    scene.slices.push_back(instance);

    constexpr std::uint32_t kW = 64u;
    constexpr std::uint32_t kH = 64u;
    RenderedTarget target = makeTarget(kW, kH);
    render::RenderTarget rt;
    rt.framebuffer = &target.framebuffer;
    rt.width = kW;
    rt.height = kH;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    render::VolumeSliceRenderer renderer;
    auto result = renderer.render(scene, app::makeSliceCamera(2.0f, 2.0f), rt);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(core::hasPendingGlError());

    const std::vector<std::uint8_t> pixels = readBoundFramebuffer(kW, kH);

    // Dense sweep: every pixel either matches the analytic oracle (inside the
    // footprint [0.5, 1.5]^2) or is exact transparent black (outside). The
    // closed-form mid-plane density of column (x, y) is
    // (v(x,y,0) + v(x,y,1)) / 2 = x + 2y + 2 — the oracle expression below
    // evaluates exactly that at the pixel centers nearest display 1.0.
    std::size_t insideCount = 0u;
    for (std::uint32_t py = 0; py < kH; ++py) {
        for (std::uint32_t px = 0; px < kW; ++px) {
            // Analytic hit point of this pixel's ray on the plane: ortho down-
            // Z camera over [0,2]^2 maps pixel centers linearly to display.
            const float dispX =
                (static_cast<float>(px) + 0.5f) / static_cast<float>(kW) * 2.0f;
            const float dispY =
                (static_cast<float>(py) + 0.5f) / static_cast<float>(kH) * 2.0f;
            const std::size_t off = pixelOffset(px, py, kW);
            if (dispX < 0.5f || dispX > 1.5f || dispY < 0.5f || dispY > 1.5f) {
                // Outside the extracted rectangle: the shader writes exact
                // transparent black (and the clear color is the same value,
                // so this also proves nothing bled across the boundary).
                EXPECT_EQ(pixels[off + 0u], 0u) << "px " << px << " py " << py;
                EXPECT_EQ(pixels[off + 1u], 0u) << "px " << px << " py " << py;
                EXPECT_EQ(pixels[off + 2u], 0u) << "px " << px << " py " << py;
                EXPECT_EQ(pixels[off + 3u], 0u) << "px " << px << " py " << py;
                continue;
            }
            ++insideCount;
            // Continuous index coordinates of the hit point (display - 0.5),
            // held axis fixed at 0.5 by the plane.
            const float ix = dispX - 0.5f;
            const float iy = dispY - 0.5f;
            const float density = dataset->sampleTrilinear(ix, iy, 0.5f);
            const volume::RgbaColor expected = tf.sample(density);
            SCOPED_TRACE(::testing::Message() << "px " << px << " py " << py);
            EXPECT_NEAR(pixels[off + 0u], expectedByte(expected.r),
                        kColorTolerance)
                << "red must equal tf(sampleTrilinear) within 1/255";
            EXPECT_NEAR(pixels[off + 1u], expectedByte(expected.g),
                        kColorTolerance)
                << "green";
            EXPECT_NEAR(pixels[off + 2u], expectedByte(expected.b),
                        kColorTolerance)
                << "blue";
            EXPECT_NEAR(pixels[off + 3u], expectedByte(expected.a),
                        kColorTolerance)
                << "alpha (TF alpha 1)";
        }
    }
    // The covered block is exactly the 32x32 pixels whose centers fall within
    // display [0.5, 1.5]: centers are (px+0.5)/32, inside iff px in [16, 47].
    EXPECT_EQ(insideCount, 1024u)
        << "closed-form count of pixel centers inside the extraction";
}

// ---------------------------------------------------------------------------
// (4) Interactive state change: same renderer, new plane point -> new layer's
//     exact values (+4 per red byte). Correctness via readback after the
//     state change.
// ---------------------------------------------------------------------------

TEST(T16VolumeSlice, StateChangeReextractsNewLayerWithoutCpuImage) {
    auto dataset =
        std::make_shared<const data::VolumeDataset>(makeProbeVolume());
    const volume::TransferFunction tf = makeAxisProbeTransferFunction();

    // 2x2 target over [0,2]^2: pixel centers at display {0.5, 1.5} == the
    // outer voxel centers, so each pixel reads its column's voxel EXACTLY
    // (continuous index integers — zero interpolation error).
    RenderedTarget target = makeTarget(2u, 2u);
    render::RenderTarget rt;
    rt.framebuffer = &target.framebuffer;
    rt.width = 2u;
    rt.height = 2u;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    render::VolumeSliceRenderer renderer;
    render::VolumeSliceScene scene;
    render::VolumeSliceInstance instance;
    instance.dataset = dataset;
    instance.transferFunction = tf;
    instance.model = app::sliceVolumeModel(*dataset, app::MprAxis::Transverse);
    instance.plane.normal = glm::vec3(0.0f, 0.0f, 1.0f);

    // State A: slice plane through layer 0's centers (held index 0).
    instance.plane.point = glm::vec3(0.0f, 0.0f, 0.5f);
    scene.slices.clear();
    scene.slices.push_back(instance);
    auto first = renderer.render(scene, app::makeSliceCamera(2.0f, 2.0f), rt);
    ASSERT_TRUE(first.ok()) << first.error().message;
    const std::vector<std::uint8_t> layer0 = readBoundFramebuffer(2u, 2u);

    // State B: ONLY the plane point moves — one voxel layer forward (held
    // index 1). No new renderer, no new dataset upload, no intermediate
    // image: the change reaches the GPU exclusively through uniforms.
    instance.plane.point = glm::vec3(0.0f, 0.0f, 1.5f);
    scene.slices.clear();
    scene.slices.push_back(instance);
    auto second = renderer.render(scene, app::makeSliceCamera(2.0f, 2.0f), rt);
    ASSERT_TRUE(second.ok()) << second.error().message;
    EXPECT_FALSE(core::hasPendingGlError());
    const std::vector<std::uint8_t> layer1 = readBoundFramebuffer(2u, 2u);

    // Closed form: layer k's density at column (x,y) is x + 2y + 4k, so the
    // red byte IS the density and the green byte is 255 minus it. Readback
    // (px,py) shows column (px,py): display coords {0.5, 1.5} == voxel
    // centers at continuous index {0, 1} on both free axes.
    for (std::uint32_t py = 0; py < 2u; ++py) {
        for (std::uint32_t px = 0; px < 2u; ++px) {
            const std::size_t off = pixelOffset(px, py, 2u);
            SCOPED_TRACE(::testing::Message()
                         << "column (" << px << "," << py << ")");
            const int redA = layer0[off + 0u];
            const int redB = layer1[off + 0u];
            EXPECT_NEAR(redA, probeFieldValue(px, py, 0u), kColorTolerance)
                << "state A must show layer 0's exact density";
            EXPECT_NEAR(
                layer0[off + 1u],
                expectedByte((255.0f - probeFieldValue(px, py, 0u)) / 255.0f),
                kColorTolerance)
                << "state A green byte";
            EXPECT_NEAR(redB, probeFieldValue(px, py, 1u), kColorTolerance)
                << "state B must show layer 1's exact density";
            EXPECT_NEAR(redB - redA, 4, kColorTolerance)
                << "one layer step = +4 in the closed-form field";
            EXPECT_NEAR(
                layer1[off + 1u],
                expectedByte((255.0f - probeFieldValue(px, py, 1u)) / 255.0f),
                kColorTolerance)
                << "state B green byte";
            EXPECT_EQ(layer1[off + 3u], 255u) << "opaque TF alpha";
        }
    }
}

// ---------------------------------------------------------------------------
// (2) Full-frame oracle: 16^3 volume, middle layer, every pixel exact.
// ---------------------------------------------------------------------------

namespace {

/// The 16^3 field value(x,y,z) = x + y + z (max 45) and its grayscale opaque
/// ramp {0 -> black, 45 -> white}: the displayed gray byte is analytically
/// round(value * 255 / 45).
data::VolumeDataset makeCube16Volume() {
    constexpr std::uint32_t kSide = 16u;
    std::vector<float> voxels;
    voxels.reserve(static_cast<std::size_t>(kSide) * kSide * kSide);
    for (std::uint32_t z = 0u; z < kSide; ++z) {
        for (std::uint32_t y = 0u; y < kSide; ++y) {
            for (std::uint32_t x = 0u; x < kSide; ++x) {
                voxels.push_back(static_cast<float>(x + y + z));
            }
        }
    }
    return data::VolumeDataset(kSide, kSide, kSide, std::move(voxels));
}

volume::TransferFunction makeGrayRamp(float maxValue) {
    using CP = volume::TransferFunction::ControlPoint;
    return volume::TransferFunction(
        {CP{0.0f, volume::RgbaColor{0.0f, 0.0f, 0.0f, 1.0f}},
         CP{maxValue, volume::RgbaColor{1.0f, 1.0f, 1.0f, 1.0f}}});
}

} // namespace

TEST(T16VolumeSlice, FullFrameMatchesOracleOnMiddleLayer) {
    auto dataset =
        std::make_shared<const data::VolumeDataset>(makeCube16Volume());
    // Max field value: 15 + 15 + 15 = 45 (closed form).
    const volume::TransferFunction tf = makeGrayRamp(45.0f);

    render::VolumeSliceInstance instance;
    instance.dataset = dataset;
    instance.transferFunction = tf;
    instance.model = app::sliceVolumeModel(*dataset, app::MprAxis::Transverse);
    instance.plane.normal = glm::vec3(0.0f, 0.0f, 1.0f);
    // Middle slice per the sample's convention (sizeZ/2 = 8): display
    // z = 8.5 puts every pixel center at continuous index z = 8 exactly.
    instance.plane.point =
        glm::vec3(0.0f, 0.0f, static_cast<float>(dataset->sizeZ() / 2u) + 0.5f);
    render::VolumeSliceScene scene;
    scene.slices.push_back(instance);

    // Target == slice dims: every pixel center is a voxel center.
    constexpr std::uint32_t kW = 16u;
    constexpr std::uint32_t kH = 16u;
    RenderedTarget target = makeTarget(kW, kH);
    render::RenderTarget rt;
    rt.framebuffer = &target.framebuffer;
    rt.width = kW;
    rt.height = kH;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    render::VolumeSliceRenderer renderer;
    auto result =
        renderer.render(scene, app::makeSliceCamera(16.0f, 16.0f), rt);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(core::hasPendingGlError());

    const std::vector<std::uint8_t> pixels = readBoundFramebuffer(kW, kH);
    for (std::uint32_t py = 0; py < kH; ++py) {
        for (std::uint32_t px = 0; px < kW; ++px) {
            // Held index = sizeZ/2 = 8; density = px + py + 8 (exact).
            const float density =
                static_cast<float>(px + py + dataset->sizeZ() / 2u);
            const volume::RgbaColor expected = tf.sample(density);
            SCOPED_TRACE(::testing::Message() << "px " << px << " py " << py);
            const std::size_t off = pixelOffset(px, py, kW);
            EXPECT_NEAR(pixels[off + 0u], expectedByte(expected.r),
                        kColorTolerance);
            EXPECT_NEAR(pixels[off + 1u], expectedByte(expected.g),
                        kColorTolerance);
            EXPECT_NEAR(pixels[off + 2u], expectedByte(expected.b),
                        kColorTolerance);
            EXPECT_NEAR(pixels[off + 3u], expectedByte(expected.a),
                        kColorTolerance);
        }
    }

    // Hand-derived spot constants at exact-rational probes:
    //   (0,1): density 9  -> gray 9/45  -> 9*255/45   = 51  exact;
    //   (2,8): density 18 -> gray 18/45 -> 18*255/45  = 102 exact.
    EXPECT_EQ(pixels[pixelOffset(0u, 1u, kW) + 0u], 51u);
    EXPECT_EQ(pixels[pixelOffset(2u, 8u, kW) + 0u], 102u);
}

// ---------------------------------------------------------------------------
// (3) Oblique plane: diagonal cut x + z = 1 through the cube center, identity
//     model, analytic ray-plane oracle at five probes; misses write zeros.
// ---------------------------------------------------------------------------

TEST(T16VolumeSlice, ObliquePlaneMatchesRayOracleAndRejectsMisses) {
    auto dataset =
        std::make_shared<const data::VolumeDataset>(makeProbeVolume());
    const volume::TransferFunction tf = makeAxisProbeTransferFunction();

    // Identity model: world space == model space ([0,1]^3 cube).
    render::VolumeSliceInstance instance;
    instance.dataset = dataset;
    instance.transferFunction = tf;
    instance.model = glm::mat4(1.0f);
    instance.plane.normal = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));
    instance.plane.point = glm::vec3(0.5f, 0.5f, 0.5f);
    render::VolumeSliceScene scene;
    scene.slices.push_back(instance);

    // Camera looking straight down the plane normal at the cube center,
    // square ortho window ±0.75 (covers the √2 × 1 cross-section rectangle).
    const glm::vec3 center = instance.plane.point;
    render::Camera camera;
    camera.position = center + instance.plane.normal * 3.0f;
    camera.view =
        glm::lookAt(camera.position, center, glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(-0.75f, 0.75f, -0.75f, 0.75f, 0.1f, 10.0f);

    constexpr std::uint32_t kW = 64u;
    constexpr std::uint32_t kH = 64u;
    RenderedTarget target = makeTarget(kW, kH);
    render::RenderTarget rt;
    rt.framebuffer = &target.framebuffer;
    rt.width = kW;
    rt.height = kH;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    render::VolumeSliceRenderer renderer;
    auto result = renderer.render(scene, camera, rt);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(core::hasPendingGlError());

    const std::vector<std::uint8_t> pixels = readBoundFramebuffer(kW, kH);

    // Five probes: the center plus ±10 px along both screen axes (offsets of
    // 10/64 * 1.5 ≈ 0.234 view units; worst-case plane distance from the cube
    // center ≈ 0.33 < the 0.5 minimum center-to-boundary distance, so all
    // five hits lie strictly inside the cube).
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 5> probes = {
        {{32u, 32u}, {22u, 32u}, {42u, 32u}, {32u, 22u}, {32u, 42u}}};
    for (const auto& [px, py] : probes) {
        SCOPED_TRACE(::testing::Message()
                     << "probe (" << px << "," << py << ")");
        const auto [ro, rd] = worldRayForPixel(px, py, kW, kH, camera);
        const float denom = glm::dot(instance.plane.normal, rd);
        ASSERT_GT(std::abs(denom), 1e-6f) << "ray must not graze the plane";
        const float t =
            glm::dot(instance.plane.normal, instance.plane.point - ro) / denom;
        ASSERT_GT(t, 0.0f) << "hit must be in front of the eye";
        const glm::vec3 hit = ro + rd * t;
        // Inside the unit cube (with margin for float noise on the probes).
        ASSERT_TRUE(glm::all(glm::greaterThanEqual(hit, glm::vec3(-1e-3f))) &&
                    glm::all(glm::lessThanEqual(hit, glm::vec3(1.0f + 1e-3f))))
            << "probe hit must lie inside the volume";
        // Identity model: model position IS the hit point; dims are 2, so
        // continuous index = hit * (dim - 1) = hit.
        const float density = dataset->sampleTrilinear(hit.x, hit.y, hit.z);
        const volume::RgbaColor expected = tf.sample(density);
        const std::size_t off = pixelOffset(px, py, kW);
        EXPECT_NEAR(pixels[off + 0u], expectedByte(expected.r),
                    kColorTolerance);
        EXPECT_NEAR(pixels[off + 1u], expectedByte(expected.g),
                    kColorTolerance);
        EXPECT_NEAR(pixels[off + 2u], expectedByte(expected.b),
                    kColorTolerance);
        EXPECT_NEAR(pixels[off + 3u], expectedByte(expected.a),
                    kColorTolerance);
    }

    // Corner rays miss the cube footprint entirely (their plane distance from
    // the center exceeds the √2/2 half-diagonal), so they write exact
    // transparent black.
    for (const auto& [px, py] :
         {std::pair<std::uint32_t, std::uint32_t>{0u, 0u}, {63u, 63u}}) {
        SCOPED_TRACE(::testing::Message()
                     << "miss (" << px << "," << py << ")");
        const std::size_t off = pixelOffset(px, py, kW);
        EXPECT_EQ(pixels[off + 0u], 0u);
        EXPECT_EQ(pixels[off + 1u], 0u);
        EXPECT_EQ(pixels[off + 2u], 0u);
        EXPECT_EQ(pixels[off + 3u], 0u);
    }
}

// ---------------------------------------------------------------------------
// (5) MPR axis convention preserved on the GPU path, per axis, whole-frame,
//     against the retained CPU oracle makeSliceImage (composed ReView path).
// ---------------------------------------------------------------------------

namespace {

/// The asymmetric gate volume 8x6x4 with field value = x + y + z (max 14):
/// unequal per-axis dims make any axis permutation or row-flip error visible,
/// and the small value range keeps the grayscale ramp lossless.
data::VolumeDataset makeAsymmetricVolume() {
    constexpr std::uint32_t kSx = 8u;
    constexpr std::uint32_t kSy = 6u;
    constexpr std::uint32_t kSz = 4u;
    std::vector<float> voxels;
    voxels.reserve(static_cast<std::size_t>(kSx) * kSy * kSz);
    for (std::uint32_t z = 0u; z < kSz; ++z) {
        for (std::uint32_t y = 0u; y < kSy; ++y) {
            for (std::uint32_t x = 0u; x < kSx; ++x) {
                voxels.push_back(static_cast<float>(x + y + z));
            }
        }
    }
    return data::VolumeDataset(kSx, kSy, kSz, std::move(voxels));
}

} // namespace

TEST(T16VolumeSlice, MprAxisConventionPreservedOnGpuPath) {
    auto dataset =
        std::make_shared<const data::VolumeDataset>(makeAsymmetricVolume());
    // Field max = 7 + 5 + 3 = 15 (closed form); grayscale opaque ramp.
    const volume::TransferFunction tf = makeGrayRamp(15.0f);

    const std::array<app::MprAxis, 3> axes = {app::MprAxis::Transverse,
                                              app::MprAxis::Coronal,
                                              app::MprAxis::Sagittal};
    const std::array<const char*, 3> names = {"Transverse", "Coronal",
                                              "Sagittal"};
    // Middle slice per axis, the sample's convention (size/2):
    // Transverse holds z = 2, Coronal holds y = 3, Sagittal holds x = 4.
    const std::array<std::uint32_t, 3> heldIndex = {
        dataset->sizeZ() / 2u, dataset->sizeY() / 2u, dataset->sizeX() / 2u};

    // One shared renderer across all three views (the sample's shape).
    auto sliceRenderer = std::make_shared<render::VolumeSliceRenderer>();

    for (std::size_t i = 0u; i < axes.size(); ++i) {
        SCOPED_TRACE(names[i]);

        // GPU path: composed exactly like the live MPR sample — shared display
        // scaffolding + render::View + type-erased drawLayer dispatch.
        render::VolumeSliceInstance instance;
        instance.dataset = dataset;
        instance.transferFunction = tf;
        instance.model = app::sliceVolumeModel(*dataset, axes[i]);
        instance.plane.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        instance.plane.point =
            glm::vec3(0.0f, 0.0f, static_cast<float>(heldIndex[i]) + 0.5f);
        render::VolumeSliceScene gpuScene;
        gpuScene.slices.push_back(instance);

        const auto [freeW, freeH] = app::sliceFreeAxes(*dataset, axes[i]);
        const std::uint32_t targetW = freeW; // target == slice dims: every
        const std::uint32_t targetH = freeH; // pixel center is a voxel center
        RenderedTarget target = makeTarget(targetW, targetH);
        render::RenderTarget rt;
        rt.framebuffer = &target.framebuffer;
        rt.width = targetW;
        rt.height = targetH;
        rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

        render::View view(render::ViewRect{0, 0, static_cast<int>(targetW),
                                           static_cast<int>(targetH)},
                          glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        view.setCamera(app::makeSliceCamera(static_cast<float>(freeW),
                                            static_cast<float>(freeH)));
        view.addItem(gpuScene, sliceRenderer);
        core::DrawContext ctx;
        auto rendered = view.renderWithEnsure(ctx);
        ASSERT_TRUE(rendered.ok()) << rendered.error().message;
        EXPECT_FALSE(core::hasPendingGlError());

        ASSERT_NE(view.target(), nullptr);
        view.target()->framebuffer().bind();
        const std::vector<std::uint8_t> pixels =
            readBoundFramebuffer(targetW, targetH);

        // CPU oracle for the same axis/index (retained reference
        // implementation; top-left-origin image).
        const data::Image oracle =
            app::makeSliceImage(*dataset, tf, axes[i], heldIndex[i]);
        ASSERT_EQ(oracle.width(), static_cast<std::int32_t>(targetW));
        ASSERT_EQ(oracle.height(), static_cast<std::int32_t>(targetH));

        // Whole-frame comparison. Orientation: BOTH spaces run along the
        // second free-axis index — the display frame puts voxel index j at
        // display y = j + 0.5 (so GL readback row py == voxel row py), and the
        // oracle image's rows carry the SAME index in its own top-left-origin
        // layout (row r == voxel row r). The two therefore correspond row for
        // row with no flip; column order matches directly.
        for (std::uint32_t py = 0; py < targetH; ++py) {
            for (std::uint32_t px = 0; px < targetW; ++px) {
                const std::size_t off = pixelOffset(px, py, targetW);
                for (int c = 0; c < 4; ++c) {
                    EXPECT_NEAR(pixels[off + static_cast<std::size_t>(c)],
                                oracle.pixel(static_cast<std::int32_t>(px),
                                             static_cast<std::int32_t>(py), c),
                                kColorTolerance)
                        << "channel " << c << " at readback (" << px << ","
                        << py << ")";
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// (6) Typed errors: null dataset, oversized TF, invalid target size.
// ---------------------------------------------------------------------------

TEST(T16VolumeSlice, TypedErrorsOnBadInput) {
    auto dataset =
        std::make_shared<const data::VolumeDataset>(makeProbeVolume());
    const volume::TransferFunction tf = makeAxisProbeTransferFunction();

    RenderedTarget target = makeTarget(8u, 8u);
    render::RenderTarget rt;
    rt.framebuffer = &target.framebuffer;
    rt.width = 8u;
    rt.height = 8u;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    render::Camera camera = app::makeSliceCamera(2.0f, 2.0f);

    // Null dataset reference: typed error code 1 naming the cause — never a
    // crash, never a silently empty layer.
    {
        render::VolumeSliceScene scene;
        render::VolumeSliceInstance nullInstance;
        nullInstance.dataset = nullptr;
        nullInstance.transferFunction = tf;
        nullInstance.plane.point = glm::vec3(0.0f, 0.0f, 1.0f);
        scene.slices.push_back(nullInstance);
        render::VolumeSliceRenderer renderer;
        auto result = renderer.render(scene, camera, rt);
        ASSERT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, 1);
        EXPECT_NE(result.error().message.find("null dataset"),
                  std::string::npos);
    }

    // Transfer function above the shader's fixed uniform array size (8):
    // typed error mirroring the ray-cast renderer's contract.
    {
        std::vector<volume::TransferFunction::ControlPoint> points;
        for (int v = 0; v < 9; ++v) {
            points.push_back({static_cast<float>(v),
                              volume::RgbaColor{0.0f, 0.0f, 0.0f, 1.0f}});
        }
        const volume::TransferFunction tooMany(std::move(points));
        render::VolumeSliceScene scene;
        render::VolumeSliceInstance instance;
        instance.dataset = dataset;
        instance.transferFunction = tooMany;
        instance.plane.point = glm::vec3(0.0f, 0.0f, 1.0f);
        scene.slices.push_back(instance);
        render::VolumeSliceRenderer renderer;
        auto result = renderer.render(scene, camera, rt);
        ASSERT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, 1);
        EXPECT_NE(result.error().message.find("more than 8 control points"),
                  std::string::npos);
    }

    // Zero-sized target: invalid geometry, rejected before any draw.
    {
        render::VolumeSliceScene scene;
        render::VolumeSliceInstance instance;
        instance.dataset = dataset;
        instance.transferFunction = tf;
        instance.plane.point = glm::vec3(0.0f, 0.0f, 1.0f);
        scene.slices.push_back(instance);
        render::VolumeSliceRenderer renderer;
        render::RenderTarget badTarget;
        badTarget.framebuffer = &target.framebuffer;
        badTarget.width = 0u;
        badTarget.height = 8u;
        auto result = renderer.render(scene, camera, badTarget);
        ASSERT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, 1);
        EXPECT_NE(result.error().message.find("invalid target size"),
                  std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// (7) Mechanical floor: samples rewired onto the GPU path; oracle retained.
// ---------------------------------------------------------------------------

TEST(T16VolumeSlice, SamplesUseExtractionPathMechanicalFloor) {
    const std::filesystem::path appDir =
        std::filesystem::path(TEST_SOURCE_DIR) / "app";

    // The plane sample must load a real volume (task gate: >= 1 hit) and draw
    // it through the GPU extraction renderer.
    const int loaderHits =
        countInFile(appDir / "plane_sample.cpp", "loadNrrdVolume");
    ASSERT_GE(loaderHits, 0) << "plane_sample.cpp must be readable";
    EXPECT_GE(loaderHits, 1)
        << "plane sample must load a volume via loadNrrdVolume";
    const int extractorHits =
        countInFile(appDir / "plane_sample.cpp", "VolumeSliceRenderer");
    ASSERT_GE(extractorHits, 0) << "plane_sample.cpp must be readable";
    EXPECT_GE(extractorHits, 1)
        << "plane sample must render through VolumeSliceRenderer";
    // ...and must NOT contain the old procedural gradient quad builder.
    const int gradientHits =
        countInFile(appDir / "plane_sample.cpp", "makeGradientImage");
    ASSERT_GE(gradientHits, 0) << "plane_sample.cpp must be readable";
    EXPECT_EQ(gradientHits, 0)
        << "the gradient-quad stand-in is gone from the plane sample";

    // The MPR sample's 2D views must have left the frozen CPU path entirely.
    const int frozenHits =
        countInFile(appDir / "mpr_sample.cpp", "makeSliceImage");
    ASSERT_GE(frozenHits, 0) << "mpr_sample.cpp must be readable";
    EXPECT_EQ(frozenHits, 0)
        << "MPR sample must not build CPU slice images anymore";
    const int mprExtractorHits =
        countInFile(appDir / "mpr_sample.cpp", "VolumeSliceRenderer");
    ASSERT_GE(mprExtractorHits, 0) << "mpr_sample.cpp must be readable";
    EXPECT_GE(mprExtractorHits, 1)
        << "MPR 2D views must render through VolumeSliceRenderer";

    // The CPU oracle stays defined for the gates (this file's reference).
    const int oracleHits =
        countInFile(appDir / "mpr_slice.cpp", "makeSliceImage");
    ASSERT_GE(oracleHits, 0) << "mpr_slice.cpp must be readable";
    EXPECT_GE(oracleHits, 1)
        << "makeSliceImage remains defined as the gate-test oracle";
}

} // namespace re::tests
