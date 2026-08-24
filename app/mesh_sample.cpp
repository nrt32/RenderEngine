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

// The harness window size.
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
        // pulled back along +Z from the AABB center by radius / tan(fov/2)),
        // clear color matching the previous direct-render target.
        const data::Aabb& b = mesh_->bounds();
        const glm::vec3 center = 0.5f * (b.min + b.max);
        const float radius =
            0.5f * glm::length(b.max - b.min);
        const float dist = radius / std::tan(0.5f * glm::radians(kFovYDeg));

        view_.id = 1;
        view_.rect = scene::Rect{0, 0, kWindowWidth, kWindowHeight};
        view_.camera = scene::Camera(center + glm::vec3(0.0f, 0.0f, dist),
                                     center, glm::vec3(0.0f, 1.0f, 0.0f));
        view_.camera.setPerspective(
            kFovYDeg,
            static_cast<float>(kWindowWidth) /
                static_cast<float>(kWindowHeight),
            0.1f, 2.0f * (dist + radius));
        view_.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        view_.setItemIds({meshId});
    }

    data::Result<void> renderFrame(int /*width*/, int /*height*/) override {
        // The bridge path: sync translates dirty fields into cached Re state,
        // renderAll draws every ReView into its own target, presentAll blits
        // each target 1:1 into its window rect (null destination = the
        // window's default framebuffer).
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
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    std::shared_ptr<const data::Mesh> mesh_;
    broker::AppContext ctx_;
    scene::View view_{};
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
