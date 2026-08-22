// tests/t1_v2_ir_dispatch_test.cpp — V2 T1 gate tests (SPEC §9 V2.3).
//
// The V2.3 deliverable: `Camera`/`RenderTarget` moved out of mesh_renderer.hpp
// into the shared `render/types.hpp`, plus the pure abstract `IRenderer::render`
// contract implemented by all four per-technique renderers
// (Mesh/Plane/Volume/SliceRenderer). This file verifies
//
//   (1) compile-time: the `Scene` dispatch variant exists with exactly one
//       alternative per technique, and all four renderers implement the
//       abstract `IRenderer` contract;
//   (2) dispatch through `IRenderer&` renders each technique's golden scene
//       with the EXACT same acceptance constants as the direct concrete calls
//       (regression lock R3 — renderers unchanged in output):
//       - MeshRenderer:   golden quad, FR-render.1 center {51, 102, 204};
//       - PlaneRenderer:  solid 64x64 image, FR-render.5 center
//                         {51, 102, 204, 255};
//       - VolumeRenderer: 2x2x2 uniform volume + constant-green TF,
//                         FR-render.6 center {0, 239, 0, 239};
//       - SliceRenderer:  golden cube clipped at plane z=0 carried by the
//                         scene, FR-render.4 center {51, 102, 204};
//   (3) the SliceRenderer dispatch path uses the plane carried by the scene
//       (a plane z=2 above the cube clips everything away -> clear color at
//       the center pixel), and
//   (4) dispatching a scene of the wrong technique to a renderer returns a
//       typed error (SPEC §5, no exceptions), never a crash.
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers (including core::readRgba8 for pixel readback) — no raw glXxx calls.

#include <gtest/gtest.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "core/read_pixels.hpp"
#include "core/texture2d.hpp"
#include "data/image.hpp"
#include "data/mesh.hpp"
#include "data/volume_dataset.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "render/plane_renderer.hpp"
#include "render/slice_renderer.hpp"
#include "render/types.hpp"
#include "render/volume_renderer.hpp"
#include "tests/offscreen_fixture.hpp"
#include "volume/color.hpp"
#include "volume/transfer_function.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants (the four techniques' established acceptance values,
// docs/render.md FR-render.1/4/5/6).
// ---------------------------------------------------------------------------

// The material's base color: a clean solid color mapping to exact RGBA8 bytes
// (0.2*255=51, 0.4*255=102, 0.8*255=204). Alpha 1.0 => opaque.
constexpr glm::vec4 kBaseColor(0.2f, 0.4f, 0.8f, 1.0f);

// FR-render.1/4 center-pixel bytes (the golden quad / the clipped cube's kept
// +Z face render at exactly the base color under the deterministic v1 flat +Z
// lighting).
constexpr std::uint8_t kExpectedR = 51u;
constexpr std::uint8_t kExpectedG = 102u;
constexpr std::uint8_t kExpectedB = 204u;

// FR-render.6 center-pixel bytes: round(0.9375*255) = 239 for the G and A
// channels of the analytic front-to-back composite of four {0,1,0,0.5} samples
// (docs/render.md).
constexpr std::uint8_t kVolumeExpectedG = 239u;
constexpr std::uint8_t kVolumeExpectedA = 239u;

// Target framebuffer size: 64x64 (the golden scenes cover the full viewport).
constexpr std::uint32_t kTargetWidth = 64u;
constexpr std::uint32_t kTargetHeight = 64u;
constexpr std::uint32_t kCenterX = kTargetWidth / 2u;  // 32
constexpr std::uint32_t kCenterY = kTargetHeight / 2u; // 32

// The color tolerance: 1/255 per FR-render.1/4/5/6.
constexpr int kColorTolerance = 1;

// The typed error code the IRenderer dispatch uses for a scene of the wrong
// technique (SPEC §5; distinct from code 1 used for invalid target sizes).
constexpr int kWrongSceneKindErrorCode = 2;

// The clip plane of the golden FR-render.4 setup: z = 0 (normal +Z through the
// origin, kept side z >= 0).
constexpr glm::vec3 kPlaneNormal(0.0f, 0.0f, 1.0f);
constexpr glm::vec3 kPlanePoint(0.0f, 0.0f, 0.0f);

// ---------------------------------------------------------------------------
// Test helpers (mirroring the T7/T8/T9/T11 gate setups).
// ---------------------------------------------------------------------------

/// Build a golden +Z-facing quad mesh covering [-1,1]^2 at z=0 (two triangles).
data::Mesh makeQuadMesh() {
    std::vector<glm::vec3> positions = {
        glm::vec3(-1.0f, -1.0f, 0.0f), // v0
        glm::vec3(1.0f, -1.0f, 0.0f),  // v1
        glm::vec3(1.0f, 1.0f, 0.0f),   // v2
        glm::vec3(-1.0f, 1.0f, 0.0f),  // v3
    };
    std::vector<std::uint32_t> indices = {0u, 1u, 2u, 0u, 2u, 3u};
    return data::Mesh::fromTriangles(std::move(positions), std::move(indices));
}

/// Build the golden cube mesh `[-1,1]^3`: 8 corners, 12 outward-facing
/// triangles (the kept z=+1 face has geometric normal +Z, FR-render.4).
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

/// A 64x64 solid image of the base color bytes {51,102,204,255} (FR-render.5:
/// the quad maps 1:1 onto the viewport, so the center pixel samples the solid
/// color exactly).
data::Image makeSolidImage() {
    std::vector<std::uint8_t> bytes(kTargetWidth * kTargetHeight * 4u, 0u);
    for (std::size_t i = 0u; i + 3u < bytes.size(); i += 4u) {
        bytes[i + 0u] = kExpectedR;
        bytes[i + 1u] = kExpectedG;
        bytes[i + 2u] = kExpectedB;
        bytes[i + 3u] = 255u;
    }
    return data::Image(static_cast<int>(kTargetWidth),
                       static_cast<int>(kTargetHeight), 4, std::move(bytes));
}

/// The unit volume dataset: 2x2x2, all voxels = 0.5 (FR-render.6).
data::VolumeDataset makeUniformDataset() {
    std::vector<float> voxels(2u * 2u * 2u, 0.5f);
    return data::VolumeDataset(2u, 2u, 2u, std::move(voxels));
}

/// The constant-green transfer function (FR-vol.1): {0,1,0,0.5} (straight RGBA)
/// at both control points, so the ramp is constant.
volume::TransferFunction makeGreenTransferFunction() {
    std::vector<volume::TransferFunction::ControlPoint> points;
    points.push_back({0.0f, volume::RgbaColor{0.0f, 1.0f, 0.0f, 0.5f}});
    points.push_back({1.0f, volume::RgbaColor{0.0f, 1.0f, 0.0f, 0.5f}});
    return volume::TransferFunction(std::move(points));
}

/// The default camera (mesh/plane/slice): eye at (0,0,5) looking down -Z at the
/// origin, orthographic projection mapping NDC [-1,1]^2 onto the full viewport.
render::Camera makeCamera() {
    render::Camera camera;
    camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0.0f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    return camera;
}

/// The volume camera (FR-render.6): eye at (0.5,0.5,5) looking down -Z at the
/// box center (0.5,0.5,0.5), orthographic projection mapping NDC [-1,1]^2 onto
/// the full viewport.
render::Camera makeVolumeCamera() {
    render::Camera camera;
    camera.position = glm::vec3(0.5f, 0.5f, 5.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0.5f, 0.5f, 0.5f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    return camera;
}

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

/// Dispatch `scene` through the `IRenderer` contract into the target and read
/// back the single pixel at (x, y). `x`/`y` are readback coordinates (y = 0 is
/// the bottom scanline).
std::vector<std::uint8_t> dispatchAndReadPixel(render::IRenderer& renderer,
                                               const render::Scene& scene,
                                               const render::Camera& camera,
                                               RenderedTarget& target,
                                               std::uint32_t x,
                                               std::uint32_t y) {
    render::RenderTarget rt;
    rt.framebuffer = &target.framebuffer;
    rt.width = kTargetWidth;
    rt.height = kTargetHeight;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    auto result = renderer.render(scene, camera, rt);
    EXPECT_TRUE(result.ok()) << result.error().message;

    std::vector<std::uint8_t> pixels;
    auto read = core::readRgba8(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    return pixels;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) Compile-time: the Scene variant and the IRenderer contract.
// ---------------------------------------------------------------------------

TEST(T1V2IrDispatch, SceneVariantHasOneAlternativePerTechnique) {
    // The dispatch payload covers exactly the four supported techniques.
    static_assert(std::variant_size_v<render::Scene> == 4);
    // IRenderer is a pure abstract contract: it cannot be instantiated, and
    // every per-technique renderer implements it.
    static_assert(std::is_abstract_v<render::IRenderer>);
    static_assert(std::is_base_of_v<render::IRenderer, render::MeshRenderer>);
    static_assert(std::is_base_of_v<render::IRenderer, render::PlaneRenderer>);
    static_assert(
        std::is_base_of_v<render::IRenderer, render::VolumeRenderer>);
    static_assert(std::is_base_of_v<render::IRenderer, render::SliceRenderer>);
}

TEST(T1V2IrDispatch, SceneVariantHoldsTheScenePointer) {
    // The variant stores the exact pointer it was given (round-trip).
    render::MeshScene meshScene;
    const render::Scene scene = &meshScene;
    const render::MeshScene* const* held =
        std::get_if<const render::MeshScene*>(&scene);
    ASSERT_NE(held, nullptr);
    EXPECT_EQ(*held, &meshScene);
}

// ---------------------------------------------------------------------------
// (2) FR-render.1 — MeshRenderer dispatch through IRenderer: the golden quad
//     renders the base color at the center pixel (regression lock R3).
// ---------------------------------------------------------------------------

TEST(T1V2IrDispatch, MeshDispatchRendersGoldenQuad) {
    render::PhongMaterial material(kBaseColor);
    data::Mesh quad = makeQuadMesh();
    render::MeshScene scene;
    scene.meshes.push_back(
        render::MeshInstance{&quad, &material, glm::mat4(1.0f)});

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::MeshRenderer renderer;
    render::IRenderer& iface = renderer;
    const render::Scene dispatchScene = &scene;

    const std::vector<std::uint8_t> pixel =
        dispatchAndReadPixel(iface, dispatchScene, makeCamera(), target,
                             kCenterX, kCenterY);

    EXPECT_NEAR(pixel[0], kExpectedR, kColorTolerance) << "R channel";
    EXPECT_NEAR(pixel[1], kExpectedG, kColorTolerance) << "G channel";
    EXPECT_NEAR(pixel[2], kExpectedB, kColorTolerance) << "B channel";
    EXPECT_EQ(pixel[3], 255u) << "alpha channel (opaque)";
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (3) FR-render.5 — PlaneRenderer dispatch through IRenderer: the solid image
//     renders its color at the center pixel.
// ---------------------------------------------------------------------------

TEST(T1V2IrDispatch, PlaneDispatchRendersSolidImage) {
    data::Image image = makeSolidImage();
    render::PlaneGeometry geometry = render::PlaneGeometry::unitQuadXY();
    render::PlaneScene scene;
    scene.planes.push_back(
        render::PlaneInstance{&geometry, &image, glm::mat4(1.0f)});

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::PlaneRenderer renderer;
    render::IRenderer& iface = renderer;
    const render::Scene dispatchScene = &scene;

    const std::vector<std::uint8_t> pixel =
        dispatchAndReadPixel(iface, dispatchScene, makeCamera(), target,
                             kCenterX, kCenterY);

    EXPECT_NEAR(pixel[0], kExpectedR, kColorTolerance) << "R channel";
    EXPECT_NEAR(pixel[1], kExpectedG, kColorTolerance) << "G channel";
    EXPECT_NEAR(pixel[2], kExpectedB, kColorTolerance) << "B channel";
    EXPECT_NEAR(pixel[3], 255u, kColorTolerance) << "A channel";
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (4) FR-render.6 — VolumeRenderer dispatch through IRenderer: the analytic
//     ray-cast center pixel {0, 239, 0, 239}.
// ---------------------------------------------------------------------------

TEST(T1V2IrDispatch, VolumeDispatchRendersAnalyticRayCast) {
    data::VolumeDataset dataset = makeUniformDataset();
    volume::TransferFunction tf = makeGreenTransferFunction();
    render::VolumeInstance instance{&dataset, &tf, glm::mat4(1.0f)};
    render::VolumeScene scene;
    scene.volumes.push_back(instance);

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::VolumeRenderer renderer;
    render::IRenderer& iface = renderer;
    const render::Scene dispatchScene = &scene;

    const std::vector<std::uint8_t> pixel =
        dispatchAndReadPixel(iface, dispatchScene, makeVolumeCamera(), target,
                             kCenterX, kCenterY);

    EXPECT_EQ(pixel[0], 0u) << "R channel";
    EXPECT_NEAR(pixel[1], kVolumeExpectedG, kColorTolerance) << "G channel";
    EXPECT_EQ(pixel[2], 0u) << "B channel";
    EXPECT_NEAR(pixel[3], kVolumeExpectedA, kColorTolerance) << "A channel";
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (5) FR-render.4 — SliceRenderer dispatch through IRenderer: the golden cube
//     clipped against the plane CARRIED BY THE SCENE (z=0) renders the base
//     color at the center pixel.
// ---------------------------------------------------------------------------

TEST(T1V2IrDispatch, SliceDispatchRendersClippedCube) {
    data::Mesh cube = makeCubeMesh();
    render::PhongMaterial material(kBaseColor);
    render::SliceScene scene;
    scene.meshes.push_back(
        render::MeshInstance{&cube, &material, glm::mat4(1.0f)});
    scene.plane.normal = kPlaneNormal;
    scene.plane.point = kPlanePoint;

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::SliceRenderer renderer;
    render::IRenderer& iface = renderer;
    const render::Scene dispatchScene = &scene;

    const std::vector<std::uint8_t> pixel =
        dispatchAndReadPixel(iface, dispatchScene, makeCamera(), target,
                             kCenterX, kCenterY);

    // The kept z=+1 face's geometric normal is +Z -> shade 1 -> base color.
    EXPECT_NEAR(pixel[0], kExpectedR, kColorTolerance) << "R channel";
    EXPECT_NEAR(pixel[1], kExpectedG, kColorTolerance) << "G channel";
    EXPECT_NEAR(pixel[2], kExpectedB, kColorTolerance) << "B channel";
    EXPECT_FALSE(core::hasPendingGlError());
}

TEST(T1V2IrDispatch, SliceDispatchUsesSceneCarriedPlane) {
    data::Mesh cube = makeCubeMesh();
    render::PhongMaterial material(kBaseColor);
    render::SliceScene scene;
    scene.meshes.push_back(
        render::MeshInstance{&cube, &material, glm::mat4(1.0f)});
    // Plane z = 2 (normal +Z, point (0,0,2); kept side z >= 2). The cube tops
    // out at z = 1 < 2, so every triangle is clipped away and the target stays
    // at its clear color (transparent black) — proving the dispatch path uses
    // the plane carried by the scene, not a fixed default.
    scene.plane.normal = kPlaneNormal;
    scene.plane.point = glm::vec3(0.0f, 0.0f, 2.0f);

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::SliceRenderer renderer;
    render::IRenderer& iface = renderer;
    const render::Scene dispatchScene = &scene;

    const std::vector<std::uint8_t> pixel =
        dispatchAndReadPixel(iface, dispatchScene, makeCamera(), target,
                             kCenterX, kCenterY);

    EXPECT_EQ(pixel[0], 0u) << "R channel (clear color)";
    EXPECT_EQ(pixel[1], 0u) << "G channel (clear color)";
    EXPECT_EQ(pixel[2], 0u) << "B channel (clear color)";
    EXPECT_EQ(pixel[3], 0u) << "A channel (clear color)";
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (6) The dispatch contract rejects a scene of the wrong technique with a
//     typed error (SPEC §5, no exceptions).
// ---------------------------------------------------------------------------

TEST(T1V2IrDispatch, MeshRendererRejectsWrongSceneKind) {
    data::Image image = makeSolidImage();
    render::PlaneGeometry geometry = render::PlaneGeometry::unitQuadXY();
    render::PlaneScene planeScene;
    planeScene.planes.push_back(
        render::PlaneInstance{&geometry, &image, glm::mat4(1.0f)});

    render::MeshRenderer renderer;
    render::IRenderer& iface = renderer;
    const render::Scene dispatchScene = &planeScene; // a PlaneScene, not a MeshScene

    render::RenderTarget rt; // never reached: the mismatch aborts before any draw
    const data::Result<void> result = iface.render(dispatchScene, makeCamera(), rt);

    EXPECT_TRUE(result.failed());
    EXPECT_EQ(result.error().code, kWrongSceneKindErrorCode);
    EXPECT_NE(result.error().message.find("does not hold a MeshScene"),
              std::string::npos);
}

TEST(T1V2IrDispatch, NullSceneDispatchRejectedByEveryRenderer) {
    // The default-constructed Scene is the documented "no scene" payload
    // (render/types.hpp): every alternative is a null pointer. The contract
    // promises a typed error (code 2) from each renderer — never a crash.
    const render::Scene nullScene; // all alternatives null

    render::MeshRenderer mesh;
    render::PlaneRenderer plane;
    render::VolumeRenderer volume;
    render::SliceRenderer slice;
    render::IRenderer* const renderers[] = {&mesh, &plane, &volume, &slice};

    render::RenderTarget rt; // never reached: rejection aborts before any draw
    for (render::IRenderer* renderer : renderers) {
        const data::Result<void> result =
            renderer->render(nullScene, makeCamera(), rt);
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code, kWrongSceneKindErrorCode);
    }
}

} // namespace re::tests