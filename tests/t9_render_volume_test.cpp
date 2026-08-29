// tests/t9_render_volume_test.cpp — T9 gate tests (FR-render.6, SPEC §4).
//
// Asserts:
//   (1) FR-render.6 — the center pixel of a ray-cast of a tiny synthetic
//       volume matches the analytic CPU ray-cast (computed from the same
//       volume/ pure math) within 1/255.
//   (2) The analytic CPU ray-cast equals the hand-derived closed-form constant
//       {0, 0.9375, 0, 0.9375} (premultiplied), documenting the acceptance
//       value in bytes {0, 239, 0, 239}.
//   (3) The world AABB of an identity-model volume is exactly [0,1]^3, and of
//       a translated+scaled model is the analytic box (pure math, no GL).
//   (4) The renderer rejects a transfer function with more than the shader's
//       maximum control points with a typed error.
//
// Analytic setup (docs/render.md): a uniform synthetic volume (every voxel =
// 0.5) occupies the unit cube [0,1]^3 in world space (identity model). The
// camera is orthographic, eye (0.5,0.5,5) looking straight down -Z at the box
// center, mapping NDC [-1,1]^2 onto the full 64x64 viewport. For any pixel the
// reconstructed ray is exactly parallel to -Z, so its world AABB intersection
// spans exactly [0,1] in z: tEntry/tExit give span = 1.0 and, with the
// renderer's default step length 0.25, `floor(1.0/0.25) = 4` steps at their
// centers (volume::computeRaySampleSteps, FR-vol.3). Every sample's density is
// the uniform 0.5; the constant-green transfer function (control points at
// 0 and 1, both {0,1,0,0.5}) maps it to straight RGBA {0,1,0,0.5}; front-to-
// back premultiplied compositing (volume::compositeFrontToBack, FR-vol.2) of
// four such samples yields {0, 0.9375, 0, 0.9375}.
//
// The GL fragment shader mirrors this exact math (slab intersection, center
// stepping, GL_LINEAR 3D-texture trilinear sampling, piecewise-linear TF,
// front-to-back premultiplied compositing), so the rendered center pixel must
// match the CPU value within 1/255 (FR-render.6).
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers (including utils::PixelReader for pixel readback) — no raw glXxx calls.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <utility>
#include <vector>

#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "utils/pixel_reader.hpp"
#include "core/texture2d.hpp"
#include "data/volume_dataset.hpp"
#include "render/types.hpp" // render::Camera / render::RenderTarget
#include "render/view.hpp"
#include "render/volume_renderer.hpp"
#include "tests/offscreen_fixture.hpp"
#include "tests/test_helpers.hpp"
#include "volume/color.hpp"
#include "volume/ray_caster.hpp"
#include "volume/transfer_function.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants (FR-render.6).
// ---------------------------------------------------------------------------

// Target framebuffer / viewport size: 64x64 (the ray-cast covers the full
// viewport).
constexpr std::uint32_t kTargetWidth = 64u;
constexpr std::uint32_t kTargetHeight = 64u;

// The color tolerance: 1/255 per FR-render.6.
constexpr int kColorTolerance = 1;

// Uniform synthetic volume: 2x2x2, every voxel = 0.5.
constexpr std::uint32_t kVolSize = 2u;
constexpr float kUniformVoxel = 0.5f;

// Constant-green transfer function: {0,1,0,0.5} (straight RGBA) at both
// control points, so the ramp is constant (FR-vol.1).
constexpr float kTfGreen = 1.0f;
constexpr float kTfAlpha = 0.5f;

// ---------------------------------------------------------------------------
// Test helpers.
// ---------------------------------------------------------------------------

/// The default camera: eye at (0.5,0.5,5) looking down -Z at the box center,
/// orthographic projection mapping NDC [-1,1]^2 onto the full viewport.
/// Build a render target (color-only FBO of `w` x `h`) bound for readback.
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

/// The unit volume dataset: 2x2x2, all voxels = kUniformVoxel.
data::VolumeDataset makeUniformDataset() {
    std::vector<float> voxels(
        static_cast<std::size_t>(kVolSize) * kVolSize * kVolSize,
        kUniformVoxel);
    return data::VolumeDataset(kVolSize, kVolSize, kVolSize, std::move(voxels));
}

/// The constant-green transfer function (FR-vol.1).
volume::TransferFunction makeGreenTransferFunction() {
    std::vector<volume::TransferFunction::ControlPoint> points;
    points.push_back({0.0f, volume::RgbaColor{0.0f, kTfGreen, 0.0f, kTfAlpha}});
    points.push_back({1.0f, volume::RgbaColor{0.0f, kTfGreen, 0.0f, kTfAlpha}});
    return volume::TransferFunction(std::move(points));
}

/// The world ray for the pixel whose center is at readback coordinates
/// (px, py) (py = 0 is the bottom scanline, matching GL). Reconstructs the
/// ray by unprojecting the pixel's NDC near/far points through the camera's
/// view-projection, exactly as the fragment shader does.
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
    const glm::mat4 viewProj = camera.proj * camera.view;
    const glm::mat4 inv = glm::inverse(viewProj);

    const glm::vec4 nearH = inv * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    const glm::vec4 farH = inv * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    const glm::vec3 nearPos = glm::vec3(nearH) / nearH.w;
    const glm::vec3 farPos = glm::vec3(farH) / farH.w;
    return {nearPos, glm::normalize(farPos - nearPos)};
}

/// The analytic CPU ray-cast of `dataset` with `tf` for the ray from
/// `origin` in `direction` against the world AABB [0,1]^3 (identity model),
/// using the renderer's default step length. Returns the premultiplied RGBA
/// result (volume::compositeFrontToBack, FR-vol.2).
volume::RgbaColor analyticRayCast(const glm::vec3& origin,
                                  const glm::vec3& direction,
                                  const data::VolumeDataset& dataset,
                                  const volume::TransferFunction& tf) {
    const volume::Ray ray{origin, direction};
    const volume::Aabb aabb{glm::vec3(0.0f), glm::vec3(1.0f)};
    const volume::RaySampleSteps steps =
        volume::computeRaySampleSteps(ray, aabb, render::kDefaultStepLength);

    // Map a model-space (== world-space, identity model) position to a
    // continuous index coordinate: the dataset occupies [0,1]^3 in model
    // space, which maps to index space [0, dim-1] via idx = modelPos*(dim-1).
    const glm::vec3 sizeScale(static_cast<float>(dataset.sizeX() - 1u),
                              static_cast<float>(dataset.sizeY() - 1u),
                              static_cast<float>(dataset.sizeZ() - 1u));
    std::vector<volume::RgbaColor> samples;
    samples.reserve(steps.positions.size());
    for (const float t : steps.positions) {
        const glm::vec3 worldPos = origin + direction * t;
        const glm::vec3 idx = worldPos * sizeScale;
        const float density = dataset.sampleTrilinear(idx.x, idx.y, idx.z);
        samples.push_back(tf.sample(density));
    }
    return volume::compositeFrontToBack(samples);
}

/// Render `scene` to the target and read back the single pixel at (x, y).
/// `x`/`y` are readback coordinates (y = 0 is the bottom scanline).
/// T3b: VolumeRenderer::render deleted — ported to View path 1/255 (FR-render.6).
std::vector<std::uint8_t> renderAndReadPixel(const render::VolumeScene& scene,
                                             render::VolumeRenderer& renderer,
                                             RenderedTarget& target,
                                             std::uint32_t x, std::uint32_t y) {
    (void)target;
    render::Camera camera = makeCamera();
    auto rendererPtr = std::make_shared<render::VolumeRenderer>(renderer.assets());
    render::View view(render::ViewRect{0, 0, static_cast<int>(kTargetWidth),
                                       static_cast<int>(kTargetHeight)},
                      glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    view.setCamera(camera);
    view.addItem(scene, rendererPtr);
    auto result = view.renderWithEnsure();
    EXPECT_TRUE(result.ok()) << result.error().message;
    EXPECT_NE(view.target(), nullptr);
    if (!view.target()) return {};
    view.target()->framebuffer().bind();
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read = reader.read(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    view.target()->framebuffer().unbind();
    return pixels;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) FR-render.6 — center pixel matches the analytic CPU ray-cast within
// 1/255.
// ---------------------------------------------------------------------------

TEST(T9RenderVolume, CenterPixelMatchesAnalyticRayCast) {
    auto dataset = std::make_shared<const data::VolumeDataset>(makeUniformDataset());
    volume::TransferFunction tf = makeGreenTransferFunction();
    // Ownership split in the instance: voxels by SHARED reference (co-owned,
    // cannot dangle), transfer function BY VALUE (small immutable ramp).
    render::VolumeInstance instance{dataset, tf, glm::mat4(1.0f)};
    render::VolumeScene scene;
    scene.volumes.push_back(instance);

    RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
    render::VolumeRenderer renderer;
    const std::vector<std::uint8_t> pixel = renderAndReadPixel(
        scene, renderer, target, kTargetWidth / 2u, kTargetHeight / 2u);

    // The analytic CPU ray-cast for this exact pixel's ray.
    const render::Camera camera = makeCamera();
    const auto [origin, direction] =
        worldRayForPixel(kTargetWidth / 2u, kTargetHeight / 2u, kTargetWidth,
                         kTargetHeight, camera);
    const volume::RgbaColor expected =
        analyticRayCast(origin, direction, *dataset, tf);

    EXPECT_NEAR(pixel[0], static_cast<int>(expected.r * 255.0f + 0.5f),
                kColorTolerance)
        << "R channel";
    EXPECT_NEAR(pixel[1], static_cast<int>(expected.g * 255.0f + 0.5f),
                kColorTolerance)
        << "G channel";
    EXPECT_NEAR(pixel[2], static_cast<int>(expected.b * 255.0f + 0.5f),
                kColorTolerance)
        << "B channel";
    EXPECT_NEAR(pixel[3], static_cast<int>(expected.a * 255.0f + 0.5f),
                kColorTolerance)
        << "A channel";
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (2) The analytic CPU ray-cast equals the hand-derived closed form.
// ---------------------------------------------------------------------------

TEST(T9RenderVolume, AnalyticRayCastMatchesClosedForm) {
    // Four uniform samples {0,1,0,0.5} composited front-to-back (FR-vol.2):
    //   out.a   = 1 - (1-0.5)^4 = 0.9375
    //   out.rgb = sum over i of (1-0.5)^i * 0.5 * {0,1,0}
    //           = 0.5*(1 + 0.5 + 0.25 + 0.125) * {0,1,0} = 0.9375 * {0,1,0}
    // so the premultiplied result is {0, 0.9375, 0, 0.9375}.
    auto dataset = std::make_shared<const data::VolumeDataset>(makeUniformDataset());
    volume::TransferFunction tf = makeGreenTransferFunction();

    const render::Camera camera = makeCamera();
    const auto [origin, direction] =
        worldRayForPixel(kTargetWidth / 2u, kTargetHeight / 2u, kTargetWidth,
                         kTargetHeight, camera);
    const volume::RgbaColor result =
        analyticRayCast(origin, direction, *dataset, tf);

    EXPECT_NEAR(result.r, 0.0f, 1e-6f);
    EXPECT_NEAR(result.g, 0.9375f, 1e-6f);
    EXPECT_NEAR(result.b, 0.0f, 1e-6f);
    EXPECT_NEAR(result.a, 0.9375f, 1e-6f);

    // Documented acceptance bytes: G = round(0.9375*255) = 239, A = 239.
    EXPECT_EQ(static_cast<int>(result.g * 255.0f + 0.5f), 239);
    EXPECT_EQ(static_cast<int>(result.a * 255.0f + 0.5f), 239);
}

// ---------------------------------------------------------------------------
// (3) World AABB of a volume is analytic (pure math, no GL).
// ---------------------------------------------------------------------------

TEST(T9RenderVolume, WorldAabbIsAnalytic) {
    auto dataset = std::make_shared<const data::VolumeDataset>(makeUniformDataset());
    volume::TransferFunction tf = makeGreenTransferFunction();

    // Identity model: the world AABB is exactly [0,1]^3.
    {
        render::VolumeInstance identity{dataset, tf, glm::mat4(1.0f)};
        const auto [minv, maxv] = render::VolumeRenderer::worldAabb(identity);
        EXPECT_EQ(minv, glm::vec3(0.0f, 0.0f, 0.0f));
        EXPECT_EQ(maxv, glm::vec3(1.0f, 1.0f, 1.0f));
    }

    // Translate by (1,2,3) and scale by 0.5, applied as model = T * S (scale in
    // model space first, then translate): the unit cube [0,1]^3 becomes
    // [1,1.5] x [2,2.5] x [3,3.5] (axis-aligned, exact).
    {
        glm::mat4 model(1.0f);
        model = glm::translate(model, glm::vec3(1.0f, 2.0f, 3.0f));
        model = glm::scale(model, glm::vec3(0.5f));
        render::VolumeInstance scaled{dataset, tf, model};
        const auto [minv, maxv] = render::VolumeRenderer::worldAabb(scaled);
        EXPECT_NEAR(minv.x, 1.0f, 1e-6f);
        EXPECT_NEAR(minv.y, 2.0f, 1e-6f);
        EXPECT_NEAR(minv.z, 3.0f, 1e-6f);
        EXPECT_NEAR(maxv.x, 1.5f, 1e-6f);
        EXPECT_NEAR(maxv.y, 2.5f, 1e-6f);
        EXPECT_NEAR(maxv.z, 3.5f, 1e-6f);
    }
}

// ---------------------------------------------------------------------------
// (4) The renderer rejects a transfer function with too many control points.
// ---------------------------------------------------------------------------

TEST(T9RenderVolume, RejectsTooManyTransferFunctionPoints) {
    // 9 control points exceed the shader's fixed uniform array size (8).
    std::vector<volume::TransferFunction::ControlPoint> points;
    for (int i = 0; i < 9; ++i) {
        const float v = static_cast<float>(i) / 8.0f;
        points.push_back(
            {v, volume::RgbaColor{0.0f, kTfGreen, 0.0f, kTfAlpha}});
    }
    volume::TransferFunction tooMany(std::move(points));
    auto dataset = std::make_shared<const data::VolumeDataset>(makeUniformDataset());

    render::VolumeInstance instance{dataset, tooMany, glm::mat4(1.0f)};
    render::VolumeScene scene;
    scene.volumes.push_back(instance);

    auto rendererPtr = std::make_shared<render::VolumeRenderer>();
    render::View view(render::ViewRect{0, 0, static_cast<int>(kTargetWidth),
                                       static_cast<int>(kTargetHeight)},
                      glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    view.setCamera(makeCamera());
    view.addItem(scene, rendererPtr);
    auto result = view.renderWithEnsure();
    EXPECT_TRUE(result.failed());
    EXPECT_NE(result.error().message.find("more than 8 control points"),
              std::string::npos);
}

} // namespace re::tests
