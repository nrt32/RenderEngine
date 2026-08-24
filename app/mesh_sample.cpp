// app/mesh_sample.cpp — mesh rendering sample (T12, FR-app.1; T20 routed
// through the broker façade).
//
// Demonstrates the mesh capability: loads the Stanford bunny (data/meshes,
// SPEC §7), shades it with an opaque Phong material from its scene
// presentation value, and drives it through the shared app::SampleHarness
// (visible window + ImGui overlay + run loop). The scene is expressed ENTIRELY
// as scene/ values (MeshObject + View) in a broker::AppContext composition
// root and rendered through IViewBridge (sync → renderAll → presentAll) —
// per the SPEC §11 ACL the sample never includes render/ and never holds a
// mapper or renderer handle.
//
// The sample exits cleanly (code 0) after RE_SAMPLE_MAX_FRAMES frames (default
// 300), so the automated gate can run it headlessly under Xvfb within a timeout
// (FR-app.1: exit code 0, no sanitizer reports).
//
// Live window size (T23): the view's rect and the camera's projection aspect
// are re-derived from the harness pixel dims EVERY frame via
// app::fitPerspectiveViewToPixels — the compile-time kWindowWidth/kWindowHeight
// constants only pick the OPENING window size, never feed projections — so
// resizing the window reframes the bunny instead of stretching it.

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
// Perspective vertical field of view in degrees (~60 deg — glm::radians(60)
// reproduces the previous 1.0471975511965976 rad constant).
constexpr float kFovYDeg = 60.0f;

/// The mesh sample: owns the loaded bunny + the AppContext and renders one
/// bridged frame per renderFrame call.
class MeshSample final : public app::ISample {
   public:
    explicit MeshSample(data::Mesh mesh)
        : mesh_(std::make_shared<const data::Mesh>(std::move(mesh))),
          ctx_(broker::AppContext::Params{}) {
        // Scene values only: one MeshObject carrying the shared asset ref +
        // the Phong presentation (base color identical to the previous direct
        // render path), added to the store for a stable id.
        scene::MeshObject mo;
        mo.mesh = mesh_;
        mo.transform = glm::mat4(1.0f);
        mo.presentation.phong.baseColor = glm::vec4(0.85f, 0.45f, 0.15f, 1.0f);
        const uint64_t meshId = ctx_.store().addMeshObject(std::move(mo));

        // One full-window view: perspective camera framing the mesh (eye
        // pulled back along +Z from the AABB center by radius / tan(fov/2)).
        // The framing (eye distance, near/far) is derived ONCE from the mesh
        // bounds; the rect + projection aspect below are re-derived from live
        // pixel dims every frame, so only this initial state uses the opening
        // window constants.
        const data::Aabb& b = mesh_->bounds();
        const glm::vec3 center = 0.5f * (b.min + b.max);
        const float radius =
            0.5f * glm::length(b.max - b.min);
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
        view_.setItemIds({meshId});
    }

    /// The resize hook: apply the new pixel dims immediately so the very next
    /// frame (and its sync) already carries the corrected rect + aspect.
    void onResize(int width, int height) noexcept override {
        applyLiveDims(width, height);
    }

    data::Result<void> renderFrame(int width, int height) override {
        // Live dims first: rect + camera aspect always derive from THIS
        // frame's framebuffer size (change-guarded setters make a no-resize
        // frame free), then the bridge path: sync translates dirty fields into
        // cached Re state, renderAll draws every ReView into its own target,
        // presentAll blits each target 1:1 into its window rect (null
        // destination = the window's default framebuffer).
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
        return "Mesh sample: Stanford bunny (Phong opaque)";
    }

    const char* instructions() const noexcept override {
        return "Capability: shaded triangle mesh (SPEC FR-render.1).\n"
               "A Phong (opaque) mesh is translated by the broker mapper "
               "inventory (MeshObjectMapper + MaterialMapper) and drawn "
               "through the IViewBridge façade.\n"
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
        std::string(RE_SOURCE_DIR) + "/data/meshes/bunny.obj";
    auto meshResult = re::io::loadObjMesh(meshPath);
    if (meshResult.failed()) {
        spdlog::error("mesh sample: failed to load '{}': {}", meshPath,
                      meshResult.error().message);
        return 1;
    }

    auto windowResult = core::Window::create(kWindowWidth, kWindowHeight,
                                             "RenderEngine - Mesh Sample");
    if (windowResult.failed()) {
        spdlog::error("mesh sample: {}", windowResult.error().message);
        return 1;
    }

    auto sample = std::make_unique<MeshSample>(std::move(*meshResult));
    app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.run(app::sampleMaxFrames(kDefaultFrames));
}
