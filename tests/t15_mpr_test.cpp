// tests/t15_mpr_test.cpp — T15 gate tests (FR-app.3, SPEC §4), V3.8b T11
// edition: the MPR contour overlay is computed ON THE GPU by
// render::ContourRenderer's geometry shader (contour.geom.glsl) and verified
// by framebuffer readback (core::readRgba8 via utils::PixelReader).
//
// Asserts:
//   (1) FR-app.3(1) — the GPU mesh contour overlay: for the golden box mesh,
//       for each slice view (Transverse = constant Z, Coronal = constant Y,
//       Sagittal = constant X), rendering the box's plane∩mesh outline through
//       render::ContourRenderer (translated scene→render through
//       broker::ContourMapper) colors >= 90% of the pixels within 2 px
//       (Euclidean) of the analytic cross-section rectangle boundary with the
//       contour color (app::kContourColor, pure red -> RGBA8 255,0,0,255)
//       within 1/255;
//   (1b) the broker translation layer: broker::ContourMapper turns a
//       scene::ContourObject into the RE-minimal render::ContourObject
//       {AssetHandle, ClipPlane, color, model} with registry dedup (one GPU
//       object per CPU mesh) and typed errors for null mesh / null registry /
//       Space::VoxelIndex planes (SPEC §5);
//   (1c) T11 review regression: the contour is visible through the SAMPLE'S
//       composition path — app::makeSliceCamera / app::makeSliceModel (the
//       shared scaffolding the live sample uses) + render::View layered
//       drawLayer dispatch + ViewTarget readback. This locks the camera
//       enclosure contract whose violation was the user-verified "MPR sample
//       shows no contour" defect (a slice camera whose clip volume excluded
//       the display-frame z of every contour crossing point silently clipped
//       all outline quads away — no GL error, no failed Result);
//   (2) FR-app.3(2) — the 3D view draws the mesh: the golden box rendered
//       through render::MeshRenderer with the slice-state-driven camera
//       (app::make3dCamera) has the analytic center-pixel color, and the
//       camera math itself is asserted (eye on the (1,1,1) diagonal at
//       1.5 bounding diagonals from the crosshair, which projects to the
//       viewport center);
//   (3) the MPR sample runs under Xvfb, opens a GL 4.6 core window and exits
//       cleanly (exit code 0, no sanitizer reports) — "MPR runs" (gate G).
//
// Analytic setup for (1) — the golden box is the box `[16,48]^3`; each view's
// contour object carries the axis-permutation model that maps voxel-index
// space into that view's display space (Transverse identity, Coronal swaps
// Y/Z, Sagittal maps (x,y,z)->(y,z,x)) and a clip plane already expressed in
// that local/display frame (constant Z at the sliced voxel layer's coordinate
// 32.5 = index 32 + 0.5 through the voxel centers, app::slicePlane). The
// display frame of EVERY view is then identical: the free axes span
// [0,64]x[0,64] mapped 1:1 onto the 64x64 viewport by one shared ortho
// down-Z camera (pixel (px,py) <-> coordinate (px+0.5, py+0.5)), so the
// analytic cross-section is the closed-form rectangle `[16,48]^2` in PIXEL
// space for all three views.
//
// Hand-counted geometry-shader behavior: the box's 12 triangles are classified
// against the plane by signed distance exactly like slice_clip.geom.glsl —
// the 4 faces perpendicular to the held axis do not cross, the 4 side faces
// contribute 2 crossing triangles each -> exactly 8 emitted thick-line quads,
// whose UNION is exactly the rectangle boundary (each rectangle edge is
// covered by two segments meeting at the edge's crossing point with the face
// diagonal; every segment endpoint lies on the boundary). Each quad is
// expanded perpendicular to its segment by uHalfWidthPx = 2 px and extended
// by square caps of the same length, so EVERY pixel whose center lies within
// 2 px of an analytic segment lies inside its quad (closed-form containment:
// dist(P, AB) <= h implies P = closest-point + offset with both components
// <= h). The acceptance band is the FR-app.3 2 px band: a pixel counts when
// its center (px+0.5, py+0.5) is within 2.0 of the rectangle boundary (min
// over the 4 edges of the point-segment distance); no pixel center sits at
// distance exactly 2.0 (centers are half-integers, edges at integers), so the
// count is float-rounding-independent.
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
// wrappers (including utils::PixelReader for pixel readback) — no raw glXxx
// calls. The contour/camera tests render under the offscreen fixture; the
// smoke test spawns a subprocess only.

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

#include "app/mpr_camera.hpp"
#include "app/mpr_slice.hpp"
#include "broker/broker.hpp"
#include "broker/contour_mapper.hpp"
#include "broker/plane_mapper.hpp"
#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "core/texture2d.hpp"
#include "data/mesh.hpp"
#include "render/asset_registry.hpp"
#include "render/contour_renderer.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "render/plane_renderer.hpp"
#include "render/view.hpp"
#include "scene/object.hpp"
#include "scene/translate_context.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/pixel_reader.hpp"

namespace re::tests {
namespace {

namespace app = re::app;
namespace broker = re::broker;
namespace core = re::core;
namespace data = re::data;
namespace render = re::render;
namespace scene = re::scene;

// ---------------------------------------------------------------------------
// Explainable constants (FR-app.3).
// ---------------------------------------------------------------------------

// The golden box mesh for the contour test: `[16,48]^3`, inside a nominal
// 64^3 voxel grid (integer bounds + half-integer slice planes => non-
// degenerate analytic cross-sections).
constexpr glm::vec3 kBoxMin(16.0f, 16.0f, 16.0f);
constexpr glm::vec3 kBoxMax(48.0f, 48.0f, 48.0f);
constexpr std::uint32_t kSliceIndex = 32u; // middle slice per axis

// The analytic cross-section of the box with any slice plane through 32.5:
// the rectangle `[16,48]^2` in the view's DISPLAY (= pixel) space. Its
// corners (in (x, y) order) and its four edges.
constexpr std::array<glm::vec2, 4> kRectangleCorners = {
    glm::vec2(16.0f, 16.0f), glm::vec2(48.0f, 16.0f), glm::vec2(48.0f, 48.0f),
    glm::vec2(16.0f, 48.0f)};

// Hand-counted: the box's 4 side faces contribute 2 crossing triangles each ->
// exactly 8 geometry-shader outline segments (thick-line quads), whose union
// covers the whole rectangle boundary (two segments meet at each edge's
// crossing point with the face diagonal). Pinned so the analytic argument and
// the constant cannot drift apart.
constexpr std::size_t kExpectedSegmentCount = 8u;
static_assert(kExpectedSegmentCount == 8u,
              "golden box: 4 side faces x 2 crossing triangles = 8 segments");

// The FR-app.3 acceptance band: pixels within 2 px (Euclidean) of the curve.
constexpr float kAcceptanceBandPx = 2.0f;
// The stroke half-width fed to the renderer: fills the ±2 px band exactly.
constexpr float kStrokeHalfWidthPx = 2.0f;
// The SPEC acceptance threshold: >= 90% of in-band pixels match the contour
// color.
constexpr double kMinMatchFraction = 0.90;

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

// The camera framing constants (make3dCamera, app/mpr_camera.hpp):
// eye = crosshair + normalize(1,1,1) * (1.5 * bounding diagonal).
constexpr float kCameraDistanceFactor = 1.5f;
// The camera's eye direction (the (1,1,1) diagonal, normalized).
const glm::vec3 kCameraDirection = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));

// Render-target size (64x64; the aspect passed to make3dCamera matches the
// sample's 640x480 viewport: 4/3). The +Z face is flat-shaded (makeBoxMesh
// quad shell), so any pixel on the face reads the exact base color — the
// pixel-ray landing point does not matter.
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

/// True when the RGBA8 pixel `px` matches the contour color within 1/255.
bool isRedPixel(const std::vector<std::uint8_t>& pixels, std::size_t offset) {
    return std::abs(static_cast<int>(pixels[offset]) -
                    static_cast<int>(kContourR)) <= kColorTolerance &&
           std::abs(static_cast<int>(pixels[offset + 1u]) -
                    static_cast<int>(kContourG)) <= kColorTolerance &&
           std::abs(static_cast<int>(pixels[offset + 2u]) -
                    static_cast<int>(kContourB)) <= kColorTolerance &&
           std::abs(static_cast<int>(pixels[offset + 3u]) -
                    static_cast<int>(kContourA)) <= kColorTolerance;
}

/// The axis-permutation display models shared with the MPR sample: they map
/// voxel-index space into each view's display space so that the displayed
/// free axes are always display (x, y):
///   Transverse: identity              — display (x,y) = voxel (x,y);
///   Coronal:    swap Y/Z              — display (x,y) = voxel (x,z);
///   Sagittal:   (x,y,z) -> (y,z,x)    — display (x,y) = voxel (y,z).
///
/// glm::mat4's constructor takes COLUMNS; each initializer list below is read
/// DOWN the matrix. The Sagittal entry is deliberately NOT the cyclic shift
/// (0,1,0)(0,0,1)(1,0,0): that encodes the transposed (z,x,y) permutation,
/// which a symmetric cube + symmetric slice planes cannot distinguish here —
/// but the sample's non-cubic box immediately shows it as a misplaced/clipped
/// Sagittal outline (T11 review finding 2). PermutationView below pins each
/// model on an asymmetric probe vector so a transposition always fails.
std::array<glm::mat4, 3> axisDisplayModels() {
    return {glm::mat4(1.0f),
            // Coronal: rows (x'|y'|z') = (x|z|y).
            glm::mat4(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                      1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
            // Sagittal: rows (x'|y'|z') = (y|z|x) => columns (read DOWN)
            // (0,0,1)(1,0,0)(0,1,0). The transposed cyclic shift
            // (0,1,0)(0,0,1)(1,0,0) realizes (z,x,y) instead — invisible on a
            // symmetric cube, but it put the live Sagittal outline half
            // off-screen (T11 review finding 2).
            glm::mat4(0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f)};
}

/// The shared ortho camera mapping the display frame's [0,W]x[0,H] plane 1:1
/// onto the viewport (pixel (px,py) <-> coordinate (px+0.5, py+0.5)): looks
/// straight down -Z from above the display-rect CENTER, so the view space is
/// centered too and the symmetric ortho window [-W/2,W/2]x[-H/2,H/2] lands
/// exactly on the display rectangle.
render::Camera makeDisplayCamera(float w, float h) {
    render::Camera camera;
    constexpr float kEyeDistance = 50.0f;
    camera.position = glm::vec3(w * 0.5f, h * 0.5f, kEyeDistance);
    camera.view = glm::lookAt(camera.position,
                              glm::vec3(w * 0.5f, h * 0.5f, 0.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj =
        glm::ortho(-w * 0.5f, w * 0.5f, -h * 0.5f, h * 0.5f, 0.1f,
                   2.0f * kEyeDistance);
    return camera;
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
// (1) FR-app.3(1) — the GPU-computed contour covers the ±2 px analytic band
//     of the closed-form rectangle for each slice view (readback-verified).
// ---------------------------------------------------------------------------

TEST(T15Mpr, GpuContourMatchesAnalyticCurveForEachSliceView) {
    const data::Mesh box = app::makeBoxMesh(kBoxMin, kBoxMax);

    // One shared registry: the mapper registers the box once; the renderer
    // resolves the handle (dedup invariant asserted separately below).
    render::AssetRegistry registry;
    broker::ContourMapper mapper(&registry);
    render::ContourRenderer renderer(&registry);

    const std::array<app::MprAxis, 3> axes = {app::MprAxis::Transverse,
                                              app::MprAxis::Coronal,
                                              app::MprAxis::Sagittal};
    const std::array<const char*, 3> axisNames = {"Transverse", "Coronal",
                                                  "Sagittal"};
    const std::array<glm::mat4, 3> displayModels = axisDisplayModels();

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

        // Scene-side contour object: the box + the axis-permutation display
        // model + the clip plane already expressed in that display frame
        // (constant Z at the sliced layer's coordinate).
        scene::ContourObject appContour;
        appContour.mesh = &box;
        appContour.transform = displayModels[view];
        appContour.plane.setNormal(glm::vec3(0.0f, 0.0f, 1.0f));
        appContour.plane.setPoint(glm::vec3(0.0f, 0.0f, plane.coordinate));
        appContour.color = app::kContourColor;

        auto mapped = mapper.map(appContour, scene::TranslateContext{});
        ASSERT_TRUE(mapped.ok()) << mapped.error().message;
        render::ContourObject gpuContour = *mapped;
        EXPECT_FALSE(gpuContour.mesh.isNull())
            << "the translated contour must carry a live AssetHandle";

        render::ContourScene contourScene;
        gpuContour.halfWidthPx = kStrokeHalfWidthPx;
        contourScene.contours.push_back(gpuContour);

        RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
        render::RenderTarget rt;
        rt.framebuffer = &target.framebuffer;
        rt.width = kTargetWidth;
        rt.height = kTargetHeight;
        rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

        const render::Camera camera =
            makeDisplayCamera(static_cast<float>(kTargetWidth),
                              static_cast<float>(kTargetHeight));

        auto result = renderer.render(contourScene, camera, rt);
        ASSERT_TRUE(result.ok()) << result.error().message;
        EXPECT_FALSE(core::hasPendingGlError());

        // Read back the whole target (still bound after render).
        std::vector<std::uint8_t> pixels;
        re::utils::PixelReader reader;
        auto read = reader.read(0u, 0u, kTargetWidth, kTargetHeight, pixels);
        ASSERT_TRUE(read.ok()) << read.error().message;
        ASSERT_EQ(pixels.size(),
                  static_cast<std::size_t>(kTargetWidth) * kTargetHeight * 4u);

        // Spot checks with explainable positions:
        //  - pixel (16, 16): its center (16.5, 16.5) is ~0.71 px from the
        //    corner (16,16) -> deep inside the 2 px band -> the contour red;
        //  - pixel (0, 0): its center (0.5, 0.5) is ~21.9 px from the
        //    rectangle boundary -> far outside any stroke -> the untouched
        //    transparent-black clear color.
        const std::size_t spotInside =
            (static_cast<std::size_t>(16) * kTargetWidth + 16u) * 4u;
        EXPECT_TRUE(isRedPixel(pixels, spotInside))
            << "pixel (16,16) must be the contour color";
        const std::size_t spotOutside = 0u; // pixel (0,0), bottom-left corner
        EXPECT_EQ(pixels[spotOutside], 0u);
        EXPECT_EQ(pixels[spotOutside + 1u], 0u);
        EXPECT_EQ(pixels[spotOutside + 2u], 0u);
        EXPECT_EQ(pixels[spotOutside + 3u], 0u);

        // The FR-app.3: >= 90% of the pixels within 2 px (Euclidean) of the
        // analytic curve match the contour color within 1/255.
        std::size_t inBand = 0u;
        std::size_t matched = 0u;
        for (std::uint32_t py = 0; py < kTargetHeight; ++py) {
            for (std::uint32_t px = 0; px < kTargetWidth; ++px) {
                const glm::vec2 center(static_cast<float>(px) + 0.5f,
                                       static_cast<float>(py) + 0.5f);
                if (distanceToRectangle(center, kRectangleCorners) <=
                    kAcceptanceBandPx) {
                    ++inBand;
                    if (isRedPixel(
                            pixels,
                            (static_cast<std::size_t>(py) * kTargetWidth +
                             px) *
                                4u)) {
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
        EXPECT_EQ(inBand, 508u) << "the analytic 2 px band pixel count";

        // Every in-band pixel center lies inside some emitted thick-line quad
        // (square caps included), so the matched fraction is ~100% — far above
        // the 90% SPEC threshold.
        const double fraction =
            static_cast<double>(matched) / static_cast<double>(inBand);
        EXPECT_GE(fraction, kMinMatchFraction)
            << "matched " << matched << " of " << inBand
            << " in-band pixels (FR-app.3 >= 90%)";
    }

    // Registry dedup across all three views' translations: the same CPU box
    // registered three times leaves exactly ONE live GPU object (SPEC §9 V2.5).
    EXPECT_EQ(registry.slotCount(), 1u)
        << "one GPU object per individual CPU mesh, shared across views";
}

// ---------------------------------------------------------------------------
// (1c) T11 review regression — the contour must be visible through the
//      SAMPLE'S composition path, not only through direct renderer::render().
//
// The user-verified defect (2026-08-24): the readback test passed while the
// live MPR sample showed NO contour on any slice view. Root cause: the
// sample's slice camera sat at display z = +5 with far = 10, so its clip
// volume covered display z in [-4.9, +4.9] — but every contour crossing point
// lives at the held voxel-layer coordinate + 0.5 (32.5 here), far outside.
// Every geometry-shader quad was clipped away silently (no GL error, no
// failed Result). This test drives the EXACT functions and objects the
// sample composes — app::makeSliceCamera / app::makeSliceModel (moved to
// app/mpr_camera.hpp so the gate can call them), render::View +
// addItem(...)->drawLayer(...) layering, ViewTarget readback — so a
// sample-vs-test wiring divergence can never reintroduce itself:
// any camera that clips the contour away fails HERE too.
// ---------------------------------------------------------------------------

TEST(T15Mpr, GpuContourVisibleThroughSampleViewComposition) {
    // A solid mid-gray 64x64 slice image: every texel is (40,40,40,255), so
    // orientation-independent exact bytes under the PlaneRenderer (the
    // interior probe below asserts this value analytically).
    constexpr std::uint8_t kSliceByte = 40u;
    std::vector<std::uint8_t> solid(static_cast<std::size_t>(64) * 64 * 4u);
    for (std::size_t i = 0u; i < solid.size(); i += 4u) {
        solid[i + 0u] = kSliceByte;
        solid[i + 1u] = kSliceByte;
        solid[i + 2u] = kSliceByte;
        solid[i + 3u] = 255u;
    }
    const data::Image sliceImage(64, 64, 4, std::move(solid));

    // The golden box + slice state + plane, exactly as the first test's
    // analytic frame ([16,48]^2 rectangle in pixel space of a 64x64 view).
    const data::Mesh box = app::makeBoxMesh(kBoxMin, kBoxMax);
    app::MprSliceState state;
    state.transverseZ = kSliceIndex;
    state.coronalY = kSliceIndex;
    state.sagittalX = kSliceIndex;
    const float planeCoordinate =
        static_cast<float>(kSliceIndex) + 0.5f; // 32.5

    // CAMERA ENCLOSURE CONTRACT (app/mpr_camera.hpp): makeSliceCamera's clip
    // volume must enclose BOTH the slice quad (display z = 0) AND the contour
    // crossings' display z (= the held coordinate + 0.5 = 32.5). Asserted
    // analytically on the projected NDC z of both points.
    const render::Camera camera = app::makeSliceCamera(sliceImage);
    for (const float enclosedZ : {0.0f, planeCoordinate}) {
        const glm::vec4 clip =
            camera.proj * camera.view * glm::vec4(32.5f, 32.5f, enclosedZ, 1.0f);
        ASSERT_GT(clip.w, 0.0f) << "enclosed z " << enclosedZ
                                << " must lie in front of the eye";
        const float ndcZ = clip.z / clip.w;
        EXPECT_GT(ndcZ, -1.0f) << "display z " << enclosedZ
                               << " above the near plane";
        EXPECT_LT(ndcZ, 1.0f) << "display z " << enclosedZ
                              << " below the far plane (a camera that clips "
                                 "the contour away is the T11 defect)";
    }

    // Broker-mediated translation, exactly as the sample constructor does it:
    // BOTH slice-view layers are translated scene→render through mappers
    // fetched from the Broker as type-erased IMapper interfaces (V3.4b T12
    // adds the textured slice layer; V3.8b T11 added the contour).
    render::AssetRegistry registry;
    broker::Broker broker_;
    broker_.registerMapper(std::make_unique<broker::ContourMapper>(&registry));
    broker_.registerMapper(std::make_unique<broker::PlaneMapper>());
    auto* contourMapper =
        broker_.get<scene::ContourObject, render::ContourObject>();
    auto* planeMapper =
        broker_.get<scene::PlaneObject, render::PlaneInstance>();
    ASSERT_NE(contourMapper, nullptr);
    ASSERT_NE(planeMapper, nullptr);
    render::ContourRenderer renderer(&registry);

    scene::ContourObject appContour;
    appContour.mesh = &box;
    appContour.transform = axisDisplayModels()[0]; // Transverse: identity
    appContour.plane.setNormal(glm::vec3(0.0f, 0.0f, 1.0f));
    appContour.plane.setPoint(glm::vec3(0.0f, 0.0f, planeCoordinate));
    appContour.color = app::kContourColor;
    auto mapped = contourMapper->map(appContour, scene::TranslateContext{});
    ASSERT_TRUE(mapped.ok()) << mapped.error().message;
    mapped->halfWidthPx = kStrokeHalfWidthPx;
    render::ContourScene contourScene;
    contourScene.contours.push_back(*mapped);

    // The textured slice layer is ALSO broker-mediated (V3.4b T12): the
    // scene-side PlaneObject carries {image asset ref, transform} and the
    // PlaneMapper binds the shared unit quad — no hand-assembled
    // render::PlaneInstance anywhere.
    scene::PlaneObject appSlicePlane;
    appSlicePlane.image = &sliceImage;
    appSlicePlane.transform = app::makeSliceModel(sliceImage);
    auto mappedPlane = planeMapper->map(appSlicePlane,
                                        scene::TranslateContext{});
    ASSERT_TRUE(mappedPlane.ok()) << mappedPlane.error().message;
    render::PlaneScene sliceScene;
    sliceScene.planes.push_back(*mappedPlane);

    // The composed slice view: slice-image layer FIRST, GPU contour SECOND,
    // through render::View's type-erased drawLayer dispatch — the sample's
    // renderFrame structure at the sample's shared camera/model functions.

    render::View view(render::ViewRect{0, 0,
                                       static_cast<int>(kTargetWidth),
                                       static_cast<int>(kTargetHeight)},
                      glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    view.setCamera(camera);
    render::PlaneRenderer planeRenderer;
    view.addItem(sliceScene, &planeRenderer);
    view.addItem(contourScene, &renderer);

    core::DrawContext ctx;
    auto rendered = view.renderWithEnsure(ctx);
    ASSERT_TRUE(rendered.ok()) << rendered.error().message;
    EXPECT_FALSE(core::hasPendingGlError());

    // Read back the view's own ViewTarget (still bound after render).
    ASSERT_NE(view.target(), nullptr);
    view.target()->framebuffer().bind();
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read =
        reader.read(0u, 0u, kTargetWidth, kTargetHeight, pixels);
    ASSERT_TRUE(read.ok()) << read.error().message;

    // Spot checks (explainable positions, same analytic frame as test (1)):
    //  - pixel (16,16): center ~0.71 px from the corner -> inside a stroke ->
    //    contour red within 1/255;
    //  - pixel (32,32): deep INSIDE the cross-section (16 px from the
    //    boundary, no stroke) -> the SLICE LAYER's exact bytes (40,40,40,255),
    //    proving the second layer did not erase the first;
    //  - pixel (0,0): also the slice bytes — the slice quad covers the WHOLE
    //    viewport (ortho [0,64]^2 -> [0,64]^2), so the View clear color is
    //    fully overdrawn by design; both corner probes together pin the
    //    slice-image extent edge-to-edge.
    const auto at = [&](std::uint32_t px, std::uint32_t py) {
        return (static_cast<std::size_t>(py) * kTargetWidth + px) * 4u;
    };
    EXPECT_TRUE(isRedPixel(pixels, at(16u, 16u)))
        << "stroke pixel (16,16) must be contour red through the composed view";
    EXPECT_NEAR(pixels[at(32u, 32u) + 0u], kSliceByte, kColorTolerance);
    EXPECT_NEAR(pixels[at(32u, 32u) + 1u], kSliceByte, kColorTolerance);
    EXPECT_NEAR(pixels[at(32u, 32u) + 2u], kSliceByte, kColorTolerance);
    EXPECT_EQ(pixels[at(32u, 32u) + 3u], 255u)
        << "slice image byte visible beneath the contour layer";
    for (int c = 0; c < 3; ++c) {
        EXPECT_NEAR(pixels[at(0u, 0u) + static_cast<std::size_t>(c)],
                    kSliceByte, kColorTolerance)
            << "channel " << c << ": slice image reaches the viewport corner";
    }
    EXPECT_EQ(pixels[at(0u, 0u) + 3u], 255u);

    // FR-app.3 acceptance through the composed path: >= 90% of the 2 px band
    // around the SAME analytic rectangle [16,48]^2 matches the contour color
    // within 1/255. The band count is the same closed-form 508 pixels as in
    // test (1): identical geometry (1:1 ortho mapping of the 64x64 image onto
    // the 64x64 ViewTarget), now reached via View/drawLayer/makeSliceCamera.
    std::size_t inBand = 0u;
    std::size_t matched = 0u;
    for (std::uint32_t py = 0; py < kTargetHeight; ++py) {
        for (std::uint32_t px = 0; px < kTargetWidth; ++px) {
            const glm::vec2 center(static_cast<float>(px) + 0.5f,
                                   static_cast<float>(py) + 0.5f);
            if (distanceToRectangle(center, kRectangleCorners) <=
                kAcceptanceBandPx) {
                ++inBand;
                if (isRedPixel(pixels, at(px, py))) {
                    ++matched;
                }
            }
        }
    }
    EXPECT_EQ(inBand, 508u)
        << "same closed-form band count as the direct-render gate";
    const double fraction =
        static_cast<double>(matched) / static_cast<double>(inBand);
    EXPECT_GE(fraction, kMinMatchFraction)
        << "matched " << matched << " of " << inBand
        << " in-band pixels through the SAMPLE composition path (FR-app.3)";
}

// ---------------------------------------------------------------------------
// (1a) T11 review regression — pin each view's display-model PERMUTATION on an
//      asymmetric probe, so a column-major transposition can never hide behind
//      the cube-symmetric analytic frame of test (1) again (the live Sagittal
//      contour was misplaced half off-screen by exactly such a transposition).
// ---------------------------------------------------------------------------

TEST(T15Mpr, AxisDisplayModelsPinPermutationNotTranspose) {
    const std::array<glm::mat4, 3> models = axisDisplayModels();
    // Probe voxel point (1,2,3); each view's display mapping is pinned to the
    // documented free-axis convention (mpr_slice.hpp / mpr_sample.cpp):
    const std::array<glm::vec3, 3> expectedDisplay = {
        glm::vec3(1.0f, 2.0f, 3.0f), // Transverse: (x,y,z) -> (x,y,z)
        glm::vec3(1.0f, 3.0f, 2.0f), // Coronal:    (x,y,z) -> (x,z,y)
        glm::vec3(2.0f, 3.0f, 1.0f), // Sagittal:   (x,y,z) -> (y,z,x)
    };
    const std::array<const char*, 3> names = {"Transverse", "Coronal",
                                              "Sagittal"};
    for (std::size_t i = 0u; i < models.size(); ++i) {
        SCOPED_TRACE(names[i]);
        const glm::vec3 out(models[i] * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f));
        EXPECT_NEAR(out.x, expectedDisplay[i].x, 1e-6f) << "display x";
        EXPECT_NEAR(out.y, expectedDisplay[i].y, 1e-6f) << "display y";
        EXPECT_NEAR(out.z, expectedDisplay[i].z, 1e-6f) << "display z";
    }
}

// ---------------------------------------------------------------------------
// (1b) broker::ContourMapper — RE-minimal translation + typed errors.
// ---------------------------------------------------------------------------

TEST(T15Mpr, ContourMapperTranslatesSceneToRender) {
    const data::Mesh box = app::makeBoxMesh(kBoxMin, kBoxMax);
    render::AssetRegistry registry;
    broker::ContourMapper mapper(&registry);
    ASSERT_EQ(registry.slotCount(), 0u);

    scene::ContourObject appContour;
    appContour.mesh = &box;
    appContour.transform = glm::translate(glm::mat4(1.0f),
                                          glm::vec3(1.0f, 2.0f, 3.0f));
    appContour.plane.setNormal(glm::vec3(0.0f, 1.0f, 0.0f));
    appContour.plane.setPoint(glm::vec3(0.0f, 32.5f, 0.0f));
    appContour.setColor(glm::vec4(0.25f, 0.5f, 0.75f, 1.0f));

    auto first = mapper.map(appContour, scene::TranslateContext{});
    ASSERT_TRUE(first.ok()) << first.error().message;

    // RE-minimal payload: AssetHandle + ClipPlane + color + model only, each
    // carried across unchanged (plane World-space 1:1).
    EXPECT_FALSE(first->mesh.isNull());
    EXPECT_NEAR(first->plane.normal.y, 1.0f, 1e-6f) << "unit plane normal";
    EXPECT_NEAR(first->plane.point.y, 32.5f, 1e-6f) << "plane point carried";
    EXPECT_NEAR(first->color.r, 0.25f, 1e-6f) << "stroke color r";
    EXPECT_NEAR(first->color.a, 1.0f, 1e-6f) << "stroke color a";
    EXPECT_NEAR(first->model[3][0], 1.0f, 1e-6f) << "model translation x";
    EXPECT_NEAR(first->model[3][2], 3.0f, 1e-6f) << "model translation z";

    // Mapping the SAME CPU mesh again dedups to the existing GPU object
    // (registry identity dedup, SPEC §9 V2.5).
    auto second = mapper.map(appContour, scene::TranslateContext{});
    ASSERT_TRUE(second.ok()) << second.error().message;
    EXPECT_EQ(registry.slotCount(), 1u)
        << "one GPU object per individual CPU mesh";
    EXPECT_EQ(first->mesh.index, second->mesh.index);
    EXPECT_EQ(first->mesh.generation, second->mesh.generation);

    // Typed error (code 1): null mesh pointer — never a crash (SPEC §5).
    scene::ContourObject nullMesh;
    auto errNullMesh = mapper.map(nullMesh, scene::TranslateContext{});
    ASSERT_TRUE(errNullMesh.failed());
    EXPECT_EQ(errNullMesh.error().code, 1);

    // Typed error (code 3): Space::VoxelIndex planes need the volume-context
    // conversion ContourMapper deliberately does not perform — never a silent
    // identity map (SPEC §5).
    scene::ContourObject voxelContour = appContour;
    voxelContour.plane.setSpace(scene::Space::VoxelIndex);
    auto errVoxel = mapper.map(voxelContour, scene::TranslateContext{});
    ASSERT_TRUE(errVoxel.failed());
    EXPECT_EQ(errVoxel.error().code, 3);

    // Typed error (code 2): mapper without a registry cannot place assets.
    broker::ContourMapper orphanMapper(nullptr);
    auto errOrphan = orphanMapper.map(appContour, scene::TranslateContext{});
    ASSERT_TRUE(errOrphan.failed());
    EXPECT_EQ(errOrphan.error().code, 2);
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

    // The scene carries the box's AssetHandle (SPEC §9 V2.5), resolved by the
    // renderer through the shared registry.
    render::AssetRegistry registry;
    const auto handle = registry.registerAsset(box);
    ASSERT_TRUE(handle.ok()) << handle.error().message;

    app::MprSliceState state;
    state.transverseZ = kTransverseZ;
    state.coronalY = kCoronalY;
    state.sagittalX = kSagittalX;
    const render::Camera camera =
        app::make3dCamera(state, box.bounds(), kAspect);

    render::MeshScene scene;
    scene.meshes.push_back(
        render::MeshInstance{*handle, &material, glm::mat4(1.0f)});

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::RenderTarget rt;
    rt.framebuffer = &target.framebuffer;
    rt.width = kTargetWidth;
    rt.height = kTargetHeight;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    render::MeshRenderer renderer(&registry, nullptr);
    auto result = renderer.render(scene, camera, rt);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(core::hasPendingGlError());

    // Read back the center pixel from the still-bound target framebuffer.
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read = reader.read(kCenterX, kCenterY, 1u, 1u, pixels);
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
