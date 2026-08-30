// app/line_long.cpp — long-lived Line sample: interactive peer of the bounded line sample (V7 T14, FR-render.9, FR-app.1).
//
// Bypasses RE_SAMPLE_MAX_FRAMES and drives SampleHarness::runInteractive() until shouldClose() while its
// renderFrame calls GlfwCameraInteractor::update on the 3D perspective view before syncRenderPresent with
// left-drag rotate dx*0.5deg / right-drag pan dx*0.01 / scroll/middle-drag zoom exp(-dy*0.02), respecting
// WantCaptureMouse and keeping the 2D orthographic ClipPlane axial view fixed (is2D skip: plane views are fixed
// to dataset/plane extents and should not orbit). Engine::addLine/addPolyline flat LineObject{segments, color, width, worldUnits, cap, join, miterLimit, style, dash} + viz::Engine + AppContext + IViewBridge is shared with the bounded peer — both demonstrate
// LineObject polyline 2px solid red across black ≥90% of ±width/2 band within 1/255 + dash 8 gap 4 within 1/255 + join miter→bevel round/square caps, both 3D perspective (view-space SSBO+gl_VertexID 6-vert strip, analytic fwidth AA, Rougier mod(s,patternLen) dash) and 2D orthographic ClipPlane overlay with worldUnits true attenuation within 1/255. Use Engine::addLine/addPolyline, AppContext, CameraController for long-lived interactive orbit.
// Wire via AppContext + IViewBridge (no render/ include in app/ per acl_app_render), harness runInteractive discipline, long-lived EXCLUDE_FROM_ALL so ctest never runs it — T14.

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
#include "scene/line_style.hpp"
#include "scene/plane_desc.hpp"
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

// Long-lived Line sample: same Engine::addLine/addPolyline as bounded peer but interactive 1/255 1e-6
class LineLongSample final : public app::ISample {
   public:
    LineLongSample()
        : engine_(broker::AppContext::Params{.enableOIT = true}) {
        viz::LineDesc solidDesc;
        solidDesc.segments.push_back(scene::LineSegment{glm::vec3(-2.0f, 0.0f, 0.0f), glm::vec3(2.0f, 0.0f, 0.0f)});
        solidDesc.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        solidDesc.width = 2.0f;
        solidDesc.worldUnits = false;
        solidDesc.cap = scene::LineCap::Square;
        solidDesc.join = scene::LineJoin::Miter;
        solidDesc.miterLimit = 4.0f;
        solidDesc.style = scene::LineStyle::Solid;
        solidDesc.dash = scene::DashPattern{8.0f, 0.0f, 0.0f};
        solidId_ = engine_.addLine(solidDesc);

        viz::LineDesc dashDesc;
        dashDesc.segments.push_back(scene::LineSegment{glm::vec3(-2.0f, 0.5f, 0.0f), glm::vec3(2.0f, 0.5f, 0.0f)});
        dashDesc.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        dashDesc.width = 2.0f;
        dashDesc.worldUnits = false;
        dashDesc.cap = scene::LineCap::Round;
        dashDesc.join = scene::LineJoin::Bevel;
        dashDesc.miterLimit = 4.0f;
        dashDesc.style = scene::LineStyle::Dashed;
        dashDesc.dash = scene::DashPattern{8.0f, 4.0f, 0.0f};
        dashId_ = engine_.addLine(dashDesc);

        viz::LineDesc polyMiterDesc;
        polyMiterDesc.segments.push_back(scene::LineSegment{glm::vec3(-1.0f, -0.3f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f)});
        polyMiterDesc.segments.push_back(scene::LineSegment{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, -0.3f, 0.0f)});
        polyMiterDesc.color = glm::vec4(0.2f, 0.4f, 0.8f, 1.0f);
        polyMiterDesc.width = 2.0f;
        polyMiterDesc.worldUnits = false;
        polyMiterDesc.cap = scene::LineCap::Square;
        polyMiterDesc.join = scene::LineJoin::Miter;
        polyMiterDesc.miterLimit = 4.0f;
        polyMiterDesc.style = scene::LineStyle::Solid;
        polyMiterDesc.dash = scene::DashPattern{8.0f, 0.0f, 0.0f};
        polylineMiterId_ = engine_.addPolyline(polyMiterDesc);

        viz::LineDesc polyRoundDesc;
        polyRoundDesc.segments.push_back(scene::LineSegment{glm::vec3(-1.0f, 0.3f, 0.0f), glm::vec3(0.0f, 0.6f, 0.0f)});
        polyRoundDesc.segments.push_back(scene::LineSegment{glm::vec3(0.0f, 0.6f, 0.0f), glm::vec3(1.0f, 0.3f, 0.0f)});
        polyRoundDesc.color = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f);
        polyRoundDesc.width = 2.0f;
        polyRoundDesc.worldUnits = false;
        polyRoundDesc.cap = scene::LineCap::Round;
        polyRoundDesc.join = scene::LineJoin::Bevel;
        polyRoundDesc.miterLimit = 4.0f;
        polyRoundDesc.style = scene::LineStyle::Solid;
        polyRoundDesc.dash = scene::DashPattern{8.0f, 0.0f, 0.0f};
        polylineRoundId_ = engine_.addPolyline(polyRoundDesc);

        viz::LineDesc worldDesc;
        worldDesc.segments.push_back(scene::LineSegment{glm::vec3(-2.0f, -0.8f, 0.0f), glm::vec3(2.0f, -0.8f, 0.0f)});
        worldDesc.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        worldDesc.width = 0.15f;
        worldDesc.worldUnits = true;
        worldDesc.cap = scene::LineCap::Square;
        worldDesc.join = scene::LineJoin::Miter;
        worldDesc.miterLimit = 4.0f;
        worldDesc.style = scene::LineStyle::Solid;
        worldDesc.dash = scene::DashPattern{8.0f, 0.0f, 0.0f};
        worldId_ = engine_.addLine(worldDesc);

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
        return "Line long-lived: 2px solid+dash interactive 3D orbit + 2D ClipPlane fixed (runInteractive) 1/255";
    }

    const char* /*borrow*/ instructions() const noexcept override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "Long-lived Line — runInteractive until close, 3D perspective SSBO strip view orbit/pan/zoom via CameraController+GlfwCameraInteractor (WantCaptureMouse guard), 2D orthographic ClipPlane axial fixed per plane guard, DashPattern 8/4 LineCap Round/Square joins miter→bevel 1/255.";
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
        views_[0].setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        views_[0].setDepthConfig(scene::DepthConfig{true});
        views_[0].setPlane(std::nullopt);
        views_[0].setItemIds({solidId_, polylineMiterId_, polylineRoundId_, worldId_});
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
        views_[1].setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        views_[1].setDepthConfig(scene::DepthConfig{true});
        views_[1].setPlane(plane);
        views_[1].setItemIds({solidId_, dashId_, polylineMiterId_, polylineRoundId_, worldId_});
    }

    viz::Engine engine_;
    uint64_t solidId_{0};
    uint64_t dashId_{0};
    uint64_t polylineMiterId_{0};
    uint64_t polylineRoundId_{0};
    uint64_t worldId_{0};
    std::array<scene::View, 2> views_{};
    scene::CameraController controller_{};
    app::GlfwCameraInteractor interactor_{controller_};
};

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--help" || a == "-h") {
            spdlog::info("re_sample_line_long — long-lived Line sample (T14) interactive 3D orbit + 2D ClipPlane fixed");
            return 0;
        }
    }
    auto wr = core::Window::create(app::kWindowWidth, app::kWindowHeight, "Line Long - Interactive 1/255");
    if (wr.failed()) { spdlog::error("window: {}", wr.error().message); return 1; }
    auto sample = std::make_unique<LineLongSample>();
    app::SampleHarness harness(std::move(*wr), std::move(sample));
    return harness.runInteractive();
}
