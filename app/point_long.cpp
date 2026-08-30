// app/point_long.cpp — long-lived Point sample: interactive peer of the bounded point sample (V7 T13, FR-render.8, FR-app.1).
//
// Bypasses RE_SAMPLE_MAX_FRAMES and drives SampleHarness::runInteractive() until shouldClose() while its
// renderFrame calls GlfwCameraInteractor::update on the 3D perspective view before syncRenderPresent with
// left-drag rotate dx*0.5deg / right-drag pan dx*0.01 / scroll/middle-drag zoom exp(-dy*0.02), respecting
// WantCaptureMouse and keeping the 2D orthographic ClipPlane axial view fixed (is2D skip: plane + MPR 2D orthographic
// displays are fixed to the dataset/plane extents and should not orbit). Engine::addPoint/addPointCloud flat
// PointObject{position, radius, worldUnits, color, fill} + PointCloudObject{vector<PointData>, worldUnits} is shared
// with the bounded peer — both demonstrate 3D Perspective spheres (MeshRenderer delegate for single, PointRenderer
// for cloud) and 2D ClipPlane circles (PointRenderer impostor gl_FragDepth flat, fill Hollow/GridDashed). 10-point
// cloud, worldUnits true→false 10px constant, radius worldUnits toggle demonstrably changes screen size with distance.
// Wire via AppContext + IViewBridge (no render/ include in app/ per acl_app_render), harness runInteractive
// discipline, long-lived EXCLUDE_FROM_ALL so ctest never runs it — T13.

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

#include "app/glfw_camera_interactor.hpp"
#include "app/sample_harness.hpp"
#include "broker/app_context.hpp"
#include "core/window.hpp"
#include "data/result.hpp"
#include "scene/camera.hpp"
#include "scene/camera_controller.hpp"
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

std::vector<scene::PointData> makeCloud10(float radius, const glm::vec4& color) {
    std::vector<scene::PointData> pts;
    pts.reserve(10);
    for (int i = 0; i < 10; ++i) {
        scene::PointData pd;
        float x = (i == 0) ? 0.0f : (static_cast<float>(i) * 0.2f - 1.0f);
        float y = (i == 0) ? 0.0f : (static_cast<float>(i % 3) * 0.2f - 0.2f);
        pd.pos = glm::vec3(x, y, 0.0f);
        pd.radius = radius;
        pd.color = color;
        uint32_t fillBits = 0u;
        if (i % 2 == 0) fillBits = 0u;
        else if (i % 3 == 0) fillBits = 1u;
        else fillBits = 2u;
        pd.fillBits = fillBits;
        pts.push_back(pd);
    }
    return pts;
}

// Long-lived Point sample: same Engine::addPoint/addPointCloud as bounded peer but interactive 1/255 1e-6
class PointLongSample final : public app::ISample {
   public:
    PointLongSample()
        : engine_(broker::AppContext::Params{}) {
        pointId_ = engine_.addPoint(glm::vec3(0.0f, 0.0f, 0.0f), 0.5f, glm::vec4(0.2f, 0.4f, 0.8f, 1.0f), true, scene::PointFill::Solid);
        auto cloudPx = makeCloud10(20.0f, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        cloudPxId_ = engine_.addPointCloud(cloudPx, false);
        auto cloudWorld = makeCloud10(0.3f, glm::vec4(0.2f, 0.4f, 0.8f, 1.0f));
        cloudWorldId_ = engine_.addPointCloud(cloudWorld, true);
        markerId_ = engine_.addPoint(glm::vec3(0.0f, 0.0f, 0.0f), 10.0f, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), false, scene::PointFill::Solid);
        hollowId_ = engine_.addPoint(glm::vec3(0.0f, 0.0f, 0.0f), 30.0f, glm::vec4(0.2f, 0.4f, 0.8f, 1.0f), false, scene::PointFill::Hollow);
        gridId_ = engine_.addPoint(glm::vec3(0.0f, 0.0f, 0.0f), 30.0f, glm::vec4(0.2f, 0.4f, 0.8f, 1.0f), false, scene::PointFill::GridDashed);
        syncViews(app::kWindowWidth, app::kWindowHeight);
        engine_.setViews(views_);
    }

    void onResize(int width, int height) noexcept override {
        syncViews(width, height);
        engine_.setViews(views_);
    }

    data::Result<void> renderFrame(int width, int height) override {
        syncViews(width, height);
        // Long-lived interactive: GlfwCameraInteractor::update only on 3D perspective view before syncRenderPresent 1/255 1e-6
        // Respect WantCaptureMouse and skip 2D orthographic ClipPlane view (is2D fixed framing) per V5 T9 guard
        interactor_.update(views_[0]);
        engine_.setViews(views_);
        return engine_.render();
    }

    const char* /*borrow*/ title() const override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "Point long-lived: 10-cloud interactive 3D orbit + 2D ClipPlane fixed (runInteractive) 1/255";
    }

    const char* /*borrow*/ instructions() const noexcept override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "Long-lived Point — runInteractive until close, 3D view orbit/pan/zoom via CameraController+GlfwCameraInteractor, 2D ClipPlane axial fixed per plane guard 1/255.";
    }

   private:
    void syncViews(int width, int height) {
        const int leftW = width / 2;
        const int rightW = width - leftW;
        // 3D perspective Camera::perspective 45 fov 1/255 1e-6
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
        // 2D orthographic ClipPlane Space::World axial Camera::ortho -2,2,-1.5,1.5 PlaneDesc Space::World 1/255
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
    scene::CameraController controller_{};
    app::GlfwCameraInteractor interactor_{controller_};
};

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--help" || a == "-h") {
            spdlog::info("re_sample_point_long — long-lived Point sample (T13) interactive 3D orbit + 2D ClipPlane fixed");
            return 0;
        }
    }
    auto wr = core::Window::create(app::kWindowWidth, app::kWindowHeight, "Point Long - Interactive 1/255");
    if (wr.failed()) { spdlog::error("window: {}", wr.error().message); return 1; }
    auto sample = std::make_unique<PointLongSample>();
    app::SampleHarness harness(std::move(*wr), std::move(sample));
    return harness.runInteractive();
}
