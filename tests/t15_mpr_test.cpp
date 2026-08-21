// tests/t15_mpr_test.cpp — T15 gate tests (FR-app.3, SPEC §4).
//
// Asserts:
//   (1) FR-app.3(1) — the mesh contour overlay: for the golden box mesh, for
//       each slice view's plane (Transverse = constant Z, Coronal = constant
//       Y, Sagittal = constant X), the plane∩mesh cross-section curve computed
//       by app::meshPlaneContour equals the closed-form rectangle derived from
//       the box + plane, and app::overlayContour colors >= 90% of the pixels
//       within 2 px (Euclidean) of that analytic curve with exactly the
//       contour color (app::kContourColor, pure red -> RGBA8 255,0,0,255);
//   (2) FR-app.3(2) — the 3D view draws the mesh: the golden box rendered
//       through render::MeshRenderer with the slice-state-driven camera
//       (app::make3dCamera) has the analytic center-pixel color, and the
//       camera math itself is asserted (eye on the (1,1,1) diagonal at
//       1.5 bounding diagonals from the crosshair, which projects to the
//       viewport center);
//   (3) the MPR sample runs under Xvfb, opens a GL 4.6 core window and exits
//       cleanly (exit code 0, no sanitizer reports) — "MPR runs" (gate G).
//
// Analytic setup for (1) — the golden box is the box `[16,48]^3` in a 64^3
// synthetic volume; each view is held on the middle slice (index 32), so its
// slice plane passes through the voxel centers at coordinate 32 + 0.5 = 32.5
// on the held axis (slicePlane). Since 16 <= 32.5 <= 48, the plane cuts the
// box and the cross-section is the closed-form rectangle `[16,48]^2` in the
// slice image's pixel space (the two free axes; the rectangle is identical for
// all three views in their respective (x,y), (x,z) and (y,z) image spaces).
// The box's 12 triangles: the 4 faces perpendicular to the held axis do not
// cross the plane, the 4 side faces contribute 2 triangles each -> exactly 8
// crossing triangles -> 8 segments whose UNION is exactly the rectangle
// boundary (each rectangle edge is covered by two segments that meet at the
// edge's crossing point with the face diagonal; every segment endpoint lies on
// the boundary — hand-counted explainable constants).
//
// The acceptance band is the FR-app.3 2 px band: a pixel counts as "within
// 2 px of the curve" when its center (px+0.5, py+0.5) is within 2.0 of the
// closed-form rectangle boundary (min over the 4 edges of the point-segment
// distance). overlayContour colors every pixel within that band (plus a 1e-3
// float guard), so the matched fraction is ~100% >= the 90% SPEC threshold.
//
// Analytic setup for (2) — the golden box `[32,96]x[32,96]x[10,60]` (the MPR
// sample's box) with the sample's slice state (transverseZ=35, coronalY=64,
// sagittalX=64) -> crosshair (64.5, 64.5, 35.5). The camera (make3dCamera)
// stands 1.5 box diagonals from the crosshair along normalize(1,1,1), looking
// at it, so the center ray of the perspective projection enters the box on
// the +Z face (z = 60) at (sagittalX - transverseZ + 60.5,
// coronalY - transverseZ + 60.5, 60) = (89, 89, 60), strictly inside the face.
// makeBoxMesh builds the box as a flat-shaded quad shell (each face owns its
// own vertices), so every vertex of the +Z face has normal exactly (0,0,1)
// and the face renders at exactly the material's base color under the v1 +Z
// lighting (docs/render.md) — anywhere on the face, so the exact pixel-ray
// landing point does not matter. The box's faces are emitted in painter's
// order for this view (far faces -Z/-X/-Y first, +Z last) and v1 FBOs have no
// depth buffer (SPEC §6), so at the center pixel the +Z face overdraws
// everything else: the ray exits the box through the -Z face (which shades to
// black, drawn first), and the +X/+Y faces' projections do not cover the
// center pixel (the center ray meets them at z = 67, outside their face
// rectangles) — so the center pixel is the base color {51, 102, 204}.
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers (including core::readRgba8 for pixel readback) — no raw glXxx
// calls. The contour/camera tests are pure CPU scaffolding; the 3D-view test
// renders through render::MeshRenderer under the offscreen fixture; the smoke
// test spawns a subprocess only.

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "app/mpr_contour.hpp"
#include "app/mpr_slice.hpp"
#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "core/read_pixels.hpp"
#include "core/texture2d.hpp"
#include "data/image.hpp"
#include "data/mesh.hpp"
#include "data/volume_dataset.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "tests/offscreen_fixture.hpp"
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
// Explainable constants (FR-app.3).
// ---------------------------------------------------------------------------

// The golden box mesh for the contour test: `[16,48]^3` in voxel-index
// coordinates, inside the 64^3 synthetic volume.
constexpr glm::vec3 kBoxMin(16.0f, 16.0f, 16.0f);
constexpr glm::vec3 kBoxMax(48.0f, 48.0f, 48.0f);
constexpr std::uint32_t kVolSize = 64u;
constexpr std::uint32_t kSliceIndex = 32u; // middle slice per axis

// The analytic cross-section of the box with any slice plane through 32.5:
// the rectangle `[16,48]^2` in the slice image's pixel space. Its corners (in
// image (x, y) order) and its four edges.
constexpr std::array<glm::vec2, 4> kRectangleCorners = {
    glm::vec2(16.0f, 16.0f), glm::vec2(48.0f, 16.0f), glm::vec2(48.0f, 48.0f),
    glm::vec2(16.0f, 48.0f)};

// Hand-counted: the box's 4 side faces contribute 2 crossing triangles each ->
// exactly 8 contour segments, whose union covers the whole rectangle boundary
// (two segments meet at each edge's crossing point with the face diagonal).
constexpr std::size_t kExpectedSegmentCount = 8u;

// The FR-app.3 acceptance band: pixels within 2 px (Euclidean) of the curve.
constexpr float kAcceptanceBandPx = 2.0f;
// The SPEC acceptance threshold: >= 90% of in-band pixels match the contour
// color.
constexpr double kMinMatchFraction = 0.90;

// The slice background: the synthetic volume is constant 0 and the transfer
// function maps it to a dark opaque color (bytes round(0.05*255)=13,
// round(0.10*255)=26).
constexpr std::uint8_t kBackgroundR = 13u;
constexpr std::uint8_t kBackgroundG = 13u;
constexpr std::uint8_t kBackgroundB = 26u;
constexpr std::uint8_t kBackgroundA = 255u;

// The contour color bytes (app::kContourColor = pure red).
constexpr std::uint8_t kContourR = 255u;
constexpr std::uint8_t kContourG = 0u;
constexpr std::uint8_t kContourB = 0u;
constexpr std::uint8_t kContourA = 255u;

// The golden box mesh for the 3D-view test: the MPR sample's box inside the
// 128x128x70 CT volume, with the sample's slice state. The center ray enters
// the box at (89, 89, 60) on the +Z face (see the file header for the
// analytic argument).
constexpr glm::vec3 k3dBoxMin(32.0f, 32.0f, 10.0f);
constexpr glm::vec3 k3dBoxMax(96.0f, 96.0f, 60.0f);
constexpr std::uint32_t kTransverseZ = 35u;
constexpr std::uint32_t kCoronalY = 64u;
constexpr std::uint32_t kSagittalX = 64u;

// The 3D-view material base color: exact RGBA8 bytes (51, 102, 204).
constexpr glm::vec4 k3dBaseColor(0.2f, 0.4f, 0.8f, 1.0f);
constexpr std::uint8_t kExpectedR = 51u;
constexpr std::uint8_t kExpectedG = 102u;
constexpr std::uint8_t kExpectedB = 204u;

// The camera framing constants (make3dCamera, app/mpr_contour.hpp):
// eye = crosshair + normalize(1,1,1) * (1.5 * bounding diagonal).
constexpr float kCameraDistanceFactor = 1.5f;
// The camera's eye direction (the (1,1,1) diagonal, normalized).
const glm::vec3 kCameraDirection = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));

// Render-target size for the 3D-view readback (64x64; the aspect passed to
// make3dCamera matches the sample's 640x480 viewport: 4/3). The +Z face is
// flat-shaded (makeBoxMesh quad shell), so any pixel on the face reads the
// exact base color — the pixel-ray landing point does not matter.
constexpr std::uint32_t kTargetWidth = 64u;
constexpr std::uint32_t kTargetHeight = 64u;
constexpr std::uint32_t kCenterX = kTargetWidth / 2u;  // 32
constexpr std::uint32_t kCenterY = kTargetHeight / 2u; // 32
constexpr float kAspect = 4.0f / 3.0f;

// The color tolerance: 1/255 per FR-render.1 (SPEC §4 tolerances).
constexpr int kColorTolerance = 1;

// ---------------------------------------------------------------------------
// Test helpers.
// ---------------------------------------------------------------------------

/// Build the 64^3 synthetic volume with every voxel value 0 (the slice
/// background reads the flat transfer function's color at 0).
data::VolumeDataset makeSyntheticVolume() {
    std::vector<float> voxels(
        static_cast<std::size_t>(kVolSize) * kVolSize * kVolSize, 0.0f);
    return data::VolumeDataset(kVolSize, kVolSize, kVolSize, std::move(voxels));
}

/// A flat transfer function: value 0 maps to a dark opaque background.
volume::TransferFunction makeBackgroundTransferFunction() {
    const volume::RgbaColor background{0.05f, 0.05f, 0.10f, 1.0f};
    return volume::TransferFunction(
        {volume::TransferFunction::ControlPoint{0.0f, background},
         volume::TransferFunction::ControlPoint{1.0f, background}});
}

/// Point-segment distance (closed form, the analytic definition the FR-app.3
/// band uses).
float pointSegmentDistance(const glm::vec2& p, const glm::vec2& a,
                           const glm::vec2& b) {
    const glm::vec2 ab = b - a;
    const float lenSq = glm::dot(ab, ab);
    if (lenSq <= 0.0f) {
        return glm::length(p - a);
    }
    const float t = glm::clamp(glm::dot(p - a, ab) / lenSq, 0.0f, 1.0f);
    return glm::length(p - (a + t * ab));
}

/// Distance from `p` to the rectangle boundary (min over the 4 edges), in
/// pixel space — the analytic curve of the box cross-section.
float distanceToRectangle(const glm::vec2& p,
                          const std::array<glm::vec2, 4>& corners) {
    float minDistance = std::numeric_limits<float>::infinity();
    for (std::size_t i = 0u; i < corners.size(); ++i) {
        minDistance = std::min(
            minDistance,
            pointSegmentDistance(p, corners[i], corners[(i + 1u) % 4u]));
    }
    return minDistance;
}

/// True when the pixel at (px, py) of `image` is exactly the contour color.
bool isContourPixel(const data::Image& image, std::int32_t px,
                    std::int32_t py) {
    return image.pixel(px, py, 0) == kContourR &&
           image.pixel(px, py, 1) == kContourG &&
           image.pixel(px, py, 2) == kContourB &&
           image.pixel(px, py, 3) == kContourA;
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

// ---------------------------------------------------------------------------
// Smoke-test constants (FR-app.1 / T15 gate G).
// ---------------------------------------------------------------------------
constexpr const char* kSampleBin = RE_SAMPLE_MPR_BIN;
constexpr int kMaxFrames = 20;
constexpr int kTimeoutSeconds = 120;
constexpr const char* kWindowOpenedMarker = "GL 4.6 core";
constexpr const char* kSanitizerSignatures[] = {
    "AddressSanitizer", "UndefinedBehaviorSanitizer",
    "runtime error:", "LeakSanitizer"};

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
// (1) FR-app.3(1) — the contour curve equals the closed-form rectangle and
//     the overlay covers the 2 px analytic band, for each slice view.
// ---------------------------------------------------------------------------

TEST(T15Mpr, ContourMatchesAnalyticCurveForEachSliceView) {
    const data::VolumeDataset volume = makeSyntheticVolume();
    const volume::TransferFunction tf = makeBackgroundTransferFunction();
    const data::Mesh box = app::makeBoxMesh(kBoxMin, kBoxMax);

    // The three slice views and the free axes of their image space (the
    // rectangle corners are identical in each image's (x, y) pixel space).
    const std::array<app::MprAxis, 3> axes = {app::MprAxis::Transverse,
                                              app::MprAxis::Coronal,
                                              app::MprAxis::Sagittal};
    const std::array<const char*, 3> axisNames = {"Transverse", "Coronal",
                                                  "Sagittal"};

    for (std::size_t view = 0u; view < axes.size(); ++view) {
        SCOPED_TRACE(std::string("view ") + axisNames[view]);

        app::MprSliceState state;
        state.transverseZ = kSliceIndex;
        state.coronalY = kSliceIndex;
        state.sagittalX = kSliceIndex;
        const app::SlicePlane plane = app::slicePlane(axes[view], state);

        // The slice plane passes through the voxel centers: coordinate 32.5 on
        // the held axis.
        EXPECT_FLOAT_EQ(plane.coordinate,
                        static_cast<float>(kSliceIndex) + 0.5f);

        const std::vector<app::ContourSegment> curve =
            app::meshPlaneContour(box, plane);

        // Hand-counted explainable constant: 8 crossing triangles -> 8
        // segments (the box's 4 side faces contribute 2 triangles each; the
        // faces perpendicular to the held axis never cross the plane).
        EXPECT_EQ(curve.size(), kExpectedSegmentCount)
            << "contour segment count for the golden box";

        // Every segment endpoint lies ON the closed-form rectangle boundary
        // (min distance to the 4 edges <= 1e-3), and the UNION of the segments
        // covers the whole boundary: each rectangle edge, densely sampled at
        // 0.25-unit steps, is within 1e-3 of some segment (two segments meet
        // at each edge's midpoint).
        for (const app::ContourSegment& segment : curve) {
            for (const glm::vec2& endpoint : segment) {
                const float distance =
                    distanceToRectangle(endpoint, kRectangleCorners);
                EXPECT_LE(distance, 1e-3f)
                    << "endpoint (" << endpoint.x << ", " << endpoint.y
                    << ") must lie on the analytic rectangle boundary";
            }
        }
        for (std::size_t c = 0u; c < kRectangleCorners.size(); ++c) {
            const glm::vec2 a = kRectangleCorners[c];
            const glm::vec2 b = kRectangleCorners[(c + 1u) % 4u];
            for (float t = 0.0f; t <= 1.0f; t += 0.25f) {
                const glm::vec2 sample = a + t * (b - a);
                float minDistance = std::numeric_limits<float>::infinity();
                for (const app::ContourSegment& segment : curve) {
                    minDistance = std::min(
                        minDistance,
                        pointSegmentDistance(sample, segment[0], segment[1]));
                }
                EXPECT_LE(minDistance, 1e-3f)
                    << "rectangle edge sample (" << sample.x << ", " << sample.y
                    << ") must be covered by the contour curve";
            }
        }

        // Build the slice image and overlay the contour (the MPR path).
        data::Image slice =
            app::makeSliceImage(volume, tf, axes[view], kSliceIndex);
        data::Image overlaid =
            app::overlayContour(slice, curve, app::kContourColor);

        // Spot checks with explainable positions:
        //  - pixel (16, 16): its center (16.5, 16.5) is 0.5 px from the edge
        //    y = 16 -> inside the 2 px band -> exactly the contour color;
        //  - pixel (0, 0): its center (0.5, 0.5) is ~21.9 px from the
        //    rectangle boundary -> outside the band -> the unchanged slice
        //    background.
        EXPECT_TRUE(isContourPixel(overlaid, 16, 16))
            << "pixel (16,16) must be the contour color";
        EXPECT_EQ(overlaid.pixel(0, 0, 0), kBackgroundR);
        EXPECT_EQ(overlaid.pixel(0, 0, 1), kBackgroundG);
        EXPECT_EQ(overlaid.pixel(0, 0, 2), kBackgroundB);
        EXPECT_EQ(overlaid.pixel(0, 0, 3), kBackgroundA);

        // The FR-app.3: >= 90% of the pixels within 2 px (Euclidean) of the
        // analytic curve match the contour color.
        std::size_t inBand = 0u;
        std::size_t matched = 0u;
        for (std::int32_t py = 0; py < overlaid.height(); ++py) {
            for (std::int32_t px = 0; px < overlaid.width(); ++px) {
                const glm::vec2 center(static_cast<float>(px) + 0.5f,
                                       static_cast<float>(py) + 0.5f);
                if (distanceToRectangle(center, kRectangleCorners) <=
                    kAcceptanceBandPx) {
                    ++inBand;
                    if (isContourPixel(overlaid, px, py)) {
                        ++matched;
                    }
                }
            }
        }

        // The 2 px band around the 32x32 rectangle holds exactly 508 pixels of
        // the 64x64 image (closed-form count: perimeter 128 units x the 4-unit
        // band ~ 512, minus the 4 corner regions where the band of adjacent
        // edges overlaps; no pixel center sits at distance exactly 2.0, so the
        // count is float-rounding-independent — verified by enumeration).
        // The rasterizer colors every such pixel (the segment union equals the
        // boundary), so the matched fraction is ~100% >= the 90% SPEC
        // threshold.
        EXPECT_EQ(inBand, 508u) << "the analytic 2 px band pixel count";
        const double fraction =
            static_cast<double>(matched) / static_cast<double>(inBand);
        EXPECT_GE(fraction, kMinMatchFraction)
            << "matched " << matched << " of " << inBand
            << " in-band pixels (FR-app.3 >= 90%)";
    }
}

// ---------------------------------------------------------------------------
// (2) FR-app.3(2) — the 3D view draws the mesh: camera math + center pixel.
// ---------------------------------------------------------------------------

TEST(T15Mpr, CameraTracksSliceStateCrosshair) {
    // The golden box + the 3D-view test's slice state.
    const data::Mesh box = app::makeBoxMesh(k3dBoxMin, k3dBoxMax);
    app::MprSliceState state;
    state.transverseZ = kTransverseZ;
    state.coronalY = kCoronalY;
    state.sagittalX = kSagittalX;

    // The crosshair: the intersection point of the three slice planes, through
    // the voxel centers.
    const glm::vec3 crosshair(static_cast<float>(kSagittalX) + 0.5f,
                              static_cast<float>(kCoronalY) + 0.5f,
                              static_cast<float>(kTransverseZ) + 0.5f);

    const render::Camera camera =
        app::make3dCamera(state, box.bounds(), kAspect);

    // The camera eye: crosshair + normalize(1,1,1) * (1.5 * bounding diagonal).
    const float diagonal = glm::length(box.bounds().max - box.bounds().min);
    const float distance = kCameraDistanceFactor * diagonal;
    const glm::vec3 expectedEye = crosshair + kCameraDirection * distance;
    EXPECT_NEAR(camera.position.x, expectedEye.x, 1e-3f) << "eye x";
    EXPECT_NEAR(camera.position.y, expectedEye.y, 1e-3f) << "eye y";
    EXPECT_NEAR(camera.position.z, expectedEye.z, 1e-3f) << "eye z";

    // The camera looks at the crosshair: it projects to the viewport center
    // (NDC (0, 0)).
    const glm::vec4 clip =
        camera.proj * camera.view * glm::vec4(crosshair, 1.0f);
    EXPECT_GT(clip.w, 0.0f) << "the crosshair is in front of the camera";
    const glm::vec3 ndc(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
    EXPECT_NEAR(ndc.x, 0.0f, 1e-4f) << "crosshair NDC x (viewport center)";
    EXPECT_NEAR(ndc.y, 0.0f, 1e-4f) << "crosshair NDC y (viewport center)";

    // The view matrix's forward axis is its row 2 (glm matrices are
    // column-major: row i = (M[0][i], M[1][i], M[2][i])). For
    // lookAt(eye, target, up) row 2 = -f = normalize(eye - target), which is
    // the slice-state-driven (1,1,1) direction.
    const glm::vec3 forward(camera.view[0][2], camera.view[1][2],
                            camera.view[2][2]);
    EXPECT_NEAR(glm::length(forward - kCameraDirection), 0.0f, 1e-4f)
        << "camera forward must be the (1,1,1) diagonal direction";
}

TEST(T15Mpr, ThreeDViewDrawsMesh) {
    // The golden box + slice state + material (see the file header for the
    // analytic argument: the center ray enters the +Z face at (89, 89, 60)).
    const data::Mesh box = app::makeBoxMesh(k3dBoxMin, k3dBoxMax);
    render::PhongMaterial material(k3dBaseColor);
    ASSERT_FALSE(material.isTransparent());

    app::MprSliceState state;
    state.transverseZ = kTransverseZ;
    state.coronalY = kCoronalY;
    state.sagittalX = kSagittalX;
    const render::Camera camera =
        app::make3dCamera(state, box.bounds(), kAspect);

    render::MeshScene scene;
    scene.meshes.push_back(
        render::MeshInstance{&box, &material, glm::mat4(1.0f)});

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::RenderTarget rt;
    rt.framebuffer = &target.framebuffer;
    rt.width = kTargetWidth;
    rt.height = kTargetHeight;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    render::MeshRenderer renderer(nullptr);
    auto result = renderer.render(scene, camera, rt);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(core::hasPendingGlError());

    // Read back the center pixel from the still-bound target framebuffer.
    std::vector<std::uint8_t> pixels;
    auto read = core::readRgba8(kCenterX, kCenterY, 1u, 1u, pixels);
    ASSERT_TRUE(read.ok()) << read.error().message;
    ASSERT_EQ(pixels.size(), 4u);

    // The center ray enters the box on the +Z face (z = 60), which is
    // flat-shaded (makeBoxMesh builds a quad shell): every +Z-face vertex has
    // normal exactly (0,0,1), so the v1 lighting shades the fragment to
    // exactly the material's base color {51, 102, 204} (see the file header
    // for the analytic argument).
    EXPECT_NEAR(pixels[0], kExpectedR, kColorTolerance) << "R channel";
    EXPECT_NEAR(pixels[1], kExpectedG, kColorTolerance) << "G channel";
    EXPECT_NEAR(pixels[2], kExpectedB, kColorTolerance) << "B channel";
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (3) FR-app.1 / T15 gate G — the MPR sample runs, opens a window, exits
// cleanly.
// ---------------------------------------------------------------------------

TEST(T15Mpr, SampleRunsOpenWindowExitClean) {
    ASSERT_TRUE(fileExists(kSampleBin)) << "MPR sample binary missing";

    const std::string logFile =
        std::string(RE_TEST_BIN_DIR) + "/t15_mpr_sample.log";
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
        << "MPR sample did not exit cleanly; captured output:\n"
        << output;
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