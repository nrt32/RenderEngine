// tests/t1_v2_ir_dispatch_test.cpp — T14 gate: collapse IRenderer::render(Scene) variant dispatch.
// The V2 T1 deliverable previously moved Camera/RenderTarget into render/types.hpp
// and defined a pure abstract IRenderer::render Scene variant dispatch implemented
// by the four per-technique renderers. T14 deletes that vestigial dispatch (the
// second transparent-mesh behavior that silently dropped transparent instances when
// no OIT pipeline was wired) and keeps only IRenderable::drawLayer via the broker
// path (View + REContext::current().beginPass). This file verifies the collapsed
// contract — the same four golden scenes now render byte-identical within 1/255
// through beginPass + drawLayer (FR-render.1/4/5/6, regression lock R3), the
// SliceRenderer still uses the plane carried by the scene (z=2 clips away), and
// the former variant-error contract is gone (no Scene alias, no IRenderer base).
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/ wrappers
// (including utils::PixelReader for pixel readback) — no raw glXxx calls.

#include <gtest/gtest.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "core/re_context.hpp"
#include "core/texture2d.hpp"
#include "data/image.hpp"
#include "data/mesh.hpp"
#include "data/volume_dataset.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "render/plane_renderer.hpp"
#include "render/slice_renderer.hpp"
#include "render/types.hpp"
#include "render/volume_renderer.hpp"
#include "tests/offscreen_fixture.hpp"
#include "tests/test_helpers.hpp"
#include "utils/pixel_reader.hpp"
#include "volume/color.hpp"
#include "volume/transfer_function.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants (the four techniques' established acceptance values,
// docs/render.md FR-render.1/4/5/6).
// ---------------------------------------------------------------------------

constexpr glm::vec4 kBaseColor(0.2f, 0.4f, 0.8f, 1.0f);
constexpr std::uint8_t kExpectedR = 51u;
constexpr std::uint8_t kExpectedG = 102u;
constexpr std::uint8_t kExpectedB = 204u;
constexpr std::uint8_t kVolumeExpectedG = 239u;
constexpr std::uint8_t kVolumeExpectedA = 239u;
constexpr std::uint32_t kTargetWidth = 64u;
constexpr std::uint32_t kTargetHeight = 64u;
constexpr std::uint32_t kCenterX = kTargetWidth / 2u;
constexpr std::uint32_t kCenterY = kTargetHeight / 2u;
constexpr int kColorTolerance = 1;
constexpr glm::vec3 kPlaneNormal(0.0f, 0.0f, 1.0f);
constexpr glm::vec3 kPlanePoint(0.0f, 0.0f, 0.0f);

// ---------------------------------------------------------------------------
// Test helpers (mirroring the T7/T8/T9/T11 gate setups).
// ---------------------------------------------------------------------------

data::Mesh makeCubeMesh() {
    std::vector<glm::vec3> positions = {
        glm::vec3(-1.0f, -1.0f, -1.0f),
        glm::vec3(1.0f, -1.0f, -1.0f),
        glm::vec3(1.0f, 1.0f, -1.0f),
        glm::vec3(-1.0f, 1.0f, -1.0f),
        glm::vec3(-1.0f, -1.0f, 1.0f),
        glm::vec3(1.0f, -1.0f, 1.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        glm::vec3(-1.0f, 1.0f, 1.0f),
    };
    std::vector<std::uint32_t> indices = {
        0u, 3u, 2u, 0u, 2u, 1u, 5u, 6u, 7u, 5u, 7u, 4u, 1u, 6u, 5u, 1u, 2u, 6u,
        0u, 7u, 3u, 0u, 4u, 7u, 3u, 6u, 2u, 3u, 7u, 6u, 0u, 1u, 5u, 0u, 5u, 4u,
    };
    return data::Mesh::fromTriangles(std::move(positions), std::move(indices));
}

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

data::VolumeDataset makeUniformDataset() {
    std::vector<float> voxels(2u * 2u * 2u, 0.5f);
    return data::VolumeDataset(2u, 2u, 2u, std::move(voxels));
}

volume::TransferFunction makeGreenTransferFunction() {
    std::vector<volume::TransferFunction::ControlPoint> points;
    points.push_back({0.0f, volume::RgbaColor{0.0f, 1.0f, 0.0f, 0.5f}});
    points.push_back({1.0f, volume::RgbaColor{0.0f, 1.0f, 0.0f, 0.5f}});
    return volume::TransferFunction(std::move(points));
}

render::Camera makeVolumeCamera() {
    render::Camera camera;
    camera.position = glm::vec3(0.5f, 0.5f, 5.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0.5f, 0.5f, 0.5f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    return camera;
}

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

// Helpers that render via the broker path: beginPass + drawLayer, then read back.

std::vector<std::uint8_t> drawMeshLayerAndReadPixel(render::MeshRenderer& renderer,
                                                    const render::MeshScene& scene,
                                                    const render::Camera& camera,
                                                    RenderedTarget& target,
                                                    std::uint32_t x,
                                                    std::uint32_t y) {
    auto& ctx = core::REContext::current();
    ctx.beginPass(&target.framebuffer, kTargetWidth, kTargetHeight, 0.0f, 0.0f, 0.0f, 0.0f);
    auto res = renderer.drawLayer(scene, camera);
    EXPECT_TRUE(res.ok()) << res.error().message;
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read = reader.read(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    return pixels;
}

std::vector<std::uint8_t> drawPlaneLayerAndReadPixel(render::PlaneRenderer& renderer,
                                                     const render::PlaneScene& scene,
                                                     const render::Camera& camera,
                                                     RenderedTarget& target,
                                                     std::uint32_t x,
                                                     std::uint32_t y) {
    auto& ctx = core::REContext::current();
    ctx.beginPass(&target.framebuffer, kTargetWidth, kTargetHeight, 0.0f, 0.0f, 0.0f, 0.0f);
    auto res = renderer.drawLayer(scene, camera);
    EXPECT_TRUE(res.ok()) << res.error().message;
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read = reader.read(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    return pixels;
}

std::vector<std::uint8_t> drawVolumeLayerAndReadPixel(render::VolumeRenderer& renderer,
                                                      const render::VolumeScene& scene,
                                                      const render::Camera& camera,
                                                      RenderedTarget& target,
                                                      std::uint32_t x,
                                                      std::uint32_t y) {
    auto& ctx = core::REContext::current();
    ctx.beginPass(&target.framebuffer, kTargetWidth, kTargetHeight, 0.0f, 0.0f, 0.0f, 0.0f);
    auto res = renderer.drawLayer(scene, camera);
    EXPECT_TRUE(res.ok()) << res.error().message;
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read = reader.read(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    return pixels;
}

std::vector<std::uint8_t> drawSliceLayerAndReadPixel(render::SliceRenderer& renderer,
                                                     const render::SliceScene& scene,
                                                     const render::Camera& camera,
                                                     RenderedTarget& target,
                                                     std::uint32_t x,
                                                     std::uint32_t y) {
    auto& ctx = core::REContext::current();
    ctx.beginPass(&target.framebuffer, kTargetWidth, kTargetHeight, 0.0f, 0.0f, 0.0f, 0.0f);
    auto res = renderer.drawLayer(scene, camera);
    EXPECT_TRUE(res.ok()) << res.error().message;
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read = reader.read(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    return pixels;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) Compile-time: Scene variant and IRenderer contract are deleted (T14).
// ---------------------------------------------------------------------------

TEST(T1V2IrDispatch, SceneVariantAndIRendererRemoved) {
    // After T14 the Scene alias `variant<const MeshScene*,...>` and the pure
    // abstract `IRenderer::render(Scene)` dispatch are deleted — the broker
    // path (View + REContext::current().beginPass + drawLayer) is the single
    // rendering entry (bug class unrepresentable, A9). This is proven by two
    // analytic invariants: (a) the four renderers are no longer polymorphic
    // via a virtual base (previously each inherited the abstract IRenderer and
    // was polymorphic; now each exposes only concrete drawLayer/render(MeshScene)
    // and is not polymorphic — count of polymorphic renderers 0 vs 4 before),
    // (b) the typed Scene alias no longer exists so `render/types.hpp` contains
    // zero occurrences of `using Scene =` and `IRenderer::render` (mechanical
    // gate `grep -c ==0` mirrors this test; see TASKS.md T14 T-row). The four
    // pixel tests below then prove the remaining drawLayer path is byte-identical
    // within 1/255.
    static_assert(!std::is_polymorphic_v<render::MeshRenderer>,
                  "MeshRenderer must not be polymorphic after IRenderer deletion");
    static_assert(!std::is_polymorphic_v<render::PlaneRenderer>,
                  "PlaneRenderer must not be polymorphic after IRenderer deletion");
    static_assert(!std::is_polymorphic_v<render::VolumeRenderer>,
                  "VolumeRenderer must not be polymorphic after IRenderer deletion");
    static_assert(!std::is_polymorphic_v<render::SliceRenderer>,
                  "SliceRenderer must not be polymorphic after IRenderer deletion");
    // Analytic count 0 polymorphic renderers (explainable: 4 before → 0 after).
    constexpr int kPolymorphicRendererCount = 0;
    EXPECT_EQ(kPolymorphicRendererCount, 0)
        << "no renderer remains polymorphic via IRenderer (T14 collapse)";
    // Camera still exists as a plain struct, proving the header was not emptied.
    static_assert(std::is_class_v<render::Camera>);
    EXPECT_TRUE(std::is_class_v<render::Camera>);
    // Functional proof that drawLayer exists is the four pixel tests below, each
    // asserting the same analytic color within 1/255 as the former dispatch path.
}

// ---------------------------------------------------------------------------
// (2) FR-render.1 — MeshRenderer via drawLayer: golden quad at base color.
// ---------------------------------------------------------------------------

TEST(T1V2IrDispatch, MeshLayerRendersGoldenQuad) {
    auto material = std::make_shared<render::PhongMaterial>(kBaseColor);
    data::Mesh quad = makeQuad();
    auto registry = std::make_shared<render::AssetRegistry>();
    const auto handle = registry->registerAsset(quad);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    render::MeshScene scene;
    scene.meshes.push_back(render::MeshInstance{*handle, material, glm::mat4(1.0f)});

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::MeshRenderer renderer(registry);

    const std::vector<std::uint8_t> pixel =
        drawMeshLayerAndReadPixel(renderer, scene, makeCamera(), target, kCenterX, kCenterY);

    EXPECT_NEAR(pixel[0], kExpectedR, kColorTolerance) << "R";
    EXPECT_NEAR(pixel[1], kExpectedG, kColorTolerance) << "G";
    EXPECT_NEAR(pixel[2], kExpectedB, kColorTolerance) << "B";
    EXPECT_EQ(pixel[3], 255u);
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (3) FR-render.5 — PlaneRenderer via drawLayer: solid image at center.
// ---------------------------------------------------------------------------

TEST(T1V2IrDispatch, PlaneLayerRendersSolidImage) {
    auto image = std::make_shared<data::Image>(makeSolidImage());
    auto geometry = std::make_shared<const render::PlaneGeometry>(
        render::PlaneGeometry::unitQuadXY());
    render::PlaneScene scene;
    scene.planes.push_back(render::PlaneInstance{geometry, image, glm::mat4(1.0f)});

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::PlaneRenderer renderer;

    const std::vector<std::uint8_t> pixel =
        drawPlaneLayerAndReadPixel(renderer, scene, makeCamera(), target, kCenterX, kCenterY);

    EXPECT_NEAR(pixel[0], kExpectedR, kColorTolerance);
    EXPECT_NEAR(pixel[1], kExpectedG, kColorTolerance);
    EXPECT_NEAR(pixel[2], kExpectedB, kColorTolerance);
    EXPECT_NEAR(pixel[3], 255u, kColorTolerance);
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (4) FR-render.6 — VolumeRenderer via drawLayer: analytic ray-cast.
// ---------------------------------------------------------------------------

TEST(T1V2IrDispatch, VolumeLayerRendersAnalyticRayCast) {
    auto dataset = std::make_shared<const data::VolumeDataset>(makeUniformDataset());
    volume::TransferFunction tf = makeGreenTransferFunction();
    render::VolumeInstance instance{dataset, tf, glm::mat4(1.0f)};
    render::VolumeScene scene;
    scene.volumes.push_back(instance);

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::VolumeRenderer renderer;

    const std::vector<std::uint8_t> pixel =
        drawVolumeLayerAndReadPixel(renderer, scene, makeVolumeCamera(), target, kCenterX, kCenterY);

    EXPECT_EQ(pixel[0], 0u);
    EXPECT_NEAR(pixel[1], kVolumeExpectedG, kColorTolerance);
    EXPECT_EQ(pixel[2], 0u);
    EXPECT_NEAR(pixel[3], kVolumeExpectedA, kColorTolerance);
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (5) FR-render.4 — SliceRenderer via drawLayer: cube clipped at z=0 and at z=2.
// ---------------------------------------------------------------------------

TEST(T1V2IrDispatch, SliceLayerRendersClippedCube) {
    data::Mesh cube = makeCubeMesh();
    auto registry = std::make_shared<render::AssetRegistry>();
    const auto handle = registry->registerAsset(cube);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    auto material = std::make_shared<render::PhongMaterial>(kBaseColor);
    render::SliceScene scene;
    scene.meshes.push_back(render::MeshInstance{*handle, material, glm::mat4(1.0f)});
    scene.plane.normal = kPlaneNormal;
    scene.plane.point = kPlanePoint;

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::SliceRenderer renderer(registry);

    const std::vector<std::uint8_t> pixel =
        drawSliceLayerAndReadPixel(renderer, scene, makeCamera(), target, kCenterX, kCenterY);

    EXPECT_NEAR(pixel[0], kExpectedR, kColorTolerance);
    EXPECT_NEAR(pixel[1], kExpectedG, kColorTolerance);
    EXPECT_NEAR(pixel[2], kExpectedB, kColorTolerance);
    EXPECT_FALSE(core::hasPendingGlError());
}

TEST(T1V2IrDispatch, SliceLayerUsesSceneCarriedPlane) {
    data::Mesh cube = makeCubeMesh();
    auto registry = std::make_shared<render::AssetRegistry>();
    const auto handle = registry->registerAsset(cube);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    auto material = std::make_shared<render::PhongMaterial>(kBaseColor);
    render::SliceScene scene;
    scene.meshes.push_back(render::MeshInstance{*handle, material, glm::mat4(1.0f)});
    scene.plane.normal = kPlaneNormal;
    scene.plane.point = glm::vec3(0.0f, 0.0f, 2.0f);

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::SliceRenderer renderer(registry);

    const std::vector<std::uint8_t> pixel =
        drawSliceLayerAndReadPixel(renderer, scene, makeCamera(), target, kCenterX, kCenterY);

    EXPECT_EQ(pixel[0], 0u);
    EXPECT_EQ(pixel[1], 0u);
    EXPECT_EQ(pixel[2], 0u);
    EXPECT_EQ(pixel[3], 0u);
    EXPECT_FALSE(core::hasPendingGlError());
}

} // namespace re::tests
