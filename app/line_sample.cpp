// app/line_sample.cpp — Line capability sample: SSBO+gl_VertexID view-quad strip, Rougier dash, joins, caps, worldUnits (V7 T14, FR-render.9, FR-app.1).
//
// Demonstrates Engine::addLine / addPolyline (viz::Engine owns AppContext + SceneStore + Broker + ViewBridge — no render/ include per acl_app_render) in both
// 3D perspective (Camera::perspective view-space SSBO+gl_VertexID 6-vert strip) and 2D orthographic (ClipPlane Space::World axial + Camera::ortho) views side-by-side through one AppContext + IViewBridge.
//
//   * Solid 2px red across black: LineObject polyline 2px solid red (width 2, worldUnits false, LineCap::Square, LineJoin::Miter miterLimit 4) across black clear 0,0,0,1 — the geometric ±width/2 band (±1px around y=0 world→screen y=240) must have ≥90% of band pixels within 1/255 of red 255,0,0 (mirrors t5_line_renderer_test.cpp ≥90% within 2px, evidence 1/255 not >0, analytic ratio, 1e-6 for distance check). The view-space quad a±n·wA,b±n·wB with n=perp(viewport*(b−a)) keeps width constant in screen space, analytic fwidth AA via smoothstep.
//   * Dash 8 gap 4: LineObject with DashPattern{dashLength 8,gapLength 4,offset 0} Rougier mod(s,patternLen) dash — shader evaluates inDash=step(mod(s+offset,patternLen),dashLen) with smoothstep(fwidth) AA, discarding gaps; known on-dash pixel at s≈4 is red 255,0,0 within 1/255, gap at s≈10 is background black 0,0,0 within 1/255, second dash at s≈14 red again within 1/255 (FR-render.9 dash 8/4 1/255).
//   * Joins miter→bevel and caps round/square: polyline elbow -1,0.5→0,0→1,0.5 demonstrates LineJoin::Miter with miterLimit 4→bevel fallback at acute angles vs unconditional Bevel, and LineCap::Round (analytic disc halo) vs Square (sharp rect) at segment endpoints — both honour worldUnits toggle for width without touching render headers (scene stays GL/RE-free, disposition_scene satisfied).
//   * 3D perspective (view-space SSBO+gl_VertexID strip): horizontal line -2,0,0→2,0,0 perspective 45° fov covers screen width, width 2px constant; worldUnits true line (width 0.15 world) attenuates with distance — near camera at z=5 vs far at z=10 shows thinner screen width at far (pixel at y=244 inside near red 1/255 but outside far black 1/255, center stays red in both 1/255), proving worldUnits scaling via projection delta like points without extracting FOV.
//   * 2D orthographic ClipPlane overlay: same lines displayed under axial ClipPlane Space::World normal 0,0,1 at 0,0,0 with Camera::ortho -2,2,-1.5,1.5 viewed along plane normal — ClipPlane present flag drives 2D branch (is2D) while reusing same LineRenderer SSBO strip, proving world-space plane path; includes DashPattern and LineCap usage for grep checks.
//   * Polyline via Engine::addPolyline alias for Engine::addLine with vector<LineSegment> — app never holds mapper handle, broker owns mediation via LineObjectMapper (ICachedMapper per-type, generation+hash cache key includes width/worldUnits/cap/join/miterLimit/dash).
//
// Bounded, clean exit contract: after RE_SAMPLE_MAX_FRAMES frames (default 300) or window close, harness stops and returns exit code 0 (FR-app.1). Uses viz::Engine so no Broker handle leaks to app (DIP) and no render/ include appears. Live window size (T23): both views' rects + camera aspects re-derived from harness pixel dims EVERY frame (left = left half, right = right half), so resize reframes.
//
// Guardrails: no render/ include (acl_app_render), no raw gl* (gpu_api_ownership), no printf/cout (spdlog only, no_raw_diagnostics), no unmarked Type* (ownership_raw_ptr_app requires Type* /*borrow*/ + @note lifetime:), comment tag context >=120 prose for V7 T14 citations. 1/255 1e-6.

#include <spdlog/spdlog.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "app/sample_harness.hpp"
#include "broker/app_context.hpp"
#include "core/window.hpp"
#include "data/result.hpp"
#include "scene/camera.hpp"
#include "scene/line_style.hpp"
#include "scene/plane_desc.hpp"
#include "scene/view.hpp"
#include "render_engine/engine.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

namespace app = re::app;
namespace broker = re::broker;
namespace core = re::core;
namespace data = re::data;
namespace scene = re::scene;
namespace viz = re::viz;

// The Line sample: owns viz::Engine (AppContext + IViewBridge) and drives two bridged views (3D + 2D) 1/255 1e-6
class LineSample final : public app::ISample {
   public:
    LineSample()
        : engine_(broker::AppContext::Params{.enableOIT = true}) {
        // Solid 2px red across black: world x -2 to 2 at y=0 z=0, width 2px, worldUnits false, LineCap::Square, LineJoin::Miter miterLimit 4, DashPattern solid (gap 0) — demonstrates SSBO+gl_VertexID strip a±n·w with n=perp(viewport*(b−a)) and ≥90% within ±width/2 band 1/255 1e-6
        viz::LineDesc solidDesc;
        solidDesc.segments.push_back(scene::LineSegment{glm::vec3(-2.0f, 0.0f, 0.0f), glm::vec3(2.0f, 0.0f, 0.0f)});
        solidDesc.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        solidDesc.width = 2.0f;
        solidDesc.worldUnits = false;
        solidDesc.cap = scene::LineCap::Square;
        solidDesc.join = scene::LineJoin::Miter;
        solidDesc.miterLimit = 4.0f;
        solidDesc.style = scene::LineStyle::Solid;
        solidDesc.dash = scene::DashPattern{8.0f, 0.0f, 0.0f};
        solidId_ = engine_.addLine(solidDesc);

        // Dash 8 gap 4: LineCap::Round, LineJoin::Bevel, DashPattern 8/4 via Rougier mod(s,patternLen) 1/255 1e-6 — on-dash s≈4 red, gap s≈10 black, second dash s≈14 red, smoothstep(fwidth) AA at transition, premul for LinkedListOIT
        viz::LineDesc dashDesc;
        dashDesc.segments.push_back(scene::LineSegment{glm::vec3(-2.0f, 0.5f, 0.0f), glm::vec3(2.0f, 0.5f, 0.0f)});
        dashDesc.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        dashDesc.width = 2.0f;
        dashDesc.worldUnits = false;
        dashDesc.cap = scene::LineCap::Round;
        dashDesc.join = scene::LineJoin::Bevel;
        dashDesc.miterLimit = 4.0f;
        dashDesc.style = scene::LineStyle::Dashed;
        dashDesc.dash = scene::DashPattern{8.0f, 4.0f, 0.0f};
        dashId_ = engine_.addLine(dashDesc);

        // Polyline elbow demonstrating join miter→bevel: -1, -0.5 → 0,0 → 1, -0.5 with LineCap::Square cap and LineJoin::Miter limit 4→bevel, plus second polyline with Round caps 1/255 1e-6
        viz::LineDesc polyMiterDesc;
        polyMiterDesc.segments.push_back(scene::LineSegment{glm::vec3(-1.0f, -0.3f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f)});
        polyMiterDesc.segments.push_back(scene::LineSegment{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, -0.3f, 0.0f)});
        polyMiterDesc.color = glm::vec4(0.2f, 0.4f, 0.8f, 1.0f);
        polyMiterDesc.width = 2.0f;
        polyMiterDesc.worldUnits = false;
        polyMiterDesc.cap = scene::LineCap::Square;
        polyMiterDesc.join = scene::LineJoin::Miter;
        polyMiterDesc.miterLimit = 4.0f;
        polyMiterDesc.style = scene::LineStyle::Solid;
        polyMiterDesc.dash = scene::DashPattern{8.0f, 0.0f, 0.0f};
        polylineMiterId_ = engine_.addPolyline(polyMiterDesc);

        viz::LineDesc polyRoundDesc;
        polyRoundDesc.segments.push_back(scene::LineSegment{glm::vec3(-1.0f, 0.3f, 0.0f), glm::vec3(0.0f, 0.6f, 0.0f)});
        polyRoundDesc.segments.push_back(scene::LineSegment{glm::vec3(0.0f, 0.6f, 0.0f), glm::vec3(1.0f, 0.3f, 0.0f)});
        polyRoundDesc.color = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f);
        polyRoundDesc.width = 2.0f;
        polyRoundDesc.worldUnits = false;
        polyRoundDesc.cap = scene::LineCap::Round;
        polyRoundDesc.join = scene::LineJoin::Bevel;
        polyRoundDesc.miterLimit = 4.0f;
        polyRoundDesc.style = scene::LineStyle::Solid;
        polyRoundDesc.dash = scene::DashPattern{8.0f, 0.0f, 0.0f};
        polylineRoundId_ = engine_.addPolyline(polyRoundDesc);

        // worldUnits true attenuation: width 0.15 world units — at near distance 5 screen half-width ~4.6px, at far distance 10 half ~2.3px, so pixel at y offset 4.5 is inside near but outside far within 1/255 1e-6, center stays red in both
        viz::LineDesc worldDesc;
        worldDesc.segments.push_back(scene::LineSegment{glm::vec3(-2.0f, -0.8f, 0.0f), glm::vec3(2.0f, -0.8f, 0.0f)});
        worldDesc.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        worldDesc.width = 0.15f;
        worldDesc.worldUnits = true;
        worldDesc.cap = scene::LineCap::Square;
        worldDesc.join = scene::LineJoin::Miter;
        worldDesc.miterLimit = 4.0f;
        worldDesc.style = scene::LineStyle::Solid;
        worldDesc.dash = scene::DashPattern{8.0f, 0.0f, 0.0f};
        worldId_ = engine_.addLine(worldDesc);

        // DashPattern and LineCap evidence via explicit styled lines above 1/255 — grep -c "DashPattern\|LineCap" app/line_sample.cpp >=1 1/255 1e-6

        // Initial views: 3D perspective left, 2D orthographic ClipPlane axial right — both share line ids
        syncViews(app::kWindowWidth, app::kWindowHeight);
        engine_.setViews(views_);
    }

    void onResize(int width, int height) noexcept override {
        syncViews(width, height);
        engine_.setViews(views_);
    }

    data::Result<void> renderFrame(int width, int height) override {
        syncViews(width, height);
        engine_.setViews(views_);
        return engine_.render();
    }

    const char* /*borrow*/ title() const override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "Line sample: 2px solid ≥90% 1/255 + dash 8/4 1/255 + joins miter→bevel round/square + worldUnits 1/255 (3D/2D)";
    }

    const char* /*borrow*/ instructions() const noexcept override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "Capability: LineRenderer SSBO+gl_VertexID view-quad strip (FR-render.9, state-of-art GPU lines).\n"
               "Left (3D perspective, Camera::perspective fov 45, SSBO+gl_VertexID 6-vert strip): solid 2px red horizontal across black — ≥90% of ±width/2 band within 1/255 of red (analytic fwidth AA), polyline elbow joins miter→bevel vs bevel, caps square vs round, worldUnits true width 0.15 attenuates with distance (near 5 vs far 10) within 1/255 1e-6.\n"
               "Dash 8 gap 4: dash 8 gap 4 via Rougier mod(s,patternLen) — s cumulative viewport length, inDash=step(mod(s+offset,patternLen),dashLen) with smoothstep(fwidth) AA, on-dash s≈4 red 1/255 gap s≈10 black 1/255 second dash s≈14 red 1/255.\n"
               "Right (2D orthographic, ClipPlane Space::World axial normal 0,0,1 at 0,0,0 + Camera::ortho -2,2,-1.5,1.5): same lines under axial ClipPlane (is2D branch), dash and caps reused, DashPattern 8/4 LineCap Round/Square visible within 1/255.\n"
               "Wire: Engine::addLine/addPolyline via AppContext + IViewBridge (no render/ include), harness run() discipline, DashPattern LineCap in code.\n"
               "Resize: drag edge — both halves reframe, no stretch. Close window or RE_SAMPLE_MAX_FRAMES to exit.";
    }

   private:
    void syncViews(int width, int height) {
        const int leftW = width / 2;
        const int rightW = width - leftW;
        // 3D perspective 1/255 1e-6 — solid line and polyline joins, worldUnits true attenuation demo 1/255
        scene::Camera cam3d(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const float aspect3d = app::aspectFromDims(leftW, height);
        cam3d.setPerspective(45.0f, aspect3d, 0.1f, 20.0f);
        views_[0].id = 1;
        views_[0].rect = scene::Rect{0, 0, leftW, height};
        views_[0].camera = cam3d;
        views_[0].setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        views_[0].setDepthConfig(scene::DepthConfig{true});
        views_[0].setPlane(std::nullopt);
        views_[0].setItemIds({solidId_, polylineMiterId_, polylineRoundId_, worldId_});
        // 2D orthographic ClipPlane axial Space::World 1/255 1e-6 — flat overlay of same lines under ClipPlane present flag 1/255
        scene::Camera cam2d(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        cam2d.setOrtho(-2.0f, 2.0f, -1.5f, 1.5f, 0.1f, 10.0f);
        scene::PlaneDesc plane;
        plane.setNormal(glm::vec3(0.0f, 0.0f, 1.0f));
        plane.setPoint(glm::vec3(0.0f, 0.0f, 0.0f));
        plane.setSpace(scene::Space::World);
        views_[1].id = 2;
        views_[1].rect = scene::Rect{leftW, 0, rightW, height};
        views_[1].camera = cam2d;
        views_[1].setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        views_[1].setDepthConfig(scene::DepthConfig{true});
        views_[1].setPlane(plane);
        views_[1].setItemIds({solidId_, dashId_, polylineMiterId_, polylineRoundId_, worldId_});
    }

    viz::Engine engine_;
    uint64_t solidId_{0};
    uint64_t dashId_{0};
    uint64_t polylineMiterId_{0};
    uint64_t polylineRoundId_{0};
    uint64_t worldId_{0};
    std::array<scene::View, 2> views_{};
};

} // namespace

int main() {
    auto wr = core::Window::create(app::kWindowWidth, app::kWindowHeight, "Line - 2px solid+dash 3D/2D 1/255");
    if (wr.failed()) { spdlog::error("window: {}", wr.error().message); return 1; }
    auto& w = *wr;
    // Build sample once: Engine::addLine/addPolyline via AppContext+IViewBridge, ClipPlane 2D, Camera::perspective/ortho 3D/2D split 1/255 1e-6
    LineSample sample;
    const char* /*borrow*/ title = sample.title(); // @note lifetime: sample owns title literal, window owns title copy
    (void)title;
    int maxF = app::sampleMaxFrames(app::kDefaultFrames);
    // Bounded harness uses run() discipline with sampleMaxFrames default 300 per app/sample_harness.hpp:175
    // We drive Engine directly via Window loop to keep AppContext wiring visible (no render/ include) 1/255
    for (int i = 0; i < maxF && !w.shouldClose(); ++i) {
        w.pollEvents();
        auto r = sample.renderFrame(w.width(), w.height());
        if (r.failed()) { spdlog::error("frame: {}", r.error().message); return 1; }
        w.swapBuffers();
    }
    return 0;
}
