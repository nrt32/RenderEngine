// tests/t23_resize_test.cpp — T23 gate (sample-harness resize handling).
//
// The review finding: there was no GLFW framebuffer-size event path — the
// harness re-read the window size lazily, but every sample except MPR baked
// its camera aspect from compile-time window constants, so a window resize
// distorted geometry. T23 wires core::Window's framebuffer-size callback
// (stored pixel size + dirty latch), delivers ISample::onResize(width,height)
// from the harness, and makes every sample derive rect + camera aspect from
// the LIVE pixel dims each frame.
//
// Gate assertions (R4 evidence rule — every check is an explainable constant):
//
//   (1) The framebuffer-size state machine stores each event's physical pixel
//       size and latches exactly one dirty delivery per event batch: fresh
//       state is 0x0 with no pending resize; apply(1024,768) reports 1024x768
//       and consumes true once; a later event re-latches.
//   (2) THE SIMULATED-RESIZE PROJECTION GATE: applying the resize hook's
//       exact code (applyFraming) with new dims 1280x720
//       makes the view's projection matrix equal glm::perspective(fov,
//       1280/720, near, far) within 1e-6 in ALL 16 entries, and the analytic
//       [0][0] scalars f/aspect hold (f = 1/tan(fov/2) = sqrt(3) for fov 60°:
//       800x600 -> 3*sqrt(3)/4, 1280x720 -> 9*sqrt(3)/16). Eye/center/up are
//       untouched (only projGen moves), repeated same-size calls are free
//       (change-guarded setters: generation stable), and the degenerate-dims
//       clamp (each non-positive dim acts as 1 pixel) yields the EXACT
//       glm::perspective matrix at aspect 1.
//   (3) The recomputed projection reaches the RENDER side through the exact
//       path samples drive: two syncs through AppContext/IViewBridge (frame 1
//       at the opening size, then the simulated resize + next frame) leave
//       the compositor's ReView carrying proj == glm::perspective(fov,
//       newAspect, ...) within 1e-6, rect == the new pixels, at the SAME
//       ReView address (resize recreates only the target, never identity).
//   (4) Through the ISample& base interface (the exact dispatch the harness
//       holds), onResize carries the current pixel dims verbatim to an
//       overriding sample (recorded dims == delivered dims), and a sample
//       without an override resolves to the base no-op: it fabricates no
//       frame and leaves observable state untouched.
//   (5) MPR grid re-resolves from live dims: mprViewports(1000,800) splits
//       into four equal 500x400 quadrants at the pinned grid positions
//       (existing T14 quadrant math), while mprViewports(1280,960) still
//       yields the SPEC FR-app.2 constants (regression anchor).
//
// Per the GL/readback guardrails this file touches no raw glXxx: tests (1)-(2)
// and (4)-(5) are pure CPU value math; test (3) runs on the shared offscreen
// GL fixture through broker/render wrappers only.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "app/mpr_slice.hpp"
#include "app/sample_harness.hpp"
#include "broker/app_context.hpp"
#include "core/framebuffer_size_state.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "render/view.hpp"
#include "scene/camera.hpp"
#include "scene/object.hpp"
#include "scene/view.hpp"

namespace re::tests {
namespace {

namespace app = re::app;
namespace scene = re::scene;

// ---------------------------------------------------------------------------
// Explainable constants.
// ---------------------------------------------------------------------------

// The framing the full-window samples share analytically: vertical fov 60°,
// near 0.1. For fov = 60° the perspective matrix's [0][0] entry equals
// f / aspect with f = 1/tan(30°) = sqrt(3) — the closed-form scalars below.
constexpr float kFovDeg = 60.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 20.0f;
constexpr double kSqrt3 = 1.7320508075688772;

const scene::PerspectiveFraming kFraming{kFovDeg, kNearPlane, kFarPlane};

inline void applyFraming(scene::View& view, const scene::PerspectiveFraming& framing, int width, int height) {
    view.setRect(scene::Rect{0, 0, width, height});
    scene::Camera cam = view.camera;
    cam.setPerspectiveFromFraming(framing, app::aspectFromDims(width, height));
    view.setCamera(std::move(cam));
}

/// All 16 entries of `got` must equal `want` within `tol`. The tolerance is
/// the project's math acceptance floor (1e-6, SPEC §4): a projection matrix
/// feeds the extraction/raster pipelines directly, so any entry drifting
/// beyond it would displace geometry by more than the gates allow — comparing
/// every entry (not just [0][0]) is what makes "matches glm::perspective"
/// mean the whole matrix.
void expectMatNear(const glm::mat4& got, const glm::mat4& want, float tol) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(got[col][row], want[col][row], tol)
                << "[" << col << "][" << row << "]";
        }
    }
}

/// A minimal non-overriding sample: implements the pure interface only, so
/// onResize resolves to the documented base no-op. It records its frame dims
/// so the test can OBSERVE the no-op contract: a resize delivery must leave
/// the recorded frame untouched (no fabricated frame, no dim change).
class NoHookSample final : public app::ISample {
   public:
    data::Result<void> renderFrame(int width, int height) override {
        lastFrameWidth_ = width;
        lastFrameHeight_ = height;
        return data::Result<void>(data::value);
    }
    const char* title() const override { return "no-hook probe"; }

    int lastFrameWidth() const noexcept { return lastFrameWidth_; }
    int lastFrameHeight() const noexcept { return lastFrameHeight_; }

   private:
    int lastFrameWidth_{-1};
    int lastFrameHeight_{-1};
};

/// A recording sample: captures every direct onResize delivery (this mirrors
/// what SampleHarness::run does when the framebuffer-size latch is set).
class RecordingSample final : public app::ISample {
   public:
    data::Result<void> renderFrame(int width, int height) override {
        lastFrameWidth_ = width;
        lastFrameHeight_ = height;
        return data::Result<void>(data::value);
    }
    const char* title() const override { return "recording probe"; }
    void onResize(int width, int height) noexcept override {
        ++resizeCalls_;
        lastResizeWidth_ = width;
        lastResizeHeight_ = height;
    }

    int resizeCalls() const noexcept { return resizeCalls_; }
    int lastResizeWidth() const noexcept { return lastResizeWidth_; }
    int lastResizeHeight() const noexcept { return lastResizeHeight_; }
    int lastFrameWidth() const noexcept { return lastFrameWidth_; }
    int lastFrameHeight() const noexcept { return lastFrameHeight_; }

   private:
    int resizeCalls_{0};
    int lastResizeWidth_{-1};
    int lastResizeHeight_{-1};
    int lastFrameWidth_{-1};
    int lastFrameHeight_{-1};
};

} // namespace

// ---------------------------------------------------------------------------
// (1) Framebuffer-size state machine: stored size + one-shot dirty latch.
// ---------------------------------------------------------------------------

TEST(T23Resize, FramebufferSizeStateStoresSizeAndLatchesOncePerEventBatch) {
    core::FramebufferSizeState state;

    // Fresh state: nothing seen yet, nothing pending — the initial constants.
    EXPECT_EQ(state.width, 0);
    EXPECT_EQ(state.height, 0);
    EXPECT_FALSE(state.consumeResized());

    // One event: both dims overwritten, exactly one delivery.
    state.apply(1024, 768);
    EXPECT_EQ(state.width, 1024);
    EXPECT_EQ(state.height, 768);
    EXPECT_TRUE(state.consumeResized());
    EXPECT_FALSE(state.consumeResized()) << "latch must clear on delivery";

    // A later event latches again (per-event semantics, not sticky-once).
    state.apply(640, 480);
    EXPECT_EQ(state.width, 640);
    EXPECT_EQ(state.height, 480);
    EXPECT_TRUE(state.consumeResized());
    EXPECT_FALSE(state.consumeResized());
}

// ---------------------------------------------------------------------------
// (2) Simulated-resize projection gate (scene-side, analytic).
// ---------------------------------------------------------------------------

TEST(T23Resize, SimulatedResizeRecomputesProjectionAnalytically) {
    scene::View view;

    // Frame 1 at the opening window size 800x600 (aspect 4/3).
    applyFraming(view, kFraming, 800, 600);
    const glm::mat4 expectedInitial =
        glm::perspective(glm::radians(kFovDeg), 800.0f / 600.0f, kNearPlane,
                         kFarPlane);
    expectMatNear(view.camera.projMatrix(), expectedInitial, 1e-6f);
    // Closed form: [0][0] = f/aspect, f = sqrt(3) at fov 60°, aspect = 4/3.
    EXPECT_NEAR(view.camera.projMatrix()[0][0],
                static_cast<float>(kSqrt3) * 3.0f / 4.0f, 1e-6f);
    ASSERT_EQ(view.rect.x, 0);
    ASSERT_EQ(view.rect.y, 0);
    ASSERT_EQ(view.rect.w, 800);
    ASSERT_EQ(view.rect.h, 600);

    // THE SIMULATED RESIZE: call the hook's exact code directly with the new
    // harness pixel dims (SampleHarness::run delivers onResize(w,h); the
    // samples' hooks all route through this one function).
    const glm::vec3 eyeBefore = view.camera.eye();
    const uint64_t viewGenBefore = view.camera.viewGen();
    applyFraming(view, kFraming, 1280, 720);

    // Next-frame projection matches glm::perspective at the NEW aspect within
    // 1e-6 in every entry, plus the closed-form horizontal scale.
    const glm::mat4 expectedResized =
        glm::perspective(glm::radians(kFovDeg), 1280.0f / 720.0f, kNearPlane,
                         kFarPlane);
    expectMatNear(view.camera.projMatrix(), expectedResized, 1e-6f);
    EXPECT_NEAR(view.camera.projMatrix()[0][0],
                static_cast<float>(kSqrt3) * 9.0f / 16.0f, 1e-6f);

    // Only the PROJECTION moved: the eye framing stays put and the per-field
    // split bumps projGen, never viewGen (SPEC §10.4 contract). This split is
    // what makes a resize cheap downstream — the camera mapper re-translates
    // only the projection field, while the view matrix (and everything cached
    // against it) stays untouched, so no other dirty path may observe a
    // resize as a camera-motion change.
    EXPECT_EQ(view.camera.eye(), eyeBefore);
    EXPECT_EQ(view.camera.viewGen(), viewGenBefore);

    // Rect tracks the live pixels (blits stay 1:1 after the resize).
    EXPECT_EQ(view.rect.w, 1280);
    EXPECT_EQ(view.rect.h, 720);
}

TEST(T23Resize, SameSizeReapplicationIsFreeAndDegenerateDimsClamp) {
    scene::View view;
    applyFraming(view, kFraming, 1280, 720);
    const uint64_t genBefore = view.generation;
    const uint64_t cameraGenBefore = view.cameraGen;

    // Re-applying identical dims must not churn generations: the change-
    // guarded setters make per-frame live-dims derivation free when nothing
    // resized (no sync work downstream either).
    applyFraming(view, kFraming, 1280, 720);
    EXPECT_EQ(view.generation, genBefore);
    EXPECT_EQ(view.cameraGen, cameraGenBefore);

    // Degenerate dims (GLFW reports 0 for minimized windows) clamp to a
    // finite ratio instead of producing inf/NaN projections; each non-
    // positive dim acts as 1 pixel.
    EXPECT_FLOAT_EQ(app::aspectFromDims(0, 600), 1.0f / 600.0f);
    EXPECT_FLOAT_EQ(app::aspectFromDims(800, 0), 800.0f);
    EXPECT_FLOAT_EQ(app::aspectFromDims(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(app::aspectFromDims(800, 600), 800.0f / 600.0f);

    // And the clamped path yields the EXACT analytic projection: the 0x0
    // clamp pins aspect to exactly 1.0 above, so every entry must equal
    // glm::perspective at aspect 1 within 1e-6 — [0][0] = f/1 = sqrt(3),
    // [1][1] = f, etc.
    applyFraming(view, kFraming, 0, 0);
    expectMatNear(view.camera.projMatrix(),
                  glm::perspective(glm::radians(kFovDeg), 1.0f, kNearPlane,
                                   kFarPlane),
                  1e-6f);
}

// ---------------------------------------------------------------------------
// (3) The resized projection reaches the render side through the bridge.
// ---------------------------------------------------------------------------

TEST(T23Resize, NextFrameProjectionReachesRenderSideThroughBridge) {
    broker::AppContext ctx(broker::AppContext::Params{});

    // One mesh item so the view syncs into a real layer-carrying ReView (the
    // full-window composition every perspective sample drives).
    std::vector<glm::vec3> positions = {
        glm::vec3(-0.25f, -0.25f, 0.0f), glm::vec3(0.25f, -0.25f, 0.0f),
        glm::vec3(0.25f, 0.25f, 0.0f),   glm::vec3(-0.25f, 0.25f, 0.0f)};
    std::vector<std::uint32_t> indices = {0u, 1u, 2u, 0u, 2u, 3u};
    auto quad = std::make_shared<const data::Mesh>(
        data::Mesh::fromTriangles(std::move(positions), std::move(indices)));
    scene::MeshObject mo;
    mo.mesh = quad;
    mo.transform = glm::mat4(1.0f);
    const std::uint64_t meshId = ctx.store().addMeshObject(std::move(mo));

    scene::View view;
    view.id = 1;
    view.setItemIds({meshId});
    view.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));

    // Frame 1 at the opening size: sync translates the view into the cached
    // ReView (identity key {layout 0, view 1}).
    applyFraming(view, kFraming, 800, 600);
    std::vector<scene::View> frame{view};
    auto first = ctx.bridge().sync(frame, ctx.store());
    ASSERT_TRUE(first.ok()) << first.error().message;
    render::View* rv = ctx.compositor()->getView(0, 1);
    ASSERT_NE(rv, nullptr) << "first sync must create the ReView";
    render::View* const identityBefore = rv;
    expectMatNear(rv->camera().proj,
                  glm::perspective(glm::radians(kFovDeg), 800.0f / 600.0f,
                                   kNearPlane, kFarPlane),
                  1e-6f);

    // Simulated resize + NEXT FRAME: the hook applies the new dims, the
    // sample's next renderFrame pushes the same values through sync again.
    applyFraming(view, kFraming, 1280, 720);
    frame.assign(1, view);
    auto second = ctx.bridge().sync(frame, ctx.store());
    ASSERT_TRUE(second.ok()) << second.error().message;

    // The render-side ReView now carries EXACTLY the analytic next-frame
    // projection at the new aspect, the rect equals the new pixels, and the
    // ReView identity survived (resize recreates only the inner target FBO,
    // never the persistent view object).
    expectMatNear(rv->camera().proj,
                  glm::perspective(glm::radians(kFovDeg), 1280.0f / 720.0f,
                                   kNearPlane, kFarPlane),
                  1e-6f);
    EXPECT_NEAR(rv->camera().proj[0][0],
                static_cast<float>(kSqrt3) * 9.0f / 16.0f, 1e-6f);
    EXPECT_EQ(rv->rect().width, 1280);
    EXPECT_EQ(rv->rect().height, 720);
    EXPECT_EQ(ctx.compositor()->getView(0, 1), identityBefore)
        << "resize must keep &ReView identity (target recreate only)";
}

// ---------------------------------------------------------------------------
// (4) The ISample::onResize hook contract.
// ---------------------------------------------------------------------------

TEST(T23Resize, ResizeHookCarriesPixelDimsVerbatimAndBaseDefaultIsNoOp) {
    RecordingSample recorder;
    NoHookSample silent;
    // Both probes are driven through the BASE interface exactly like the
    // harness holds them (SampleHarness owns a unique_ptr<ISample>), so the
    // virtual dispatch contract is what is under test.
    app::ISample& recorderAsInterface = recorder;
    app::ISample& silentAsInterface = silent;

    recorderAsInterface.onResize(1280, 720);
    EXPECT_EQ(recorder.resizeCalls(), 1);
    EXPECT_EQ(recorder.lastResizeWidth(), 1280);
    EXPECT_EQ(recorder.lastResizeHeight(), 720);

    // A burst of coalesced events delivers the LATEST size again (idempotent
    // per the hook contract): two deliveries, second carries the newer dims.
    recorderAsInterface.onResize(640, 480);
    EXPECT_EQ(recorder.resizeCalls(), 2);
    EXPECT_EQ(recorder.lastResizeWidth(), 640);
    EXPECT_EQ(recorder.lastResizeHeight(), 480);

    // A sample that does not override resolves to the base no-op: through the
    // same interface call it records NO frame (the sentinels survive) —
    // onResize must not fabricate or disturb renderFrame state. The next real
    // frame then carries its own dims verbatim.
    ASSERT_EQ(silent.lastFrameWidth(), -1);
    ASSERT_EQ(silent.lastFrameHeight(), -1);
    silentAsInterface.onResize(-1, -1);
    EXPECT_EQ(silent.lastFrameWidth(), -1)
        << "base no-op must not fabricate frame dims";
    EXPECT_EQ(silent.lastFrameHeight(), -1)
        << "base no-op must not fabricate frame dims";
    auto frame = silentAsInterface.renderFrame(320, 240);
    ASSERT_TRUE(frame.ok()) << frame.error().message;
    EXPECT_EQ(silent.lastFrameWidth(), 320);
    EXPECT_EQ(silent.lastFrameHeight(), 240);

    // And renderFrame itself delivers pixel dims verbatim to an overriding
    // sample through the interface too.
    auto recorded = recorderAsInterface.renderFrame(800, 600);
    ASSERT_TRUE(recorded.ok()) << recorded.error().message;
    EXPECT_EQ(recorder.lastFrameWidth(), 800);
    EXPECT_EQ(recorder.lastFrameHeight(), 600);
}

// ---------------------------------------------------------------------------
// (5) MPR grid re-resolves from live dims (existing T14 quadrant math).
// ---------------------------------------------------------------------------

TEST(T23Resize, MprGridReResolvesFromLiveWindowDims) {
    // Regression anchor: the SPEC FR-app.2 opening window still yields the
    // pinned T14 layout (T top-left, C top-right, S bottom-left, 3D bottom-
    // right; four equal 640x480 quadrants of 1280x960, y up).
    const std::array<app::MprViewport, 4> spec =
        app::mprViewports(1280, 960);
    EXPECT_EQ(spec[0].x, 0);
    EXPECT_EQ(spec[0].y, 480);
    EXPECT_EQ(spec[0].width, 640);
    EXPECT_EQ(spec[0].height, 480);
    EXPECT_EQ(spec[1].x, 640);
    EXPECT_EQ(spec[1].y, 480);
    EXPECT_EQ(spec[1].width, 640);
    EXPECT_EQ(spec[1].height, 480);
    EXPECT_EQ(spec[2].x, 0);
    EXPECT_EQ(spec[2].y, 0);
    EXPECT_EQ(spec[2].width, 640);
    EXPECT_EQ(spec[2].height, 480);
    EXPECT_EQ(spec[3].x, 640);
    EXPECT_EQ(spec[3].y, 0);
    EXPECT_EQ(spec[3].width, 640);
    EXPECT_EQ(spec[3].height, 480);

    // A RESIZED window re-splits the same formula over the live dims:
    // 1000x800 -> halfW=500, halfH=400, top row at y = halfH (GL bottom-left
    // origin), quadrants tiling the window exactly.
    const std::array<app::MprViewport, 4> resized =
        app::mprViewports(1000, 800);
    EXPECT_EQ(resized[0].x, 0);
    EXPECT_EQ(resized[0].y, 400);
    EXPECT_EQ(resized[0].width, 500);
    EXPECT_EQ(resized[0].height, 400);
    EXPECT_EQ(resized[1].x, 500);
    EXPECT_EQ(resized[1].y, 400);
    EXPECT_EQ(resized[1].width, 500);
    EXPECT_EQ(resized[1].height, 400);
    EXPECT_EQ(resized[2].x, 0);
    EXPECT_EQ(resized[2].y, 0);
    EXPECT_EQ(resized[2].width, 500);
    EXPECT_EQ(resized[2].height, 400);
    EXPECT_EQ(resized[3].x, 500);
    EXPECT_EQ(resized[3].y, 0);
    EXPECT_EQ(resized[3].width, 500);
    EXPECT_EQ(resized[3].height, 400);

    // Quadrants tile the resized window exactly (no gaps, no overlap).
    EXPECT_EQ(resized[0].width + resized[1].width, 1000);
    EXPECT_EQ(resized[2].width + resized[3].width, 1000);
    EXPECT_EQ(resized[0].height + resized[2].height, 800);
    EXPECT_EQ(resized[1].height + resized[3].height, 800);
}

} // namespace re::tests
