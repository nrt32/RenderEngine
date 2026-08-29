// tests/t10_oit_test.cpp — T10 gate tests (FR-render.2/3, SPEC §4).
//
// Asserts:
//   (1) FR-render.2 — two overlapping quads at known depths composite to the
//       analytic depth-ordered blend within 1/255 (LinkedListOIT capture ->
//       depth-sort -> composite).
//   (2) FR-render.3 — an opaque-only scene produces center-pixel alpha == 1.0,
//       and adding one transparent quad flips the injected pipeline on
//       (observable via a spy).
//   (3) FR-render.3 — the pipeline interface is swappable: a stub impl drives
//       the same MeshRenderer (begin -> drawTransparent -> end).
//
// Analytic setup (docs/render.md): two full-screen +Z-facing quads at known
// depths, orthographic camera looking down -Z. The near quad (world z=0, closer
// to the camera) is {0.4,0.2,0.1,0.5}; the far quad (world z=-1) is
// {0.1,0.6,0.3,0.4}. LinkedListOIT captures both per pixel, sorts by depth
// (near -> far), and composites back-to-front with the premultiplied-alpha
// "over" operator:
//
//   far premult  = {0.04, 0.24, 0.12, 0.4}
//   near premult = {0.20, 0.10, 0.05, 0.5}
//   near over far:
//     rgb = {0.2,0.1,0.05} + (1-0.5)*{0.04,0.24,0.12} = {0.22,0.22,0.11}
//     a   = 0.5 + (1-0.5)*0.4 = 0.7
//   => bytes {round(0.22*255), round(0.22*255), round(0.11*255),
//             round(0.7*255)} = {56, 56, 28, 179}
//
// If the pipeline sorted wrongly (far over near) the result would be
// {0.16, 0.30, 0.15, 0.7} -> {41, 77, 38, 179}, which is outside the 1/255
// tolerance of the depth-ordered value, so the test discriminates ordering.
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers (including utils::PixelReader for pixel readback) — no raw glXxx calls.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
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
#include "render/linked_list_oit.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "tests/offscreen_fixture.hpp"
#include "tests/test_helpers.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants (FR-render.2/3).
// ---------------------------------------------------------------------------

// Target framebuffer / viewport size: 64x64.
constexpr std::uint32_t kTargetWidth = 64u;
constexpr std::uint32_t kTargetHeight = 64u;
constexpr std::uint32_t kCenterX = kTargetWidth / 2u;  // 32
constexpr std::uint32_t kCenterY = kTargetHeight / 2u; // 32

// The color tolerance: 1/255 per FR-render.2.
constexpr int kColorTolerance = 1;

// The two transparent quad materials (straight RGBA). alpha < 1 => transparent.
constexpr glm::vec4 kNearColor(0.4f, 0.2f, 0.1f, 0.5f); // near quad (z=0)
constexpr glm::vec4 kFarColor(0.1f, 0.6f, 0.3f, 0.4f);  // far quad (z=-1)

// Analytic depth-ordered composite of near-over-far (premultiplied-alpha over).
// See the file comment for the derivation.
constexpr std::uint8_t kExpectedR = 56u;  // round(0.22 * 255) = 56
constexpr std::uint8_t kExpectedG = 56u;  // round(0.22 * 255) = 56
constexpr std::uint8_t kExpectedB = 28u;  // round(0.11 * 255) = 28
constexpr std::uint8_t kExpectedA = 179u; // round(0.70 * 255) = 179

// Each of the two full-screen quads rasterizes every pixel, so the node
// allocator captures exactly width*height*2 fragments across the frame.
constexpr std::uint32_t kExpectedCapturedFragments =
    kTargetWidth * kTargetHeight * 2u; // 8192

// ---------------------------------------------------------------------------
// Test helpers.
// ---------------------------------------------------------------------------

/// Build a golden +Z-facing quad mesh covering [-1,1]^2 at z=0 (two triangles).
/// The default camera: eye at (0,0,5) looking down -Z at the origin, with an
/// orthographic projection mapping NDC [-1,1]^2 onto the full viewport.
/// Build a color-only FBO render target of `w` x `h` pixels with clear color
/// transparent black.
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

/// Read back a single pixel from the currently-bound framebuffer at (x, y).
/// A recording stub pipeline: records begin/drawTransparent/end calls so a test
/// can assert how MeshRenderer drives the (swappable) pipeline interface.
class RecordingPipeline final : public render::ITransparencyPipeline {
   public:
    data::Result<void> begin(const render::Camera&,
                             const render::RenderTarget&,
                             core::REContext&) override {
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
                           const render::RenderTarget&,
                           core::REContext&) override {
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
    int endCount() const noexcept {
        return endCount_;
    }

   private:
    int beginCount_{0};
    int drawTransparentCount_{0};
    int endCount_{0};
};

} // namespace

// ---------------------------------------------------------------------------
// (1) FR-render.2 — two overlapping quads at known depths composite to the
//     analytic depth-ordered blend within 1/255.
// ---------------------------------------------------------------------------

TEST(T10Oit, TwoQuadsCompositeToDepthOrderedBlend) {
    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);

    // Two full-screen transparent quads at known depths: near at z=0, far at
    // z=-1 (near is closer to the camera at z=5). Both instances share ONE
    // registered handle: the same CPU quad is one GPU object in the shared
    // registry (SPEC §9 V2.5).
    data::Mesh quad = makeQuad();
    auto registry = std::make_shared<render::AssetRegistry>();
    const auto handle = registry->registerAsset(quad);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    auto nearMaterial =
        std::make_shared<render::PhongMaterial>(kNearColor);
    auto farMaterial =
        std::make_shared<render::PhongMaterial>(kFarColor);
    ASSERT_TRUE(nearMaterial->isTransparent());
    ASSERT_TRUE(farMaterial->isTransparent());

    glm::mat4 nearModel(1.0f); // z = 0
    glm::mat4 farModel =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.0f));

    render::MeshScene scene;
    scene.meshes.push_back(
        render::MeshInstance{*handle, nearMaterial, nearModel});
    scene.meshes.push_back(render::MeshInstance{*handle, farMaterial, farModel});

    render::Camera camera = makeCamera();
    render::RenderTarget rt;
    rt.framebuffer = &target.framebuffer;
    rt.width = kTargetWidth;
    rt.height = kTargetHeight;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    auto pipeline = std::make_shared<render::LinkedListOIT>();
    render::MeshRenderer renderer(registry, pipeline);
    auto result = renderer.renderForTest(scene, camera, rt);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(core::hasPendingGlError());

    // Both quads are full-screen and uniformly shaded, so every pixel must
    // match the analytic depth-ordered blend. Sample the center plus four
    // corner-ish pixels (readback coordinates, y = 0 is the bottom scanline).
    constexpr std::uint32_t kSampleX[5] = {32u, 8u, 56u, 8u, 56u};
    constexpr std::uint32_t kSampleY[5] = {32u, 8u, 8u, 56u, 56u};
    for (int i = 0; i < 5; ++i) {
        const std::vector<std::uint8_t> pixel =
            readPixel(kSampleX[i], kSampleY[i]);
        EXPECT_NEAR(pixel[0], kExpectedR, kColorTolerance)
            << "R channel at (" << kSampleX[i] << "," << kSampleY[i] << ")";
        EXPECT_NEAR(pixel[1], kExpectedG, kColorTolerance)
            << "G channel at (" << kSampleX[i] << "," << kSampleY[i] << ")";
        EXPECT_NEAR(pixel[2], kExpectedB, kColorTolerance)
            << "B channel at (" << kSampleX[i] << "," << kSampleY[i] << ")";
        EXPECT_NEAR(pixel[3], kExpectedA, kColorTolerance)
            << "A channel at (" << kSampleX[i] << "," << kSampleY[i] << ")";
    }
    EXPECT_FALSE(core::hasPendingGlError());

    // Both full-screen quads rasterize every pixel => exactly 2 fragments per
    // pixel were captured and depth-sorted by the pipeline. The node-allocator
    // counter is read back through the test-consumed readback (guardrail
    // no_production_readback).
    const auto captured = pipeline->readCapturedFragmentCount();
    ASSERT_TRUE(captured.ok()) << captured.error().message;
    EXPECT_EQ(*captured, kExpectedCapturedFragments);
    EXPECT_FALSE(pipeline->isEngaged());
}

// ---------------------------------------------------------------------------
// (2) FR-render.3 — opaque-only scene: alpha == 1.0 at the sampled pixels, and
//     adding one transparent quad flips the pipeline on.
// ---------------------------------------------------------------------------

TEST(T10Oit, OpaqueAlphaIsOneAndTransparentQuadEngagesPipeline) {
    data::Mesh quad = makeQuad();
    auto registry = std::make_shared<render::AssetRegistry>();
    const auto handle = registry->registerAsset(quad);
    ASSERT_TRUE(handle.ok()) << handle.error().message;

    // Opaque-only scene: center-pixel alpha must be exactly 1.0 (no
    // transparency engaged) and the pipeline must stay OFF.
    {
        auto opaque =
            std::make_shared<render::PhongMaterial>(glm::vec4(0.2f, 0.4f, 0.8f, 1.0f));
        ASSERT_FALSE(opaque->isTransparent());

        render::MeshScene opaqueScene;
        opaqueScene.meshes.push_back(
            render::MeshInstance{*handle, opaque, glm::mat4(1.0f)});

        RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
        render::RenderTarget rt;
        rt.framebuffer = &target.framebuffer;
        rt.width = kTargetWidth;
        rt.height = kTargetHeight;
        rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

        auto spy = std::make_shared<RecordingPipeline>();
        render::MeshRenderer renderer(registry, spy);
        auto result = renderer.renderForTest(opaqueScene, makeCamera(), rt);
        ASSERT_TRUE(result.ok()) << result.error().message;

        // Sample multiple pixels: every pixel of an opaque-only scene must have
        // alpha == 1.0 (no transparency engaged), per FR-render.3.
        constexpr std::uint32_t kSampleX[4] = {32u, 8u, 56u, 16u};
        constexpr std::uint32_t kSampleY[4] = {32u, 8u, 56u, 48u};
        for (int i = 0; i < 4; ++i) {
            const std::vector<std::uint8_t> pixel =
                readPixel(kSampleX[i], kSampleY[i]);
            EXPECT_EQ(pixel[3], 255u)
                << "alpha == 1.0 for an opaque scene at (" << kSampleX[i] << ","
                << kSampleY[i] << ")";
        }
        // The opaque-only scene must never engage the pipeline.
        EXPECT_EQ(spy->beginCount(), 0);
        EXPECT_EQ(spy->drawTransparentCount(), 0);
        EXPECT_FALSE(spy->isEngaged());
        EXPECT_FALSE(core::hasPendingGlError());
    }

    // Add one transparent quad to the same scene: the pipeline flips on
    // (begin/drawTransparent/end observed via the spy).
    {
        auto opaque =
            std::make_shared<render::PhongMaterial>(glm::vec4(0.2f, 0.4f, 0.8f, 1.0f));
        auto transparent =
            std::make_shared<render::PhongMaterial>(glm::vec4(0.4f, 0.2f, 0.1f, 0.5f));
        ASSERT_TRUE(transparent->isTransparent());

        render::MeshScene mixedScene;
        mixedScene.meshes.push_back(
            render::MeshInstance{*handle, opaque, glm::mat4(1.0f)});
        mixedScene.meshes.push_back(
            render::MeshInstance{*handle, transparent, glm::mat4(1.0f)});

        RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
        render::RenderTarget rt;
        rt.framebuffer = &target.framebuffer;
        rt.width = kTargetWidth;
        rt.height = kTargetHeight;
        rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

        auto spy = std::make_shared<RecordingPipeline>();
        render::MeshRenderer renderer(registry, spy);
        auto result = renderer.renderForTest(mixedScene, makeCamera(), rt);
        ASSERT_TRUE(result.ok()) << result.error().message;

        EXPECT_EQ(spy->beginCount(), 1)
            << "pipeline engaged for a transparent scene";
        EXPECT_EQ(spy->drawTransparentCount(), 1)
            << "one transparent mesh captured";
        EXPECT_EQ(spy->endCount(), 1);
        EXPECT_FALSE(spy->isEngaged()) << "frame finished";
        EXPECT_FALSE(core::hasPendingGlError());
    }
}

// ---------------------------------------------------------------------------
// (3) FR-render.3 — the pipeline interface is swappable: a stub impl drives the
//     same MeshRenderer.
// ---------------------------------------------------------------------------

TEST(T10Oit, PipelineInterfaceIsSwappable) {
    data::Mesh quad = makeQuad();
    auto registry = std::make_shared<render::AssetRegistry>();
    const auto handle = registry->registerAsset(quad);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    auto transparent =
        std::make_shared<render::PhongMaterial>(glm::vec4(0.4f, 0.2f, 0.1f, 0.5f));
    ASSERT_TRUE(transparent->isTransparent());

    render::MeshScene scene;
    scene.meshes.push_back(
        render::MeshInstance{*handle, transparent, glm::mat4(1.0f)});

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::RenderTarget rt;
    rt.framebuffer = &target.framebuffer;
    rt.width = kTargetWidth;
    rt.height = kTargetHeight;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    // The stub does no compositing; MeshRenderer must still drive it correctly:
    // exactly one frame (begin -> drawTransparent -> end) for the transparent
    // mesh, leaving the pipeline disengaged. This proves the renderer depends
    // only on the ITransparencyPipeline abstraction (open/closed, DI).
    auto stub = std::make_shared<RecordingPipeline>();
    render::MeshRenderer renderer(registry, stub);
    auto result = renderer.renderForTest(scene, makeCamera(), rt);
    ASSERT_TRUE(result.ok()) << result.error().message;

    EXPECT_EQ(stub->beginCount(), 1);
    EXPECT_EQ(stub->drawTransparentCount(), 1);
    EXPECT_EQ(stub->endCount(), 1);
    EXPECT_FALSE(stub->isEngaged());
    EXPECT_FALSE(core::hasPendingGlError());
}

} // namespace re::tests
