// app/mesh_long.cpp — long-lived mesh sample (T10, interactive, not for testing).
//
// Mirrors app/mesh_sample.cpp (bounded 300-frame Engine sample) but bypasses sampleMaxFrames and
// uses SampleHarness::runInteractive() until shouldClose() with pan+rotate+zoom via
// scene::CameraController + app::GlfwCameraInteractor (left drag rotate dx*0.5deg, right drag
// pan dx*0.01, scroll/middle drag zoom exp(-dy*0.02)). Each frame renderFrame calls
// interactor.update(view) before syncRenderPresent, respects WantCaptureMouse, skips
// orthographic slice views per V5 T9 guard, and mutates via view.setCamera(newCam) (not
// mutateCamera lambda — deleted in T8) so viewGen/cameraGen + generation bump lets broker
// re-translate only dirty camera fields 1e-6 analytic. Guard: EXCLUDE_FROM_ALL so ctest never
// runs this target. Docs note long-lived not for testing.

// Long-lived interactive sample: mesh bunny with broker path + camera interaction. The bounded peer
// uses viz::Engine (42 lines) for the 80% case; the interactive peer drops to broker/AppContext so
// the camera can be mutated via View::setCamera and the per-field viewGen split is observable. The
// visual result is byte-identical within 1/255 to the bounded peer when no input is applied.

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

class MeshLongSample final : public app::ISample {
   public:
    explicit MeshLongSample(data::Mesh mesh)
        : mesh_(std::make_shared<const data::Mesh>(std::move(mesh))),
          ctx_(broker::AppContext::Params{}) {
        scene::MeshObject mo;
        mo.mesh = mesh_;
        mo.transform = glm::mat4(1.0f);
        mo.presentation.phong.baseColor = glm::vec4(0.85f, 0.45f, 0.15f, 1.0f);
        const uint64_t meshId = ctx_.store().addMeshObject(std::move(mo));

        const data::Aabb& b = mesh_->bounds();
        const glm::vec3 center = 0.5f * (b.min + b.max);
        const float radius = 0.5f * glm::length(b.max - b.min);
        const float dist = radius / std::tan(0.5f * glm::radians(app::kDefaultFovYDeg));
        scene::SceneViewBuilder bld(1, scene::Rect{0, 0, app::kWindowWidth, app::kWindowHeight},
                                    {app::kDefaultFovYDeg, 0.1f, 2.0f * (dist + radius)});
        bld.withCamera(scene::Camera(center + glm::vec3(0.0f, 0.0f, dist), center, glm::vec3(0.0f, 1.0f, 0.0f)));
        bld.syncLive(app::kWindowWidth, app::kWindowHeight);
        view_ = bld.build();
        view_.setClearColor(glm::vec4(0.1f, 0.1f, 0.12f, 1.0f));
        view_.setItemIds({meshId});
        builder_ = std::move(bld);
        // Sync once more to ensure builder's internal view matches view_
        builder_.view() = view_;
    }

    void onResize(int width, int height) noexcept override {
        builder_.syncLive(width, height);
        view_ = builder_.view();
    }

    data::Result<void> renderFrame(int width, int height) override {
        // T10 interactor ordering — the long-lived peer must call GlfwCameraInteractor::update on the current view
        // before the broker sync so the WantCaptureMouse guard and the orthographic/PlaneDesc skip can veto the
        // mutation; the interactor then forwards the synthetic drag through CameraController::onMouseDrag and
        // finally View::setCamera so viewGen/cameraGen bump and the broker re-translates only dirty camera fields.
        interactor_.update(builder_.view());
        builder_.syncLive(width, height);
        view_ = builder_.view();
        views_ = {view_};
        return app::syncRenderPresent(ctx_, views_);
    }

    const char* title() const override {
        return "Mesh long-lived: bunny interactive orbit/pan/zoom (runInteractive)";
    }

    const char* instructions() const noexcept override {
        return "Long-lived mesh sample — bypasses sampleMaxFrames, runs until window close.\n"
               "Left-drag rotates (dx*0.5deg), right-drag pans (dx*0.01), scroll/middle-drag zooms exp(-dy*0.02)\n"
               "via scene::CameraController + app::GlfwCameraInteractor with WantCaptureMouse guard;\n"
               "View::setCamera bumps viewGen so broker re-translates only dirty camera fields.\n"
               "Not for automated testing — bounded peer re_sample_mesh is the gate sample.";
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

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--help" || a == "-h") {
            spdlog::info("re_sample_mesh_long — long-lived mesh sample (T10)");
            spdlog::info("Usage: {} [--help]", argv[0]);
            spdlog::info("Runs until window close via SampleHarness::runInteractive(), not bounded 300 frames.");
            spdlog::info("Camera: left-drag rotate dx*0.5deg, right-drag pan dx*0.01, scroll/middle zoom exp(-dy*0.02)");
            return 0;
        }
    }
    auto windowResult = core::Window::create(app::kWindowWidth, app::kWindowHeight, "Mesh Long - Interactive");
    if (windowResult.failed()) {
        spdlog::error("window: {}", windowResult.error().message);
        return 1;
    }
    const std::string meshPath = std::string(RE_SOURCE_DIR) + "/data/meshes/bunny.obj";
    auto meshResult = re::io::loadObjMesh(meshPath);
    if (meshResult.failed()) {
        spdlog::error("mesh long: failed to load '{}': {}", meshPath, meshResult.error().message);
        return 1;
    }
    auto sample = std::make_unique<MeshLongSample>(std::move(*meshResult));
    app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    // T10 long-lived bypass — the interactive peer intentionally ignores RE_SAMPLE_MAX_FRAMES and the bounded
    // kDefaultFrames=300 contract and instead drives SampleHarness::runInteractive() until the window's
    // shouldClose flag is set, matching the task's runInteractive until shouldClose() requirement for pan+rotate+zoom.
    return harness.runInteractive();
}
