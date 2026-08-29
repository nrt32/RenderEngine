// app/slice_sample.cpp — mesh slice rendering sample (T13, FR-app.1; T20
// routed through the broker façade).
//
// Demonstrates the mesh-slice capability (SPEC §1 capability 4, FR-render.4):
// loads the Utah teapot (data/meshes, SPEC §7), defines a horizontal clip
// plane through its vertical midpoint, and renders the clipped mesh (the kept
// half-space `dot(normal, p - point) >= 0`) through the IViewBridge façade.
// The scene is a MeshSliceObject scene value whose clip plane lives on the
// VIEW (the broker contextual rule, SPEC §11.4); the synchronizer maps it
// through broker::MeshSliceObjectMapper into a render::SliceScene drawn by
// render::SliceRenderer's geometry-shader clip — slicing is geometry, not
// compositing (SPEC §3), and per the SPEC §11 ACL this sample never includes
// render/ and never holds a mapper or renderer handle.
//
// The sample is driven through the shared app::SampleHarness exactly like the
// other capability samples, and exits cleanly (code 0) after
// RE_SAMPLE_MAX_FRAMES frames (default 300), so the gate can run it headlessly
// under Xvfb within a timeout (FR-app.1: exit code 0, no sanitizer reports).
//
// Live window size (T23, T7): the view's rect and the camera's projection aspect
// are re-derived from the harness pixel dims EVERY frame via
// the SceneViewBuilder live-dims helper (scene/builders.hpp) — the compile-time
// kWindowWidth/kWindowHeight constants only pick the OPENING window size, never
// feed projections — so resizing the window reframes the clipped teapot instead
// of stretching it (T7 single helper, not six private duplicates).

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <utility>

#include "app/glfw_camera_interactor.hpp"
#include "app/sample_harness.hpp"
#include "broker/app_context.hpp"
#include "core/window.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "io/mesh/obj_mesh_loader.hpp"
#include "scene/builders.hpp"
#include "scene/camera_controller.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

namespace app = re::app;
namespace broker = re::broker;
namespace core = re::core;
namespace data = re::data;
namespace scene = re::scene;

// Window size, frame count and FOV are shared via app::kWindowWidth etc.
// (AS2 constants dedup).

/// The slice sample: owns the loaded teapot + AppContext and renders one
/// bridged clipped frame per renderFrame call.
class SliceSample final : public app::ISample {
   public:
    explicit SliceSample(data::Mesh mesh)
        : mesh_(std::make_shared<const data::Mesh>(std::move(mesh))),
          // registerCameraMapper is OFF for this composition: the view is a
          // PERSPECTIVE 3D view that merely CARRIES a clip plane for its
          // mesh-slice item (the broker contextual rule), while CameraMapper's
          // T4 validation pins "plane present -> orthographic" for true 2D
          // slice displays. The analytic framing here is perspective, so the
          // context skips that mapper registration and the synchronizer
          // translates the camera directly (identical matrices, no cache).
          ctx_(broker::AppContext::Params{.enableOIT = false,
                                          .registerCameraMapper = false}) {
        // Scene values only: one MeshSliceObject carrying the shared asset ref
        // + presentation; the clip plane belongs to the VIEW (broker
        // contextual rule), so it never rides on the object.
        scene::MeshSliceObject ms;
        ms.mesh = mesh_;
        ms.transform = glm::mat4(1.0f);
        ms.presentation.phong.baseColor = glm::vec4(0.25f, 0.55f, 0.85f, 1.0f);
        const uint64_t sliceId = ctx_.store().addMeshSliceObject(std::move(ms));

        // Horizontal clip plane at the mesh's vertical midpoint: normal +Y
        // through `y = 0.5*(min.y + max.y)`. The kept side is `y >= midpoint`,
        // so the teapot is cut open around its equator (FR-render.4: every
        // emitted cross-section vertex lies on the plane). World space.
        const data::Aabb& b = mesh_->bounds();
        scene::PlaneDesc plane;
        plane.setNormal(glm::vec3(0.0f, 1.0f, 0.0f));
        plane.setPoint(glm::vec3(0.0f, 0.5f * (b.min.y + b.max.y), 0.0f));
        plane.setSpace(scene::Space::World);

        // Perspective camera framing the mesh (eye pulled back along +Z from
        // the AABB center by radius / tan(fov/2)). The framing (eye distance,
        // near/far) is derived ONCE from the mesh bounds; rect + projection
        // aspect are re-derived from live pixel dims via builder one call (T7).
        const glm::vec3 center = 0.5f * (b.min + b.max);
        const float radius = 0.5f * glm::length(b.max - b.min);
        const float dist = radius / std::tan(0.5f * glm::radians(app::kDefaultFovYDeg));
        scene::SceneViewBuilder bld(1, scene::Rect{0, 0, app::kWindowWidth, app::kWindowHeight}, {app::kDefaultFovYDeg, 0.1f, 2.0f * (dist + radius)});
        bld.withCamera(scene::Camera(center + glm::vec3(0.0f, 0.0f, dist), center, glm::vec3(0.0f, 1.0f, 0.0f)));
        bld.syncLive(app::kWindowWidth, app::kWindowHeight);
        view_ = bld.view();
        view_.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        view_.setItemIds({sliceId});
        view_.setPlane(plane);
        builder_ = std::move(bld);
    }

    /// The resize hook: one builder call (T7 V5) — the builder stores the framing type (fov/near/far) and its syncLive(w,h) does rect := {0,0,w,h} plus camera.setPerspectiveFromFraming at aspect w/h, which is the single helper that replaces the six private duplicates; this hook therefore forwards the live harness pixel size through the builder so the projection stays derived from the current size without re-deriving framing distance (T7).
    void onResize(int width, int height) noexcept override {
        builder_.syncLive(width, height);
        view_ = builder_.view();
    }

    data::Result<void> renderFrame(int width, int height) override {
        interactor_.update(builder_.view());
        builder_.syncLive(width, height);
        view_ = builder_.view();
        views_ = {view_};
        return app::syncRenderPresent(ctx_, views_);
    }

    const char* title() const override {
        return "Slice sample: teapot clipped by a horizontal plane";
    }

    const char* instructions() const noexcept override {
        return "Capability: mesh slice rendering / plane clip (SPEC "
               "FR-render.4).\n"
               "The teapot is clipped by a horizontal plane at its vertical "
               "midpoint; the geometry shader keeps the upper half and emits "
               "the on-plane cross-section (slicing is geometry, not "
               "compositing). The MeshSliceObject translates through "
               "broker::MeshSliceObjectMapper with the plane supplied by its "
               "View.\n"
               "Controls: left-drag orbits, right-drag pans, middle/wheel "
               "zooms via scene::CameraController with WantCaptureMouse guard; "
               "View::setCamera bumps viewGen so broker re-translates only "
               "dirty camera fields.\n"
               "Resize check: drag a window edge — the view reframes to the "
               "live pixel size (camera aspect follows width/height), no "
               "stretching.\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    std::shared_ptr<const data::Mesh> mesh_;
    broker::AppContext ctx_;
    scene::View view_{};
    scene::SceneViewBuilder builder_{1, scene::Rect{0, 0, 800, 600}};
    std::vector<scene::View> views_{};
    scene::CameraController controller_{};
    app::GlfwCameraInteractor interactor_{controller_};
};

} // namespace

int main() {
    // Single-site entry via app::runSample (AS2).
    return app::runSample(
        "RenderEngine - Slice Sample", app::kWindowWidth, app::kWindowHeight,
        app::kDefaultFrames, []() -> std::unique_ptr<app::ISample> {
            const std::string meshPath =
                std::string(RE_SOURCE_DIR) + "/data/meshes/teapot.obj";
            auto meshResult = re::io::loadObjMesh(meshPath);
            if (meshResult.failed()) {
                spdlog::error("slice sample: failed to load '{}': {}", meshPath,
                              meshResult.error().message);
                return nullptr;
            }
            return std::make_unique<SliceSample>(std::move(*meshResult));
        });
}
