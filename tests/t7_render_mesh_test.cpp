// tests/t7_render_mesh_test.cpp — T7 gate tests (FR-render.1/3, SPEC §4).
//
// Asserts:
//   (1) a known solid-color quad mesh rendered to an offscreen target has the
//       expected center-pixel color within 1/255 (FR-render.1);
//   (2) an opaque-only scene produces center-pixel alpha == 1.0, and the
//       injected transparency-pipeline spy confirms the pipeline is OFF
//       (FR-render.3, no transparency engaged);
//   (3) materials report isTransparent() correctly (FR-render.3).
//
// The acceptance constant is analytic (docs/render.md): the v1 opaque pass uses
// a fixed head-on light from +Z with ambient=0, diffuse=1, specular=0, so a
// +Z-facing quad renders at exactly its material's base color. With the quad
// mapped 1:1 onto the viewport (orthographic projection), the center pixel is
// that base color within 1/255.
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers (including utils::PixelReader for pixel readback) — no raw glXxx calls.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
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
#include "render/imaterial.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "tests/offscreen_fixture.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants (FR-render.1).
// ---------------------------------------------------------------------------

// The material's base color: a clean solid color that maps to exact RGBA8
// bytes (0.2*255=51, 0.4*255=102, 0.8*255=204), so the expected center pixel is
// unambiguous. Alpha 1.0 => opaque.
constexpr glm::vec4 kBaseColor(0.2f, 0.4f, 0.8f, 1.0f);
constexpr std::uint8_t kExpectedR = 51u;
constexpr std::uint8_t kExpectedG = 102u;
constexpr std::uint8_t kExpectedB = 204u;
constexpr std::uint8_t kExpectedA = 255u;

// Target framebuffer size: 64x64. The quad maps 1:1 onto the viewport, so the
// center pixel (32, 32) lies inside the quad.
constexpr std::uint32_t kTargetWidth = 64u;
constexpr std::uint32_t kTargetHeight = 64u;
constexpr std::uint32_t kCenterX = kTargetWidth / 2u;  // 32
constexpr std::uint32_t kCenterY = kTargetHeight / 2u; // 32

// The color tolerance: 1/255 per FR-render.1.
constexpr int kColorTolerance = 1;

// ---------------------------------------------------------------------------
// Test helpers.
// ---------------------------------------------------------------------------

/// Build a golden +Z-facing quad mesh covering [-1,1]^2 at z=0 (two triangles).
/// Vertex normals are all (0,0,1), so a front-facing fragment shades to 1.
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

/// The default camera: eye at (0,0,5) looking down -Z at the origin, with an
/// orthographic projection mapping NDC [-1,1]^2 onto the full viewport.
render::Camera makeCamera() {
    render::Camera camera;
    camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0.0f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    return camera;
}

/// An injectable transparency-pipeline spy: records
/// begin()/end()/drawTransparent() calls so a test can assert the pipeline was
/// NOT engaged for an opaque scene.
class SpyTransparencyPipeline final : public render::ITransparencyPipeline {
   public:
    data::Result<void> begin(const render::Camera&,
                             const render::RenderTarget&) override {
        ++beginCount_;
        return data::Result<void>(data::value);
    }
    data::Result<void> drawTransparent(const render::MeshGeometry&,
                                       const glm::vec4&, const glm::mat4&,
                                       const render::Camera&) override {
        ++drawTransparentCount_;
        return data::Result<void>(data::value);
    }
    data::Result<void> end(const render::Camera&,
                           const render::RenderTarget&) override {
        ++endCount_;
        return data::Result<void>(data::value);
    }
    bool isEngaged() const noexcept override {
        return beginCount_ > endCount_;
    }
    int beginCount() const noexcept {
        return beginCount_;
    }
    int drawTransparentCount() const noexcept {
        return drawTransparentCount_;
    }

   private:
    int beginCount_{0};
    int drawTransparentCount_{0};
    int endCount_{0};
};

} // namespace

// ---------------------------------------------------------------------------
// (1) FR-render.1 — solid-color mesh center-pixel color within 1/255.
// ---------------------------------------------------------------------------

TEST(T7RenderMesh, OpaqueQuadCenterPixelMatchesBaseColor) {
    // Offscreen RGBA8 color target (64x64).
    auto targetColor = core::Texture2D::create();
    auto targetFramebuffer = core::Framebuffer::create();
    ASSERT_TRUE(targetColor.ok()) << targetColor.error().message;
    ASSERT_TRUE(targetFramebuffer.ok()) << targetFramebuffer.error().message;
    std::vector<std::uint8_t> zeros(kTargetWidth * kTargetHeight * 4u, 0u);
    targetColor->bind(0u);
    targetColor->upload(kTargetWidth, kTargetHeight, zeros.data());
    targetColor->unbind(0u);
    targetFramebuffer->bind();
    targetFramebuffer->attachColor(*targetColor);
    ASSERT_TRUE(targetFramebuffer->isComplete());
    targetFramebuffer->unbind();

    render::PhongMaterial material(kBaseColor);
    ASSERT_FALSE(material.isTransparent());

    data::Mesh quad = makeQuadMesh();
    // The scene carries the mesh's AssetHandle (SPEC §9 V2.5), resolved by the
    // renderer through the shared asset registry.
    render::AssetRegistry registry;
    const auto handle = registry.registerAsset(quad);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    render::MeshScene scene;
    scene.meshes.push_back(
        render::MeshInstance{*handle, &material, glm::mat4(1.0f)});

    render::Camera camera = makeCamera();
    render::RenderTarget rt;
    rt.framebuffer = &*targetFramebuffer;
    rt.width = kTargetWidth;
    rt.height = kTargetHeight;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    // No transparency pipeline injected: an opaque-only scene must render via
    // the plain forward pass.
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

    EXPECT_NEAR(pixels[0], kExpectedR, kColorTolerance) << "R channel";
    EXPECT_NEAR(pixels[1], kExpectedG, kColorTolerance) << "G channel";
    EXPECT_NEAR(pixels[2], kExpectedB, kColorTolerance) << "B channel";
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (2) FR-render.3 — opaque-only scene: center-pixel alpha == 1.0 and the
//     injected spy confirms the pipeline is OFF.
// ---------------------------------------------------------------------------

TEST(T7RenderMesh, OpaqueSceneAlphaIsOneAndPipelineStaysOff) {
    SpyTransparencyPipeline spy;

    auto targetColor = core::Texture2D::create();
    auto targetFramebuffer = core::Framebuffer::create();
    ASSERT_TRUE(targetColor.ok()) << targetColor.error().message;
    ASSERT_TRUE(targetFramebuffer.ok()) << targetFramebuffer.error().message;
    std::vector<std::uint8_t> zeros(kTargetWidth * kTargetHeight * 4u, 0u);
    targetColor->bind(0u);
    targetColor->upload(kTargetWidth, kTargetHeight, zeros.data());
    targetColor->unbind(0u);
    targetFramebuffer->bind();
    targetFramebuffer->attachColor(*targetColor);
    ASSERT_TRUE(targetFramebuffer->isComplete());
    targetFramebuffer->unbind();

    render::PhongMaterial material(kBaseColor);
    ASSERT_FALSE(material.isTransparent());

    data::Mesh quad = makeQuadMesh();
    render::AssetRegistry registry;
    const auto handle = registry.registerAsset(quad);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    render::MeshScene scene;
    scene.meshes.push_back(
        render::MeshInstance{*handle, &material, glm::mat4(1.0f)});

    render::Camera camera = makeCamera();
    render::RenderTarget rt;
    rt.framebuffer = &*targetFramebuffer;
    rt.width = kTargetWidth;
    rt.height = kTargetHeight;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    // Inject the spy. The opaque-only scene must NOT engage it (FR-render.3).
    render::MeshRenderer renderer(&registry, &spy);
    auto result = renderer.render(scene, camera, rt);
    ASSERT_TRUE(result.ok()) << result.error().message;

    // Center-pixel alpha must be exactly 1.0 (opaque; no transparency engaged).
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read = reader.read(kCenterX, kCenterY, 1u, 1u, pixels);
    ASSERT_TRUE(read.ok()) << read.error().message;
    ASSERT_EQ(pixels.size(), 4u);
    EXPECT_EQ(pixels[3], kExpectedA) << "alpha channel (== 255 / 1.0)";

    // The spy must confirm the pipeline was never engaged.
    EXPECT_EQ(spy.beginCount(), 0) << "OIT pipeline must stay OFF for opaque";
    EXPECT_FALSE(spy.isEngaged());
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (3) FR-render.3 — materials report isTransparent() correctly.
// ---------------------------------------------------------------------------

TEST(T7RenderMesh, MaterialsReportTransparencyCorrectly) {
    // Opaque: alpha == 1.0 -> not transparent.
    render::PhongMaterial opaque(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_FALSE(opaque.isTransparent());
    EXPECT_EQ(opaque.baseColor().a, 1.0f);

    // Transparent: alpha < 1.0 -> transparent.
    render::PhongMaterial transparent(glm::vec4(1.0f, 1.0f, 1.0f, 0.5f));
    EXPECT_TRUE(transparent.isTransparent());
    EXPECT_EQ(transparent.baseColor().a, 0.5f);

    // Fully invisible (alpha == 0) is still transparent (needs compositing).
    render::PhongMaterial invisible(glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    EXPECT_TRUE(invisible.isTransparent());
}

} // namespace re::tests
