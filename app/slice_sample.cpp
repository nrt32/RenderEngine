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
// Live window size (T23): the view's rect and the camera's projection aspect
// are re-derived from the harness pixel dims EVERY frame via
// app::fitPerspectiveViewToPixels — the compile-time kWindowWidth/kWindowHeight
// constants only pick the OPENING window size, never feed projections — so
// resizing the window reframes the clipped teapot instead of stretching it.

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

#include "app/sample_harness.hpp"
#include "broker/app_context.hpp"
#include "core/window.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "io/mesh/obj_mesh_loader.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

namespace app = re::app;
namespace broker = re::broker;
namespace core = re::core;
namespace data = re::data;
namespace scene = re::scene;

// The harness window size: the OPENING size only — every per-frame value
// (view rect, camera aspect) derives from the live framebuffer dims instead.
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
// Default number of frames before the sample exits cleanly (gate overrides via
// RE_SAMPLE_MAX_FRAMES).
constexpr int kDefaultFrames = 300;
// Perspective vertical field of view in degrees (~60 deg).
constexpr float kFovYDeg = 60.0f;

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
        // aspect are re-derived from live pixel dims every frame.
        const glm::vec3 center = 0.5f * (b.min + b.max);
        const float radius = 0.5f * glm::length(b.max - b.min);
        const float dist = radius / std::tan(0.5f * glm::radians(kFovYDeg));
        framing_.fovDeg = kFovYDeg;
        framing_.nearPlane = 0.1f;
        framing_.farPlane = 2.0f * (dist + radius);

        view_.id = 1;
        view_.camera = scene::Camera(center + glm::vec3(0.0f, 0.0f, dist),
                                     center, glm::vec3(0.0f, 1.0f, 0.0f));
        app::fitPerspectiveViewToPixels(view_, framing_, kWindowWidth,
                                        kWindowHeight);
        view_.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        view_.setItemIds({sliceId});
        view_.setPlane(plane);
    }

    /// The resize hook: apply the new pixel dims immediately so the very next
    /// frame (and its sync) already carries the corrected rect + aspect.
    void onResize(int width, int height) noexcept override {
        applyLiveDims(width, height);
    }

    data::Result<void> renderFrame(int width, int height) override {
        // Live dims first: rect + camera aspect always derive from THIS
        // frame's framebuffer size (change-guarded setters make a no-resize
        // frame free), then the bridge path: sync → renderAll → presentAll
        // (null destination = window default framebuffer). The ReView target
        // rect equals the live pixel size, so the blit is 1:1.
        applyLiveDims(width, height);
        views_ = {view_};
        auto s = ctx_.bridge().sync(views_, ctx_.store());
        if (s.failed()) {
            return s;
        }
        auto r = ctx_.bridge().renderAll();
        if (r.failed()) {
            return r;
        }
        return ctx_.bridge().presentAll(nullptr);
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
               "Resize check: drag a window edge — the view reframes to the "
               "live pixel size (camera aspect follows width/height), no "
               "stretching.\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    /// Re-derive the view rect + camera aspect from live pixel dims — the one
    /// body shared by the resize hook and every rendered frame.
    void applyLiveDims(int width, int height) noexcept {
        app::fitPerspectiveViewToPixels(view_, framing_, width, height);
    }

    std::shared_ptr<const data::Mesh> mesh_;
    broker::AppContext ctx_;
    scene::View view_{};
    app::PerspectiveFraming framing_{}; // fov/near/far fixed; aspect is live
    std::vector<scene::View> views_{};
};

} // namespace

int main() {
    const std::string meshPath =
        std::string(RE_SOURCE_DIR) + "/data/meshes/teapot.obj";
    auto meshResult = re::io::loadObjMesh(meshPath);
    if (meshResult.failed()) {
        spdlog::error("slice sample: failed to load '{}': {}", meshPath,
                      meshResult.error().message);
        return 1;
    }

    auto windowResult = core::Window::create(
        kWindowWidth, kWindowHeight, "RenderEngine - Slice Sample");
    if (windowResult.failed()) {
        spdlog::error("slice sample: {}", windowResult.error().message);
        return 1;
    }

    auto sample = std::make_unique<SliceSample>(std::move(*meshResult));
    app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.run(app::sampleMaxFrames(kDefaultFrames));
}
