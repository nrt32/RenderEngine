// app/csg_long.cpp — long-lived CSG sample: interactive peer of the bounded Puxel CSG sample (V7 T12, FR-render.7, FR-app.1).
//
// Bypasses RE_SAMPLE_MAX_FRAMES and drives SampleHarness::runInteractive() until shouldClose() while its
// renderFrame calls GlfwCameraInteractor::update on the 3D perspective view before syncRenderPresent with
// left-drag rotate dx*0.5deg / right-drag pan dx*0.01 / scroll/middle-drag zoom exp(-dy*0.02), respecting
// WantCaptureMouse and keeping the 2D orthographic ClipPlane axial view fixed (is2D skip: plane + MPR 2D orthographic
// displays are fixed to the dataset/plane extents and should not orbit). Engine::addCsg flat
// CsgObject{base Cube(2), subtractors {Sphere(0.6)}, paints {paintInterior true volume interior vs false surface strip}}
// is shared with the bounded peer — both demonstrate 3D perspective (Camera::perspective) and 2D orthographic
// (ClipPlane Space::World axial, Camera::ortho) views. Hole shows B material 1/255, transparent(A α0.5)−B + surrounding
// Mesh α0.6 k-way merge over() 1/255, paintInterior true (volume interior recolor) vs false (surface strip) 1/255.
// Wire via AppContext + IViewBridge (no render/ include in app/ per acl_app_render), harness runInteractive
// discipline, long-lived EXCLUDE_FROM_ALL so ctest never runs it — T12.

#include <spdlog/spdlog.h>

#include <array>
#include <cmath>
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
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "scene/camera.hpp"
#include "scene/camera_controller.hpp"
#include "scene/csg_op.hpp"
#include "scene/material_desc.hpp"
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

data::Mesh makeCubeMesh(float half) {
    std::vector<glm::vec3> p = {
        {-half, -half, -half}, {half, -half, -half}, {half, half, -half}, {-half, half, -half},
        {-half, -half, half}, {half, -half, half}, {half, half, half}, {-half, half, half},
    };
    std::vector<std::uint32_t> idx = {
        0,1,2, 0,2,3,  4,6,5, 4,7,6,  0,4,5, 0,5,1,  2,6,7, 2,7,3,  0,3,7, 0,7,4,  1,5,6, 1,6,2
    };
    return data::Mesh::fromTriangles(std::move(p), std::move(idx));
}

data::Mesh makeSphereMesh(float radius, int lat = 16, int lon = 16) {
    std::vector<glm::vec3> pos;
    std::vector<std::uint32_t> idx;
    for (int i = 0; i <= lat; ++i) {
        float v = static_cast<float>(i) / static_cast<float>(lat);
        float phi = v * 3.14159265359f;
        for (int j = 0; j <= lon; ++j) {
            float u = static_cast<float>(j) / static_cast<float>(lon);
            float theta = u * 2.0f * 3.14159265359f;
            float x = radius * std::sin(phi) * std::cos(theta);
            float y = radius * std::cos(phi);
            float z = radius * std::sin(phi) * std::sin(theta);
            pos.emplace_back(x, y, z);
        }
    }
    for (int i = 0; i < lat; ++i) {
        for (int j = 0; j < lon; ++j) {
            int a = i * (lon + 1) + j;
            int b = a + lon + 1;
            idx.push_back(static_cast<std::uint32_t>(a));
            idx.push_back(static_cast<std::uint32_t>(b));
            idx.push_back(static_cast<std::uint32_t>(a + 1));
            idx.push_back(static_cast<std::uint32_t>(b));
            idx.push_back(static_cast<std::uint32_t>(b + 1));
            idx.push_back(static_cast<std::uint32_t>(a + 1));
        }
    }
    return data::Mesh::fromTriangles(std::move(pos), std::move(idx));
}

// Long-lived CSG sample: same Engine::addCsg flat CsgObject as bounded peer but interactive 1/255 1e-6
class CsgLongSample final : public app::ISample {
   public:
    CsgLongSample()
        : engine_(broker::AppContext::Params{.enableOIT = true}) {
        data::Mesh cube = makeCubeMesh(1.0f);
        data::Mesh sphere = makeSphereMesh(0.6f, 20, 20);
        data::Mesh paintCube = makeCubeMesh(0.3f);
        auto cubeRef = std::make_shared<const data::Mesh>(std::move(cube));
        auto sphereRef = std::make_shared<const data::Mesh>(std::move(sphere));
        auto paintRef = std::make_shared<const data::Mesh>(std::move(paintCube));
        scene::MeshMaterialDesc baseMat;
        baseMat.phong.baseColor = glm::vec4(0.2f, 0.4f, 0.8f, 1.0f);
        scene::MeshMaterialDesc sphereMat;
        sphereMat.phong.baseColor = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f);
        scene::MeshMaterialDesc yellowPaintMat;
        yellowPaintMat.phong.baseColor = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
        scene::MeshMaterialDesc greenPaintMat;
        greenPaintMat.phong.baseColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
        scene::CsgOperand sub;
        sub.mesh = sphereRef;
        sub.operandTransform = glm::mat4(1.0f);
        sub.material = sphereMat;
        scene::CsgOperand paintOpTrue;
        paintOpTrue.mesh = paintRef;
        paintOpTrue.operandTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 0.0f, 0.0f));
        paintOpTrue.material = yellowPaintMat;
        scene::CsgOperand paintOpFalse;
        paintOpFalse.mesh = paintRef;
        paintOpFalse.operandTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-0.3f, 0.0f, 0.0f));
        paintOpFalse.material = greenPaintMat;
        scene::CsgPaintOperand paintTrue{paintOpTrue, true, 1.0f};
        scene::CsgPaintOperand paintFalse{paintOpFalse, false, 1.0f};
        viz::CsgDesc desc;
        desc.baseMesh = cubeRef;
        desc.baseTransform = glm::mat4(1.0f);
        desc.baseMaterial = baseMat;
        desc.subtractors = {sub};
        desc.paints = {paintTrue, paintFalse};
        desc.transform = glm::mat4(1.0f);
        // Engine::addCsg flat CsgObject{base Cube(2), subtractors {Sphere(0.6)}, paints {paintInterior true/false}} 1/255
        csgId_ = engine_.addCsg(desc);
        // Transparent base α0.5 for k-way merge plus surrounding α0.6 behind hole 1/255 over()
        viz::CsgDesc descTransp = desc;
        descTransp.baseMaterial.phong.baseColor = glm::vec4(0.2f, 0.4f, 0.8f, 0.5f);
        descTransp.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.1f));
        csgTranspId_ = engine_.addCsg(descTransp);
        data::Mesh surroundBox = makeCubeMesh(1.5f);
        auto surroundRef = std::make_shared<const data::Mesh>(std::move(surroundBox));
        scene::MeshMaterialDesc surroundMat;
        surroundMat.phong.baseColor = glm::vec4(0.1f, 0.8f, 0.1f, 0.6f);
        surroundId_ = engine_.addMesh(surroundRef, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.5f)), surroundMat);
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
        return "CSG long-lived: Cube(2)-Sphere(0.6) interactive 3D orbit + 2D fixed (runInteractive)";
    }

    const char* /*borrow*/ instructions() const noexcept override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "Long-lived CSG — runInteractive until close, 3D view orbit/pan/zoom via CameraController+GlfwCameraInteractor, 2D orthographic ClipPlane skip.";
    }

   private:
    void syncViews(int width, int height) {
        const int leftW = width / 2;
        const int rightW = width - leftW;
        // 3D perspective Camera::perspective 45 fov 1/255 1e-6
        scene::Camera cam3d(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const float aspect3d = app::aspectFromDims(leftW, height);
        cam3d.setPerspective(45.0f, aspect3d, 0.1f, 10.0f);
        views_[0].id = 1;
        views_[0].rect = scene::Rect{0, 0, leftW, height};
        views_[0].camera = cam3d;
        views_[0].setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        views_[0].setDepthConfig(scene::DepthConfig{true});
        views_[0].setPlane(std::nullopt);
        views_[0].setItemIds({csgId_, csgTranspId_, surroundId_});
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
        views_[1].setItemIds({csgId_, csgTranspId_, surroundId_});
    }

    viz::Engine engine_;
    uint64_t csgId_{0};
    uint64_t csgTranspId_{0};
    uint64_t surroundId_{0};
    std::array<scene::View, 2> views_{};
    scene::CameraController controller_{};
    app::GlfwCameraInteractor interactor_{controller_};
};

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--help" || a == "-h") {
            spdlog::info("re_sample_csg_long — long-lived CSG sample (T12) interactive 3D orbit + 2D fixed");
            return 0;
        }
    }
    auto wr = core::Window::create(app::kWindowWidth, app::kWindowHeight, "CSG Long - Interactive 1/255");
    if (wr.failed()) { spdlog::error("window: {}", wr.error().message); return 1; }
    auto sample = std::make_unique<CsgLongSample>();
    app::SampleHarness harness(std::move(*wr), std::move(sample));
    return harness.runInteractive();
}
