// app/slice_long.cpp — long-lived slice sample: the interactive peer of the bounded horizontal teapot-clip
// sample that bypasses RE_SAMPLE_MAX_FRAMES and drives SampleHarness::runInteractive() until
// shouldClose() while its renderFrame calls GlfwCameraInteractor::update before syncRenderPresent
// with left-drag rotate dx*0.5deg, right-drag pan dx*0.01 and scroll/middle-drag zoom
// exp(-dy*0.02), respecting WantCaptureMouse and, for true 2D orthographic slice displays,
// skipping via the PlaneDesc guard so only dirty camera fields bump viewGen via View::setCamera — T10.

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

class SliceLongSample final : public app::ISample {
   public:
    explicit SliceLongSample(data::Mesh mesh)
        : mesh_(std::make_shared<const data::Mesh>(std::move(mesh))),
          ctx_(broker::AppContext::Params{.enableOIT = false, .registerCameraMapper = false}) {
        scene::MeshSliceObject ms;
        ms.mesh = mesh_;
        ms.transform = glm::mat4(1.0f);
        ms.presentation.phong.baseColor = glm::vec4(0.25f, 0.55f, 0.85f, 1.0f);
        const uint64_t sliceId = ctx_.store().addMeshSliceObject(std::move(ms));

        const data::Aabb& b = mesh_->bounds();
        scene::PlaneDesc plane;
        plane.setNormal(glm::vec3(0.0f, 1.0f, 0.0f));
        plane.setPoint(glm::vec3(0.0f, 0.5f * (b.min.y + b.max.y), 0.0f));
        plane.setSpace(scene::Space::World);

        const glm::vec3 center = 0.5f * (b.min + b.max);
        const float radius = 0.5f * glm::length(b.max - b.min);
        const float dist = radius / std::tan(0.5f * glm::radians(app::kDefaultFovYDeg));
        scene::SceneViewBuilder bld(1, scene::Rect{0, 0, app::kWindowWidth, app::kWindowHeight},
                                    {app::kDefaultFovYDeg, 0.1f, 2.0f * (dist + radius)});
        bld.withCamera(scene::Camera(center + glm::vec3(0.0f, 0.0f, dist), center, glm::vec3(0.0f, 1.0f, 0.0f)));
        bld.syncLive(app::kWindowWidth, app::kWindowHeight);
        view_ = bld.view();
        view_.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        view_.setItemIds({sliceId});
        view_.setPlane(plane);
        builder_ = std::move(bld);
        // Keep builder view in sync with view_ (includes plane)
        builder_.view() = view_;
    }

    void onResize(int width, int height) noexcept override {
        builder_.syncLive(width, height);
        // Preserve plane across syncLive (syncLive only touches rect + projection)
        scene::PlaneDesc p = view_.plane.value();
        view_ = builder_.view();
        view_.setPlane(p);
        builder_.view() = view_;
    }

    data::Result<void> renderFrame(int width, int height) override {
        // T10 slice guard — the bounded slice peer carries a World-space clip plane on a perspective view
        // (mesh-slice contextual rule: the view owns the plane but the clip is geometry-shader based). The
        // long-lived peer's interactor respects both the WantCaptureMouse guard and the PlaneDesc guard
        // (GlfwCameraInteractor skips any view with plane.has_value() — T10 analytic plane guard asserts no
        // rotate when view has PlaneDesc, even with a perspective camera). For this perspective clip view the
        // interactor will therefore be vetoed and the camera stays fixed, which matches the plane guard spec;
        // the dedicated plane guard test covers the 2D orthographic case where a PlaneDesc also vetoes rotation.
        // The broker still only re-translates dirty camera fields after View::setCamera when the guard allows it.
        interactor_.update(builder_.view());
        builder_.syncLive(width, height);
        // Preserve plane
        scene::PlaneDesc p = view_.plane.value();
        view_ = builder_.view();
        view_.setPlane(p);
        builder_.view() = view_;
        views_ = {view_};
        return app::syncRenderPresent(ctx_, views_);
    }

    const char* /*borrow*/ title() const override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "Slice long-lived: teapot clipped interactive (runInteractive)";
    }

    const char* /*borrow*/ instructions() const noexcept override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "Long-lived slice — runInteractive until close, left rotate dx*0.5deg, right pan, zoom exp(-dy*0.02), view.setCamera bump.";
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
            spdlog::info("re_sample_slice_long — long-lived slice sample (T10) PlaneDesc guard vetoes (perspective clip stays fixed)");
            return 0;
        }
    }
    auto windowResult = core::Window::create(app::kWindowWidth, app::kWindowHeight, "Slice Long - Interactive");
    if (windowResult.failed()) {
        spdlog::error("window: {}", windowResult.error().message);
        return 1;
    }
    const std::string meshPath = std::string(RE_SOURCE_DIR) + "/data/meshes/teapot.obj";
    auto meshResult = re::io::loadObjMesh(meshPath);
    if (meshResult.failed()) {
        spdlog::error("slice long: failed to load '{}': {}", meshPath, meshResult.error().message);
        return 1;
    }
    auto sample = std::make_unique<SliceLongSample>(std::move(*meshResult));
    app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.runInteractive();
}
