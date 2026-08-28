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
// are re-derived from the harness pixel dims EVERY frame via the builder helper — the compile-time kWindowWidth/kWindowHeight
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
#include "scene/builders.hpp"
#include "scene/camera.hpp"

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
// (AS2 constants dedup — the OPENING size only; live dims drive every frame).

/// The mesh sample: owns the loaded bunny + the AppContext and renders one
/// bridged frame per renderFrame call.
class MeshSample final : public app::ISample {
   public:
    explicit MeshSample(data::Mesh mesh)
        : mesh_(std::make_shared<const data::Mesh>(std::move(mesh))),
          ctx_(broker::AppContext::Params{}) {
        // Scene values via Objects helper (T7): one MeshObject carrying the shared asset ref +
        // the Phong presentation (base color identical to the previous direct
        // render path), added to the store for a stable id. The helper Objects::mesh
        // targets the T6 single-map store path (addObject<MeshObject>) rather than a
        // deleted 17-partition API — one helper, not hand-written boilerplate.
        scene::MeshMaterialDesc mat;
        mat.phong.baseColor = glm::vec4(0.85f, 0.45f, 0.15f, 1.0f);
        auto mo = scene::Objects::mesh(mesh_, glm::mat4(1.0f), mat);
        const uint64_t meshId = ctx_.store().addMeshObject(std::move(mo));

        // One full-window view: perspective camera framing the mesh (eye
        // pulled back along +Z from the AABB center by radius / tan(fov/2)).
        // The framing (eye distance, near/far) is derived ONCE from the mesh
        // bounds; the rect + projection aspect below are re-derived from live
        // pixel dims every frame via the builder helper one call (T7),
        // so only this initial state uses the opening window constants.
        const data::Aabb& b = mesh_->bounds();
        const glm::vec3 center = 0.5f * (b.min + b.max);
        const float radius =
            0.5f * glm::length(b.max - b.min);
        const float dist = radius / std::tan(0.5f * glm::radians(app::kDefaultFovYDeg));
        // T7 V5: framing now owned by the builder (no app framing type) — the builder stores the framing type value (fov/near/far) and its syncLive(w,h) one call does rect := {0,0,w,h} plus camera.setPerspectiveFromFraming at aspect w/h, which is the single helper that replaces the six hand-copied private builder helper methods that previously duplicated the live-aspect rule in each sample (T7).
        scene::SceneViewBuilder bld(view_.id, scene::Rect{0, 0, app::kWindowWidth, app::kWindowHeight}, {app::kDefaultFovYDeg, 0.1f, 2.0f * (dist + radius)});
        bld.withCamera(scene::Camera(center + glm::vec3(0.0f, 0.0f, dist), center, glm::vec3(0.0f, 1.0f, 0.0f)));
        view_ = bld.build();
        // Seed live dims via builder one call.
        bld.syncLive(app::kWindowWidth, app::kWindowHeight);
        view_ = bld.view();
        view_.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        view_.setItemIds({meshId});
        // Keep builder for live updates
        builder_ = std::move(bld);
    }

    /// The resize hook: one builder call.
    void onResize(int width, int height) noexcept override {
        syncLive(width, height);
    }

    data::Result<void> renderFrame(int width, int height) override {
        syncLive(width, height);
        views_ = {view_};
        return app::syncRenderPresent(ctx_, views_);
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
    void syncLive(int width, int height) noexcept {
        builder_.syncLive(width, height);
        view_ = builder_.view();
    }

    std::shared_ptr<const data::Mesh> mesh_;
    broker::AppContext ctx_;
    scene::View view_{};
    scene::SceneViewBuilder builder_{1, scene::Rect{0, 0, 800, 600}};
    std::vector<scene::View> views_{};
};

} // namespace

int main() {
    // Single-site entry via app::runSample (AS2: load→window→run dedup).
    return app::runSample(
        "RenderEngine - Mesh Sample", app::kWindowWidth, app::kWindowHeight,
        app::kDefaultFrames, []() -> std::unique_ptr<app::ISample> {
            const std::string meshPath =
                std::string(RE_SOURCE_DIR) + "/data/meshes/bunny.obj";
            auto meshResult = re::io::loadObjMesh(meshPath);
            if (meshResult.failed()) {
                spdlog::error("mesh sample: failed to load '{}': {}", meshPath,
                              meshResult.error().message);
                return nullptr;
            }
            return std::make_unique<MeshSample>(std::move(*meshResult));
        });
}
