// app/oit_long.cpp — long-lived OIT sample: the interactive peer of the bounded linked-list OIT sample
// that bypasses RE_SAMPLE_MAX_FRAMES and drives SampleHarness::runInteractive() until shouldClose()
// while its renderFrame calls GlfwCameraInteractor::update before syncRenderPresent with left-drag
// rotate dx*0.5deg, right-drag pan dx*0.01 and scroll/middle-drag zoom exp(-dy*0.02), respecting
// WantCaptureMouse and keeping the depth-tested target's DepthConfig{true} so viewGen bumps only
// dirty camera fields via View::setCamera; EXCLUDE_FROM_ALL so ctest never runs it — T10.

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/glfw_camera_interactor.hpp"
#include "app/mpr_slice.hpp"
#include "app/oit_scene.hpp"
#include "app/sample_harness.hpp"
#include "broker/app_context.hpp"
#include "core/window.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "io/mesh/obj_mesh_loader.hpp"
#include "scene/camera_controller.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

namespace app = re::app;
namespace broker = re::broker;
namespace core = re::core;
namespace data = re::data;
namespace oit = re::app::oit_scene;
namespace scene = re::scene;

class OitLongSample final : public app::ISample {
   public:
    explicit OitLongSample(data::Mesh bunny)
        : bunny_(std::make_shared<const data::Mesh>(std::move(bunny))),
          ctx_(broker::AppContext::Params{.enableOIT = true, .registerCameraMapper = false}) {
        const data::Aabb& bunnyBounds = bunny_->bounds();
        const uint64_t idGold = ctx_.store().addMeshObject(makeBoxObject(oit::kGoldMin, oit::kGoldMax, oit::kGoldColor));
        const uint64_t idBunny = ctx_.store().addMeshObject(makeBunnyObject(bunnyBounds));
        const uint64_t idNear = ctx_.store().addMeshObject(makeBoxObject(oit::kNearGlassMin, oit::kNearGlassMax, oit::kNearGlassColor));
        const uint64_t idFar = ctx_.store().addMeshObject(makeBoxObject(oit::kFarGlassMin, oit::kFarGlassMax, oit::kFarGlassColor));
        view_.id = 1;
        syncViewSize(app::kWindowWidth, app::kWindowHeight);
        view_.setClearColor(oit::kClearColor);
        view_.setDepthConfig(scene::DepthConfig{true});
        view_.setItemIds({idGold, idBunny, idNear, idFar});
    }

    void onResize(int width, int height) noexcept override {
        syncViewSize(width, height);
    }

    data::Result<void> renderFrame(int width, int height) override {
        syncViewSize(width, height);
        interactor_.update(view_);
        views_ = {view_};
        return app::syncRenderPresent(ctx_, views_);
    }

    const char* /*borrow*/ title() const override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "OIT long-lived: opaque+glass interactive (runInteractive)";
    }

    const char* /*borrow*/ instructions() const noexcept override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "Long-lived OIT — runInteractive, orbit/pan/zoom via CameraController, WantCaptureMouse guard, view.setCamera bump.";
    }

   private:
    void syncViewSize(int width, int height) {
        view_.setRect(scene::Rect{0, 0, width, height});
        const float aspect = app::aspectFromDims(width, height);
        if (view_.camera.isPerspective()) {
            view_.setCamera(oit::cameraFor(aspect));
        } else {
            scene::Camera cam = view_.camera;
            cam.setOrtho(-aspect, aspect, -1.0f, 1.0f, oit::kNearPlane, oit::kFarPlane);
            view_.setCamera(std::move(cam));
        }
    }

    static scene::MeshObject makeBoxObject(const glm::vec3& minCorner, const glm::vec3& maxCorner, const glm::vec4& color) {
        scene::MeshObject obj;
        obj.mesh = std::make_shared<const data::Mesh>(app::makeBoxMesh(minCorner, maxCorner));
        obj.transform = glm::mat4(1.0f);
        obj.presentation.phong.baseColor = color;
        return obj;
    }

    scene::MeshObject makeBunnyObject(const data::Aabb& bounds) {
        scene::MeshObject obj;
        obj.mesh = bunny_;
        obj.transform = oit::bunnyModel(bounds);
        obj.presentation.phong.baseColor = oit::kBunnyColor;
        return obj;
    }

    std::shared_ptr<const data::Mesh> bunny_;
    broker::AppContext ctx_;
    scene::View view_{};
    std::vector<scene::View> views_{};
    scene::CameraController controller_{};
    app::GlfwCameraInteractor interactor_{controller_};
};

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--help" || a == "-h") {
            spdlog::info("re_sample_oit_long — long-lived OIT sample (T10) interactive");
            return 0;
        }
    }
    auto windowResult = core::Window::create(app::kWindowWidth, app::kWindowHeight, "OIT Long - Interactive");
    if (windowResult.failed()) {
        spdlog::error("window: {}", windowResult.error().message);
        return 1;
    }
    const std::string meshPath = std::string(RE_SOURCE_DIR) + "/data/meshes/bunny.obj";
    auto meshResult = re::io::loadObjMesh(meshPath);
    if (meshResult.failed()) {
        spdlog::error("oit long: failed to load '{}': {}", meshPath, meshResult.error().message);
        return 1;
    }
    auto sample = std::make_unique<OitLongSample>(std::move(*meshResult));
    app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.runInteractive();
}
