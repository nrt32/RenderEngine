// tests/t18_depth_test.cpp — T18 gate (depth-buffer support).
//
// Architecture-review finding being closed: v1 framebuffers were color-only
// everywhere, which forced painter's-order workarounds (the MPR box emits its
// faces in draw order so the last-drawn near face wins) and blocked correct
// opaque meshes under order-independent transparency. This gate proves the new
// opt-in depth path:
//
//   (1) A ViewTarget created with DepthMode::Enabled owns a DEPTH_COMPONENT24
//       depth attachment and its framebuffer checks COMPLETE WITH that
//       attachment attached (a driver silently dropping it fails loudly here).
//       The default create() stays color-only: no depth attachment, which is
//       the deterministic painter's-order configuration every prior analytic
//       pixel gate asserts against on software GL (llvmpipe).
//   (2) FR acceptance: two overlapping opaque meshes at different z, drawn in
//       anti-painter order (NEAR first, FAR last). With the per-view depth
//       test enabled the NEARER mesh's color wins the overlap pixel even
//       though it was drawn FIRST; with the default color-only pass the
//       LATER-DRAWN mesh wins. The two expected colors differ by 128 in R vs
//       G — far outside the 1/255 tolerance — so the probe discriminates the
//       occlusion semantics exactly.
//   (3) The flag flip / resize contract: flipping setDepthTest recreates the
//       inner target with the matching depth mode, and resizing preserves it.
//
//   (4) LinkedListOIT end-to-end stays green THROUGH a depth-enabled target:
//       the classic two-transparent-quads scene composites to the same
//       analytic depth-ordered premultiplied blend, because both OIT passes
//       run with the depth test off exactly as before (the capture draws
//       behind MeshRenderer::render's default depth-off beginPass prologue;
//       the composite issues its own explicit core::disableDepthTest()) —
//       the depth attachment simply sits unused behind them.
//
// Analytic constants (docs/render.md OIT math): near quad {0.4,0.2,0.1,0.5}
// at z=0 over far quad {0.1,0.6,0.3,0.4} at z=-1 =>
//   rgb = {0.2,0.1,0.05} + (1-0.5)*{0.04,0.24,0.12} = {0.22,0.22,0.11}
//   a   = 0.5 + (1-0.5)*0.4                          = 0.70
//   bytes = {round(0.22*255), round(0.22*255), round(0.11*255),
//            round(0.70*255)} = {56, 56, 28, 179}
// Occlusion probes: opaque flat-shaded quads facing +Z shade to EXACTLY their
// base color under the fixed head-on light (ambient 0, diffuse 1, light dir
// +Z => dot = 1), so byte values are round(color*255): green {0,0.5,0,1} ->
// {0,128,0,255}, red {0.5,0,0,1} -> {128,0,0,255}.
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers (utils::PixelReader delegates the raw readback to core/) — no raw
// glXxx calls.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "data/mesh.hpp"
#include "core/gl_error.hpp"
#include "render/asset_registry.hpp"
#include "render/imaterial.hpp"
#include "render/linked_list_oit.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "render/view.hpp"
#include "render/view_target.hpp"
#include "tests/offscreen_fixture.hpp"
#include "tests/test_helpers.hpp"
#include "utils/pixel_reader.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants (T18 gate + FR acceptance arrangement).
// ---------------------------------------------------------------------------

// Target / view size: 64x64 physical pixels; center pixel (32, 32).
constexpr std::uint32_t kTargetWidth = 64u;
constexpr std::uint32_t kTargetHeight = 64u;
constexpr std::uint32_t kCenterX = kTargetWidth / 2u;  // 32
constexpr std::uint32_t kCenterY = kTargetHeight / 2u; // 32

// Color tolerance: 1/255 per the project's acceptance convention.
constexpr int kColorTolerance = 1;

// The two OPAQUE overlap meshes (FR acceptance): nearer mesh GREEN, farther
// mesh RED. Bytes are round(component * 255) because a +Z-facing surface
// shades at exactly its base color under the fixed headlight.
constexpr glm::vec4 kNearColor(0.0f, 0.5f, 0.0f, 1.0f); // -> {0, 128, 0, 255}
constexpr glm::vec4 kFarColor(0.5f, 0.0f, 0.0f, 1.0f);  // -> {128, 0, 0, 255}
constexpr std::uint8_t kNearG = 128u;
constexpr std::uint8_t kFarR = 128u;

// The transparent pair of the OIT regression case (docs/render.md math above).
constexpr glm::vec4 kOitNearColor(0.4f, 0.2f, 0.1f, 0.5f); // z = 0
constexpr glm::vec4 kOitFarColor(0.1f, 0.6f, 0.3f, 0.4f);  // z = -1
constexpr std::uint8_t kOitExpectedR = 56u;  // round(0.22 * 255)
constexpr std::uint8_t kOitExpectedG = 56u;  // round(0.22 * 255)
constexpr std::uint8_t kOitExpectedB = 28u;  // round(0.11 * 255)
constexpr std::uint8_t kOitExpectedA = 179u; // round(0.70 * 255)

// Both OIT quads are full-screen, so exactly 2 fragments per pixel are
// captured: 64 * 64 * 2 = 8192.
constexpr std::uint32_t kExpectedCapturedFragments =
    kTargetWidth * kTargetHeight * 2u;

// Sample points inside both quads' coverage (readback coords, y up).
constexpr std::uint32_t kProbeX[3] = {32u, 8u, 56u};
constexpr std::uint32_t kProbeY[3] = {32u, 56u, 8u};

// ---------------------------------------------------------------------------
// Helpers (same golden shapes as the established mesh/OIT gates).
// ---------------------------------------------------------------------------

/// Golden +Z-facing quad covering [-1,1]^2 at z=0 (two CCW triangles).
/// Camera at (0,0,5) looking down -Z; ortho maps NDC [-1,1]^2 onto the full
/// 64x64 viewport. World z=0 sits 5 units from the eye, world z=-1 sits 6 —
/// both inside the [0.1, 10] clip range, at distinct depths.
/// Read back one RGBA8 pixel of `framebuffer` at (x, y) via utils::PixelReader
/// (the raw readback stays under core/).
/// One shared fixture piece: registry + golden quad handle + the two opaque
/// materials + a renderer. All co-owned via shared_ptr so teardown order
/// between view items, scenes, and this fixture can never dangle.
struct OverlapFixture {
    std::shared_ptr<render::AssetRegistry> registry{
        std::make_shared<render::AssetRegistry>()};
    data::Mesh quad{makeQuad()};
    render::AssetHandle handle{};
    std::shared_ptr<render::PhongMaterial> nearMaterial{
        std::make_shared<render::PhongMaterial>(kNearColor)};
    std::shared_ptr<render::PhongMaterial> farMaterial{
        std::make_shared<render::PhongMaterial>(kFarColor)};
    std::shared_ptr<render::MeshRenderer> renderer{
        std::make_shared<render::MeshRenderer>(registry)};

    OverlapFixture() {
        const auto registered = registry->registerAsset(quad);
        EXPECT_TRUE(registered.ok()) << registered.error().message;
        if (registered.ok()) {
            handle = *registered;
        }
    }

    /// A MeshScene holding ONLY the near quad (world z=0, distance 5).
    render::MeshScene nearScene() const {
        render::MeshScene s;
        s.meshes.push_back(render::MeshInstance{handle, nearMaterial, glm::mat4(1.0f)});
        return s;
    }
    /// A MeshScene holding ONLY the far quad (translated to z=-1, distance 6).
    render::MeshScene farScene() const {
        render::MeshScene s;
        s.meshes.push_back(render::MeshInstance{
            handle, farMaterial,
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.0f))});
        return s;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// (1) Depth-enabled target completeness; color-only default unchanged.
// ---------------------------------------------------------------------------

TEST(T18Depth, DepthEnabledTargetIsCompleteAndOwnsItsDepthAttachment) {
    auto enabled = render::ViewTarget::create(kTargetWidth, kTargetHeight,
                                              render::DepthMode::Enabled);
    ASSERT_TRUE(enabled.ok()) << enabled.error().message;
    EXPECT_TRUE(enabled->hasDepth());
    ASSERT_NE(enabled->depth(), nullptr);
    EXPECT_NE(enabled->depth()->id(), 0u)
        << "a generated GL texture name is never 0";
    EXPECT_NE(enabled->depth()->id(), enabled->color().id())
        << "depth attachment and color attachment are distinct textures";
    // Completeness asserted WITH the depth attachment bound: re-check through
    // core::Framebuffer::isComplete while the FBO is bound.
    enabled->framebuffer().bind();
    EXPECT_TRUE(enabled->framebuffer().isComplete());
    enabled->framebuffer().unbind();

    // The DEFAULT factory path stays color-only — the deterministic-gate
    // configuration every prior analytic pixel gate asserts against.
    auto colorOnly = render::ViewTarget::create(kTargetWidth, kTargetHeight);
    ASSERT_TRUE(colorOnly.ok()) << colorOnly.error().message;
    EXPECT_FALSE(colorOnly->hasDepth());
    EXPECT_EQ(colorOnly->depth(), nullptr);
    EXPECT_EQ(colorOnly->depthMode(), render::DepthMode::ColorOnly);
    colorOnly->framebuffer().bind();
    EXPECT_TRUE(colorOnly->framebuffer().isComplete());
    colorOnly->framebuffer().unbind();
}

// ---------------------------------------------------------------------------
// (2)+(3) FR acceptance: overlap pixel ownership under both depth modes.
// ---------------------------------------------------------------------------

TEST(T18Depth, DepthTestOnNearerMeshWinsTheOverlapPixel) {
    OverlapFixture f;
    render::View view(render::ViewRect{0, 0, static_cast<int>(kTargetWidth),
                                       static_cast<int>(kTargetHeight)},
                      glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    view.setCamera(makeCamera());
    view.setDepthTest(true);

    // ANTI-painter draw order on purpose: the NEARER mesh is added FIRST, so
    // painter's order would let the later-drawn farther mesh win. Only true
    // depth-tested occlusion can put the nearer color on top.
    view.addItem(f.nearScene(), f.renderer);
    view.addItem(f.farScene(), f.renderer);

    core::DrawContext ctx;
    auto rendered = view.renderWithEnsure(ctx);
    ASSERT_TRUE(rendered.ok()) << rendered.error().message;
    ASSERT_NE(view.target(), nullptr);
    EXPECT_TRUE(view.target()->hasDepth())
        << "a depth-tested view renders into a target that owns a depth "
           "attachment";

    // Every probed pixel shows the NEARER mesh's exact base color (drawn
    // FIRST yet kept by the depth test), within 1/255.
    for (int i = 0; i < 3; ++i) {
        expectPixel(readPixel(view.target()->framebuffer(), kProbeX[i], kProbeY[i]),
                    0, static_cast<int>(kNearG), 0, "depth-on overlap probe");
    }
    EXPECT_FALSE(core::hasPendingGlError());
}

TEST(T18Depth, DepthOffDefaultKeepsLaterDrawnMeshAtTheOverlapPixel) {
    OverlapFixture f;
    render::View view(render::ViewRect{0, 0, static_cast<int>(kTargetWidth),
                                       static_cast<int>(kTargetHeight)},
                      glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    view.setCamera(makeCamera());
    // Default flag: depthTest() == false, target stays color-only.
    EXPECT_FALSE(view.depthTest());

    view.addItem(f.nearScene(), f.renderer);
    view.addItem(f.farScene(), f.renderer);

    core::DrawContext ctx;
    auto rendered = view.renderWithEnsure(ctx);
    ASSERT_TRUE(rendered.ok()) << rendered.error().message;
    ASSERT_NE(view.target(), nullptr);
    EXPECT_FALSE(view.target()->hasDepth())
        << "the default view must not allocate a depth attachment";

    // Same arrangement, same draw order — but painter's-order now: the
    // LATER-DRAWN (farther, red) mesh wins every probed pixel.
    for (int i = 0; i < 3; ++i) {
        expectPixel(readPixel(view.target()->framebuffer(), kProbeX[i], kProbeY[i]),
                    static_cast<int>(kFarR), 0, 0, "color-only overlap probe");
    }
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (3) Flag-flip / resize contract of the per-view mode.
// ---------------------------------------------------------------------------

TEST(T18Depth, SetDepthTestRecreatesTargetAndResizePreservesMode) {
    OverlapFixture f;
    render::View view(render::ViewRect{0, 0, 64, 64});
    ASSERT_TRUE(view.ensureTarget().ok());
    ASSERT_NE(view.target(), nullptr);
    EXPECT_FALSE(view.target()->hasDepth());

    // Flipping the flag flips the target's depth mode on the next
    // ensureTarget (inner-target recreate; the View itself persists).
    view.setDepthTest(true);
    ASSERT_TRUE(view.ensureTarget().ok());
    ASSERT_NE(view.target(), nullptr);
    EXPECT_TRUE(view.target()->hasDepth());
    EXPECT_EQ(view.target()->depthMode(), render::DepthMode::Enabled);
    EXPECT_EQ(view.target()->width(), 64u);
    EXPECT_EQ(view.target()->height(), 64u);

    // Resizing preserves the enabled depth mode (resize never silently turns
    // an occlusion-capable view into a painter's-order one).
    view.setRect(render::ViewRect{0, 0, 32, 48});
    ASSERT_TRUE(view.ensureTarget().ok());
    ASSERT_NE(view.target(), nullptr);
    EXPECT_EQ(view.target()->width(), 32u);
    EXPECT_EQ(view.target()->height(), 48u);
    EXPECT_TRUE(view.target()->hasDepth());
}

// ---------------------------------------------------------------------------
// (4) LinkedListOIT end-to-end stays green through a depth-enabled target.
// ---------------------------------------------------------------------------

TEST(T18Depth, LinkedListOitCompositeUnchangedThroughDepthEnabledTarget) {
    data::Mesh quad = makeQuad();
    auto registry = std::make_shared<render::AssetRegistry>();
    const auto handle = registry->registerAsset(quad);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    auto nearMaterial = std::make_shared<render::PhongMaterial>(kOitNearColor);
    auto farMaterial = std::make_shared<render::PhongMaterial>(kOitFarColor);
    ASSERT_TRUE(nearMaterial->isTransparent());
    ASSERT_TRUE(farMaterial->isTransparent());

    render::MeshScene scene;
    scene.meshes.push_back(
        render::MeshInstance{*handle, nearMaterial, glm::mat4(1.0f)});
    scene.meshes.push_back(render::MeshInstance{
        *handle, farMaterial,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.0f))});

    // The SAME analytic scene as the established OIT gate, rendered into a
    // DEPTH-ENABLED ViewTarget: both OIT passes keep the depth test off
    // exactly as on a color-only target (the capture draws behind this direct
    // render's default depth-off beginPass prologue; end() issues its own
    // explicit core::disableDepthTest()), so the composite bytes are identical
    // — the depth attachment simply sits unused behind them.
    auto target = render::ViewTarget::create(kTargetWidth, kTargetHeight,
                                             render::DepthMode::Enabled);
    ASSERT_TRUE(target.ok()) << target.error().message;
    EXPECT_TRUE(target->hasDepth());

    render::RenderTarget rt;
    rt.framebuffer = &target->framebuffer();
    rt.width = kTargetWidth;
    rt.height = kTargetHeight;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    auto pipeline = std::make_shared<render::LinkedListOIT>();
    render::MeshRenderer renderer(registry, pipeline);
    auto result = renderer.render(scene, makeCamera(), rt);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(core::hasPendingGlError());

    for (int i = 0; i < 5; ++i) {
        static constexpr std::uint32_t kSampleX[5] = {32u, 8u, 56u, 8u, 56u};
        static constexpr std::uint32_t kSampleY[5] = {32u, 8u, 8u, 56u, 56u};
        expectPixel(readPixel(target->framebuffer(), kSampleX[i], kSampleY[i]),
                    kOitExpectedR, kOitExpectedG, kOitExpectedB,
                    "OIT-over-depth-enabled-target probe", kOitExpectedA);
    }

    // Exactly two captured fragments per pixel (both full-screen transparent
    // quads), proving the capture pass ran unmodified on the depth-enabled
    // target (test-consumed counter readback, guardrail no_production_readback).
    const auto captured = pipeline->readCapturedFragmentCount();
    ASSERT_TRUE(captured.ok()) << captured.error().message;
    EXPECT_EQ(*captured, kExpectedCapturedFragments);
    EXPECT_FALSE(pipeline->isEngaged());
}

} // namespace re::tests
