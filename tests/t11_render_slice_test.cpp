// tests/t11_render_slice_test.cpp — T11 gate tests (FR-render.4, SPEC §4).
//
// Asserts:
//   (1) the cross-section vertices emitted by the SliceRenderer geometry shader
//       all lie on the clip plane: the analytic distance from each emitted
//       vertex to the plane is <= 1e-4 relative (SPEC §4 plane-geometry
//       tolerance), and the emitted vertex count matches the hand-counted
//       value for the golden cube (FR-render.4);
//   (2) the clipped mesh renders correctly on a known mesh: a cube
//       `[-1,1]^3` clipped by the plane z=0 (kept side z >= 0), viewed head-on
//       from +Z with an orthographic camera, renders the material's base color
//       at the viewport center. The geometry shader shades with the triangle's
//       geometric (face) normal computed from the world-space winding, so the
//       kept z=+1 face (geometric normal exactly +Z) shades to exactly the base
//       color under the deterministic v1 flat lighting (docs/render.md).
//
// Golden cube (data authored in this file): 8 corners of `[-1,1]^3`, 12
// triangles. Clip plane z=0 (normal (0,0,1), point (0,0,0)). The cross-section
// is the square `[-1,1]^2` at z=0. Exactly 8 of the 12 triangles cross the
// plane (the 4 vertical faces x=±1, y=±1 contribute 2 triangles each; the
// z=±1 faces do not cross). Each crossing triangle emits a degenerate
// cross-section triangle (3 vertices) -> 8 * 3 = 24 emitted vertices, all on
// the plane (docs/render.md).
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers (including utils::PixelReader for pixel readback and
// core::TransformFeedback for the cross-section capture) — no raw glXxx calls.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "utils/pixel_reader.hpp"
#include "core/texture2d.hpp"
#include "data/mesh.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "render/slice_renderer.hpp"
#include "render/view.hpp"
#include "tests/offscreen_fixture.hpp"
#include "tests/test_helpers.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants (FR-render.4).
// ---------------------------------------------------------------------------

// The material's base color: a clean solid color mapping to exact RGBA8 bytes
// (0.2*255=51, 0.4*255=102, 0.8*255=204), so the expected center pixel is
// unambiguous. Alpha 1.0 => opaque.
constexpr glm::vec4 kBaseColor(0.2f, 0.4f, 0.8f, 1.0f);
constexpr std::uint8_t kExpectedR = 51u;
constexpr std::uint8_t kExpectedG = 102u;
constexpr std::uint8_t kExpectedB = 204u;

// Target framebuffer size: 64x64.
constexpr std::uint32_t kTargetWidth = 64u;
constexpr std::uint32_t kTargetHeight = 64u;
constexpr std::uint32_t kCenterX = kTargetWidth / 2u;  // 32
constexpr std::uint32_t kCenterY = kTargetHeight / 2u; // 32

// The color tolerance: 1/255 per FR-render.1/4.
constexpr int kColorTolerance = 1;

// The clip plane: z = 0 (normal +Z through the origin), cutting the cube in
// half. Kept side is z >= 0.
constexpr glm::vec3 kPlaneNormal(0.0f, 0.0f, 1.0f);
constexpr glm::vec3 kPlanePoint(0.0f, 0.0f, 0.0f);

// Characteristic extent of the cube (max dimension of `[-1,1]^3` is 2): used
// to express the plane-distance tolerance as a RELATIVE 1e-4 of the geometry,
// so the tolerance stays meaningful if the cube is ever rescaled.
constexpr float kMeshExtent = 2.0f;
constexpr float kRelativeTolerance = 1e-4f;
constexpr float kPlaneDistanceTolerance = kRelativeTolerance * kMeshExtent;

// Hand-counted number of cross-section vertices the golden cube emits: 8
// crossing triangles x 3 vertices per degenerate cross-section triangle = 24.
constexpr std::size_t kExpectedCrossSectionVertices = 24u;

// ---------------------------------------------------------------------------
// Test helpers.
// ---------------------------------------------------------------------------

/// Build the golden cube mesh `[-1,1]^3`: 8 corners, 12 outward-facing
/// triangles.
data::Mesh makeCubeMesh() {
    std::vector<glm::vec3> positions = {
        glm::vec3(-1.0f, -1.0f, -1.0f), // v0
        glm::vec3(1.0f, -1.0f, -1.0f),  // v1
        glm::vec3(1.0f, 1.0f, -1.0f),   // v2
        glm::vec3(-1.0f, 1.0f, -1.0f),  // v3
        glm::vec3(-1.0f, -1.0f, 1.0f),  // v4
        glm::vec3(1.0f, -1.0f, 1.0f),   // v5
        glm::vec3(1.0f, 1.0f, 1.0f),    // v6
        glm::vec3(-1.0f, 1.0f, 1.0f),   // v7
    };
    // 12 triangles, CCW outward (from each face's outward side), so the
    // geometric face normal (computed from the winding) points outward: the
    // back face (z = +1) has geometric normal +Z, rendering at exactly the
    // base color under the deterministic +Z light.
    std::vector<std::uint32_t> indices = {
        0u, 3u, 2u, 0u, 2u, 1u, // front (z = -1), outward -Z
        5u, 6u, 7u, 5u, 7u, 4u, // back  (z = +1), outward +Z
        1u, 6u, 5u, 1u, 2u, 6u, // right (x = +1), outward +X
        0u, 7u, 3u, 0u, 4u, 7u, // left  (x = -1), outward -X
        3u, 6u, 2u, 3u, 7u, 6u, // top   (y = +1), outward +Y
        0u, 1u, 5u, 0u, 5u, 4u, // bottom (y = -1), outward -Y
    };
    return data::Mesh::fromTriangles(std::move(positions), std::move(indices));
}

/// The default camera: eye at (0,0,5) looking down -Z at the origin, with an
/// orthographic projection mapping NDC [-1,1]^2 onto the full viewport. From
/// +Z the clipped cube's kept z=+1 face is visible head-on.
/// Build a color-only render target (64x64) bound for readback.
struct RenderedTarget {
    core::Texture2D color;
    core::Framebuffer framebuffer;

    RenderedTarget(core::Texture2D color, core::Framebuffer framebuffer)
        : color(std::move(color)), framebuffer(std::move(framebuffer)) {}
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

} // namespace

// ---------------------------------------------------------------------------
// (1) FR-render.4 — emitted cross-section vertices all lie on the clip plane.
// ---------------------------------------------------------------------------

TEST(T11RenderSlice, CrossSectionVerticesLieOnClipPlane) {
    data::Mesh cube = makeCubeMesh();
    auto registry = std::make_shared<render::AssetRegistry>();
    const auto handle = registry->registerAsset(cube);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    auto material =
        std::make_shared<render::PhongMaterial>(kBaseColor);
    render::SliceScene scene;
    scene.meshes.push_back(
        render::MeshInstance{*handle, material, glm::mat4(1.0f)});

    render::ClipPlane plane;
    plane.normal = kPlaneNormal;
    plane.point = kPlanePoint;

    render::SliceRenderer renderer(registry);

    // The capture pass draws (its fragment shader discards) into the currently
    // bound framebuffer, so bind an offscreen target to keep the draw valid on
    // the surfaceless EGL fallback.
    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    target.framebuffer.bind();

    std::vector<glm::vec3> vertices;
    auto capture = renderer.captureCrossSection(scene, plane, vertices);
    ASSERT_TRUE(capture.ok()) << capture.error().message;

    // Hand-counted explainable constant: 8 crossing triangles x 3 vertices.
    EXPECT_EQ(vertices.size(), kExpectedCrossSectionVertices)
        << "cross-section vertex count for the golden cube";

    // Every emitted vertex must lie on the clip plane z=0 within 1e-4 relative
    // (SPEC §4 plane-geometry tolerance): |dot(normal, v - point)| <=
    // 1e-4 * extent.
    for (std::size_t i = 0u; i < vertices.size(); ++i) {
        const glm::vec3& v = vertices[i];
        const float distance =
            glm::abs(glm::dot(kPlaneNormal, v - kPlanePoint));
        EXPECT_LE(distance, kPlaneDistanceTolerance)
            << "vertex " << i << " (" << v.x << ", " << v.y << ", " << v.z
            << ") must lie on the clip plane";
    }
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (2) FR-render.4 — clipped mesh renders correctly on the golden cube.
// ---------------------------------------------------------------------------

TEST(T11RenderSlice, ClippedMeshRendersCorrectly) {
    data::Mesh cube = makeCubeMesh();
    auto registry = std::make_shared<render::AssetRegistry>();
    const auto handle = registry->registerAsset(cube);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    auto material =
        std::make_shared<render::PhongMaterial>(kBaseColor);
    render::SliceScene scene;
    scene.meshes.push_back(
        render::MeshInstance{*handle, material, glm::mat4(1.0f)});

    render::ClipPlane plane;
    plane.normal = kPlaneNormal;
    plane.point = kPlanePoint;

    // T3a: SliceRenderer::render deleted — port via render::View +
    // REContext::current().beginPass + View::addItem (View owns beginPass).
    auto renderer = std::make_shared<render::SliceRenderer>(registry);
    render::Camera camera = makeCamera();
    // View path: put plane into scene's plane field for drawLayer without explicit plane param
    render::SliceScene viewScene = scene;
    viewScene.plane = plane;
    render::View view(render::ViewRect{0, 0, static_cast<int>(kTargetWidth), static_cast<int>(kTargetHeight)},
                      glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    view.setCamera(camera);
    view.addItem(viewScene, renderer);
    ASSERT_TRUE(view.ensureTarget().ok());
    auto result = view.render();
    ASSERT_TRUE(result.ok()) << result.error().message;

    // Read back the center pixel from the View's target.
    view.target()->framebuffer().bind();
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read = reader.read(kCenterX, kCenterY, 1u, 1u, pixels);
    ASSERT_TRUE(read.ok()) << read.error().message;
    ASSERT_EQ(pixels.size(), 4u);
    view.target()->framebuffer().unbind();

    // The kept z=+1 face's normal is +Z, so the deterministic v1 flat lighting
    // shades it to exactly the base color: {51, 102, 204}.
    EXPECT_NEAR(pixels[0], kExpectedR, kColorTolerance) << "R channel";
    EXPECT_NEAR(pixels[1], kExpectedG, kColorTolerance) << "G channel";
    EXPECT_NEAR(pixels[2], kExpectedB, kColorTolerance) << "B channel";
    EXPECT_FALSE(core::hasPendingGlError());
}

} // namespace re::tests
