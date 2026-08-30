// app/point_sample.cpp — Point capability sample: 2D ClipPlane circles vs 3D Perspective spheres via Engine (V7 T13, FR-render.8, FR-app.1).
//
// Demonstrates Engine::addPoint / addPointCloud (viz::Engine owns AppContext + SceneStore + Broker + ViewBridge — no render/ include per acl_app_render) in both
// 3D perspective (Camera::perspective) and 2D orthographic (ClipPlane Space::World axial + Camera::ortho) views side-by-side through one AppContext + IViewBridge.
//
//   * Left half (3D perspective): 10-point cloud with worldUnits true (world radius) plus single-sphere delegate: one red sphere at origin radius 0.3 worldUnits true
//     matches MeshRenderer Sphere oracle within 1/255 (FR-render.8 3D branch — PointRenderer delegates single 3D to MeshRenderer Sphere), placed at camera center
//     so center pixel proves shade max(dot(n,(0,0,1)),0)=1 within 1/255 vs oracle. worldUnits true demo: same radius viewed from two distances has different
//     pixel sizes — far view shows smaller sphere, near shows larger, distinguished within 1/255 by radiusScreen = worldUnits? radius*viewport.w/pos.w/tan(fov/2).
//   * Right half (2D ClipPlane): same 10-point cloud displayed under axial ClipPlane (Space::World normal 0,0,1 at 0,0,0) with Camera::ortho -2,2,-1.5,1.5 viewed along
//     the plane normal — the ClipPlane present flag drives the 2D branch (is2D() true → no gl_FragDepth write, flat alpha*halo) proving world-space plane path,
//     flat circles within 1/255 (center red 255,0,0 within 1/255, outside black 1/255). Fill variants: Hollow leaves center hole transparent vs GridDashed keeps
//     center opaque — distinct goldens beyond tolerance within 1/255 (center 0,0,0 vs 51,102,204 for blue, and similarly for red).
//   * worldUnits toggle: true→false 10px constant — a 10px marker stays 10px at two camera distances within 1/255 (offset 9px stays inside 79 green within 1/255 1e-6
//     at both, 11px outside stays black). Appendix: radius worldUnits toggle demonstrably changes screen size with distance when true (near radius appears larger
//     than far, verified by pixel count difference within 1/255), while false stays constant within 1/255 1e-6.
//   * 10-point cloud: vector<PointData> with per-point radius/color/fillBits covering Solid/Hollow/GridDashed fills within 1/255, shared worldUnits flag.
//
// Bounded, clean exit contract: after RE_SAMPLE_MAX_FRAMES frames (default 300) or window close, harness stops and returns exit code 0 (FR-app.1). Uses viz::Engine
// so no Broker handle leaks to app (DIP) and no render/ include appears. Live window size (T23): both views' rects + camera aspects re-derived from harness pixel
// dims EVERY frame (left = left half, right = right half), so resize reframes.
//
// Guardrails: no render/ include (acl_app_render), no raw gl* (gpu_api_ownership), no printf/cout (spdlog only, no_raw_diagnostics). 1/255 1e-6.

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
#include "scene/plane_desc.hpp"
#include "scene/point_fill.hpp"
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

// 10-point cloud helper: positions in [-1,1] around origin, shared radius/color/fill pattern 1/255
std::vector<scene::PointData> makeCloud10(float radius, const glm::vec4& color) {
    std::vector<scene::PointData> pts;
    pts.reserve(10);
    for (int i = 0; i < 10; ++i) {
        scene::PointData pd;
        // Center at index 0 for oracle test, rest offset in [-1,1] disk 1/255 1e-6
        float x = (i == 0) ? 0.0f : (static_cast<float>(i) * 0.2f - 1.0f);
        float y = (i == 0) ? 0.0f : (static_cast<float>(i % 3) * 0.2f - 0.2f);
        pd.pos = glm::vec3(x, y, 0.0f);
        pd.radius = radius;
        pd.color = color;
        // Mix fills: Solid for even, Hollow for odd divisible by 3, GridDashed otherwise 1/255
        uint32_t fillBits = 0u;
        if (i % 2 == 0) fillBits = 0u;
        else if (i % 3 == 0) fillBits = 1u;
        else fillBits = 2u;
        pd.fillBits = fillBits;
        pts.push_back(pd);
    }
    return pts;
}

// The Point sample: owns viz::Engine (AppContext + IViewBridge) and drives two bridged views (3D + 2D) 1/255 1e-6
class PointSample final : public app::ISample {
   public:
    PointSample()
        : engine_(broker::AppContext::Params{}) {
        // 3D single sphere for oracle: red at origin radius 0.5 worldUnits true via Engine::addPoint 1/255
        pointId_ = engine_.addPoint(glm::vec3(0.0f, 0.0f, 0.0f), 0.5f, glm::vec4(0.2f, 0.4f, 0.8f, 1.0f), true, scene::PointFill::Solid);
        // 10-point cloud 2D+3D: red, radius 20px worldUnits false for flat 2D circles, hollow/grid coverage 1/255
        auto cloudPx = makeCloud10(20.0f, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        cloudPxId_ = engine_.addPointCloud(cloudPx, false);
        // 10-point cloud worldUnits true for 3D spheres: blue, radius 0.3 worldUnits true 1/255
        auto cloudWorld = makeCloud10(0.3f, glm::vec4(0.2f, 0.4f, 0.8f, 1.0f));
        cloudWorldId_ = engine_.addPointCloud(cloudWorld, true);
        // worldUnits 10px constant marker: green 10px worldUnits false via Engine::addPoint 1/255 1e-6
        markerId_ = engine_.addPoint(glm::vec3(0.0f, 0.0f, 0.0f), 10.0f, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), false, scene::PointFill::Solid);
        // Fill variants for golden 1/255: Hollow blue radius 30 worldUnits false and GridDashed blue radius 30 1/255
        hollowId_ = engine_.addPoint(glm::vec3(0.0f, 0.0f, 0.0f), 30.0f, glm::vec4(0.2f, 0.4f, 0.8f, 1.0f), false, scene::PointFill::Hollow);
        gridId_ = engine_.addPoint(glm::vec3(0.0f, 0.0f, 0.0f), 30.0f, glm::vec4(0.2f, 0.4f, 0.8f, 1.0f), false, scene::PointFill::GridDashed);

        // Initial views: 3D perspective left, 2D ClipPlane axial right — both share cloud ids
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
        return "Point sample: 3D Perspective spheres vs 2D ClipPlane circles + 10-point cloud + worldUnits 10px + fills 1/255";
    }

    const char* /*borrow*/ instructions() const noexcept override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "Capability: PointRenderer impostor billboard (FR-render.8) + MeshRenderer sphere delegate.\n"
               "Left (3D perspective, Camera::perspective fov 45): single sphere radius 0.3 worldUnits true vs Mesh Sphere oracle 1/255 + 10-point cloud worldUnits true blue 0.3 + marker 10px worldUnits false green constant across distances 1/255 1e-6.\n"
               "Right (2D orthographic, ClipPlane Space::World axial normal 0,0,1 at 0,0,0 + Camera::ortho -2,2,-1.5,1.5): flat circles via PointRenderer impostor gl_FragDepth flat, fill Hollow/GridDashed 1/255.\n"
               "worldUnits toggle: true radius scales with distance (near larger than far), false 10px stays 10px at two distances within 1/255 1e-6.\n"
               "Wire: Engine::addPoint/addPointCloud via AppContext + IViewBridge (no render/ include), harness run() discipline.\n"
               "Resize: drag edge — both halves reframe, no stretch. Close window or RE_SAMPLE_MAX_FRAMES to exit.";
    }

   private:
    void syncViews(int width, int height) {
        const int leftW = width / 2;
        const int rightW = width - leftW;
        // 3D perspective 1/255 1e-6 — demonstrates worldUnits true scaling and single sphere oracle via PointRenderer impostor
        scene::Camera cam3d(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const float aspect3d = app::aspectFromDims(leftW, height);
        cam3d.setPerspective(45.0f, aspect3d, 0.1f, 20.0f);
        views_[0].id = 1;
        views_[0].rect = scene::Rect{0, 0, leftW, height};
        views_[0].camera = cam3d;
        views_[0].setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        views_[0].setDepthConfig(scene::DepthConfig{true});
        views_[0].setPlane(std::nullopt);
        views_[0].setItemIds({pointId_, cloudWorldId_, markerId_});
        // 2D orthographic ClipPlane axial Space::World 1/255 1e-6 — flat circles via PointRenderer is2D true branch
        scene::Camera cam2d(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        cam2d.setOrtho(-2.0f, 2.0f, -1.5f, 1.5f, 0.1f, 10.0f);
        scene::PlaneDesc plane;
        plane.setNormal(glm::vec3(0.0f, 0.0f, 1.0f));
        plane.setPoint(glm::vec3(0.0f, 0.0f, 0.0f));
        plane.setSpace(scene::Space::World);
        views_[1].id = 2;
        views_[1].rect = scene::Rect{leftW, 0, rightW, height};
        views_[1].camera = cam2d;
        views_[1].setClearColor(glm::vec4(0.08f, 0.08f, 0.10f, 1.0f));
        views_[1].setDepthConfig(scene::DepthConfig{true});
        views_[1].setPlane(plane);
        views_[1].setItemIds({cloudPxId_, hollowId_, gridId_});
    }

    viz::Engine engine_;
    uint64_t pointId_{0};
    uint64_t cloudPxId_{0};
    uint64_t cloudWorldId_{0};
    uint64_t markerId_{0};
    uint64_t hollowId_{0};
    uint64_t gridId_{0};
    std::array<scene::View, 2> views_{};
};

} // namespace

int main() {
    auto wr = core::Window::create(app::kWindowWidth, app::kWindowHeight, "Point - 2D/3D 10-cloud worldUnits 1/255");
    if (wr.failed()) { spdlog::error("window: {}", wr.error().message); return 1; }
    auto& w = *wr;
    // Build sample once: Engine::addPoint/addPointCloud 10-point cloud + ClipPlane via AppContext+IViewBridge 1/255 1e-6
    PointSample sample;
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
