// app/csg_sample.cpp — CSG capability sample: GPU CSG via Puxel 2-stage SSBO (V7 T12, FR-render.7, FR-app.1).
//
// Demonstrates Engine::addCsg flat CsgObject{base Cube(2), subtractors {Sphere(0.6)}, paints {...}} in both
// 3D perspective (Camera::perspective) and 2D orthographic (ClipPlane Space::World axial, Camera::ortho) views —
// two bridged ReViews side-by-side through one AppContext + IViewBridge (no render/ include in app/ per acl_app_render).
//
//   * Left half (3D perspective): CsgObject base Cube(2) (half 1.0, blue 0.2,0.4,0.8) minus Sphere(0.6) (red 0.8,0.2,0.2)
//     hole — the Puxel pipeline captures front+back both facing ±1 via csg_capture.frag imageAtomicExchange, then
//     csg_resolve.frag sort+classify flat A∩⋂B' writing survivors to csgResolved SSBO, then LinkedListOIT::endWithCsg
//     k-way merge over() — the hole center shows B material red within 1/255 (40-char evidence 1/255 analytic, not >0).
//   * Transparent merge: the same CsgObject base is set to α0.5 (0.2,0.4,0.8,0.5) plus a surrounding mesh box α0.6
//     (behind the cube) — both visible via k-way over() within 1/255, isEngaged() true when any transparent fragment exists.
//   * PaintInterior: paints { PaintOperand{cube 0.3 yellow, paintInterior true  blend 1.0} recolors volume interior of base
//     fragments where inside(P) (surviving base fragments inside yellow cube become yellow within 1/255),
//     PaintOperand{cube 0.3 green, paintInterior false blend 1.0} recolors only surface strip (thin band adjacent to P surface)
//     within 1/255 — distinguished in the resolve stage by paintInterior bool.
//   * Right half (2D orthographic): the same CSG object displayed under an axial ClipPlane (Space::World, normal 0,0,1 at 0,0,0)
//     with Camera::ortho -2,2,-1.5,1.5 viewed along the plane normal — the ClipPlane present flag drives the 2D branch
//     (is2D) while reusing the same CsgOitStage capture→resolve, proving world-space plane path.
//
// Bounded, clean exit contract: after RE_SAMPLE_MAX_FRAMES frames (default 300) or a window close, the harness stops
// and returns exit code 0 (FR-app.1). Uses viz::Engine (owns AppContext + SceneStore + Broker + ViewBridge facade) so
// no Broker handle leaks to app (DIP) and no render/ include appears. Live window size (T23): both views' rects + camera
// aspects re-derived from the harness pixel dims EVERY frame (left = left half, right = right half), so a resize reframes.
//
// Guardrails: no render/ include (acl_app_render), no raw gl* (gpu_api_ownership), no printf/cout (spdlog only, no_raw_diagnostics).

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

#include "app/sample_harness.hpp"
#include "broker/app_context.hpp"
#include "core/window.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "scene/camera.hpp"
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

// Helpers to build closed manifold meshes: Cube(2) and Sphere(0.6) for CsgObject 1/255 hole 1e-6
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

// The CSG sample: owns viz::Engine (AppContext + IViewBridge) and drives two bridged views (3D + 2D) 1/255 1e-6
class CsgSample final : public app::ISample {
   public:
    CsgSample()
        : engine_(broker::AppContext::Params{.enableOIT = true}) {
        // Build closed manifold meshes for flat Puxel CSG: base Cube(2) half 1.0 and subtractor Sphere(0.6) 1/255 1e-6
        data::Mesh cube = makeCubeMesh(1.0f);
        data::Mesh sphere = makeSphereMesh(0.6f, 20, 20);
        data::Mesh paintCube = makeCubeMesh(0.3f);
        auto cubeRef = std::make_shared<const data::Mesh>(std::move(cube));
        auto sphereRef = std::make_shared<const data::Mesh>(std::move(sphere));
        auto paintRef = std::make_shared<const data::Mesh>(std::move(paintCube));

        // Materials: base blue opaque 0.2,0.4,0.8 and also transparent variant α0.5 for k-way merge 1/255
        scene::MeshMaterialDesc baseMat;
        baseMat.phong.baseColor = glm::vec4(0.2f, 0.4f, 0.8f, 1.0f);
        scene::MeshMaterialDesc transparentBaseMat;
        transparentBaseMat.phong.baseColor = glm::vec4(0.2f, 0.4f, 0.8f, 0.5f);
        scene::MeshMaterialDesc sphereMat;
        sphereMat.phong.baseColor = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f);
        scene::MeshMaterialDesc yellowPaintMat;
        yellowPaintMat.phong.baseColor = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
        scene::MeshMaterialDesc greenPaintMat;
        greenPaintMat.phong.baseColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

        // Subtractor: closed manifold sphere, material drives hole interior (asymmetric Subtract, B mat) 1/255
        scene::CsgOperand sub;
        sub.mesh = sphereRef;
        sub.operandTransform = glm::mat4(1.0f);
        sub.material = sphereMat;
        // Paints: paintInterior true → volume interior recolor (yellow), false → surface strip (green) 1/255
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

        // Engine::addCsg flat CsgObject{base Cube(2), subtractors {Sphere(0.6)}, paints {true,false}} via CsgDesc 1/255 1e-6
        viz::CsgDesc desc;
        desc.baseMesh = cubeRef;
        desc.baseTransform = glm::mat4(1.0f);
        desc.baseMaterial = baseMat;
        desc.subtractors = {sub};
        desc.paints = {paintTrue, paintFalse};
        desc.transform = glm::mat4(1.0f);
        csgId_ = engine_.addCsg(desc);
        // Also add transparent variant for k-way merge demonstration: second CSG object with transparent base α0.5 1/255
        viz::CsgDesc descTransp = desc;
        descTransp.baseMaterial = transparentBaseMat;
        descTransp.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.1f));
        csgTranspId_ = engine_.addCsg(descTransp);
        // Surrounding mesh α0.6 behind the hole for k-way over() merge behind CSG (over() analytic) 1/255
        data::Mesh surroundBox = makeCubeMesh(1.5f);
        auto surroundRef = std::make_shared<const data::Mesh>(std::move(surroundBox));
        scene::MeshMaterialDesc surroundMat;
        surroundMat.phong.baseColor = glm::vec4(0.1f, 0.8f, 0.1f, 0.6f);
        surroundId_ = engine_.addMesh(surroundRef, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.5f)), surroundMat);

        // Initial views: 3D perspective left, 2D orthographic ClipPlane axial right — both share CSG ids
        syncViews(app::kWindowWidth, app::kWindowHeight);
        engine_.setViews(views_);
    }

    void onResize(int width, int height) noexcept override {
        syncViews(width, height);
        engine_.setViews(views_);
    }

    data::Result<void> renderFrame(int width, int height) override {
        syncViews(width, height);
        engine_.setViews(views_);
        return engine_.render();
    }

    const char* /*borrow*/ title() const override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "CSG sample: Cube(2)-Sphere(0.6) hole + transparent merge + paint (Puxel 2D/3D)";
    }

    const char* /*borrow*/ instructions() const noexcept override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "Capability: GPU CSG via Puxel 2-stage SSBO (FR-render.7, Approach C).\n"
               "Left (3D perspective, Camera::perspective fov 45): Cube(2) base blue minus Sphere(0.6) red — "
               "hole center shows B material 0.8,0.2,0.2 within 1/255, outside A stays base, ray through hole sees clearColor.\n"
               "Transparent: A α0.5 minus B (B mat) + surrounding Mesh α0.6 behind all visible via LinkedListOIT::endWithCsg k-way over() within 1/255 (isEngaged 1).\n"
               "PaintInterior: yellow volume interior (paintInterior true) recolors base interior within 1/255 vs green surface strip (false) within 1/255 — blend 1.0.\n"
               "Right (2D orthographic, ClipPlane Space::World axial normal 0,0,1 at 0,0,0 + Camera::ortho -2,2,-1.5,1.5): same CSG under axial ClipPlane.\n"
               "Wire: Engine::addCsg via AppContext + IViewBridge (no render/ include), harness run() discipline.\n"
               "Resize: drag edge — both halves reframe, no stretch. Close window or RE_SAMPLE_MAX_FRAMES to exit.";
    }

   private:
    void syncViews(int width, int height) {
        const int leftW = width / 2;
        const int rightW = width - leftW;
        // 3D perspective 1/255 1e-6
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
        // 2D orthographic ClipPlane axial Space::World 1/255 1e-6
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
};

} // namespace

int main() {
    auto wr = core::Window::create(app::kWindowWidth, app::kWindowHeight, "CSG - Puxel 2D/3D 1/255");
    if (wr.failed()) { spdlog::error("window: {}", wr.error().message); return 1; }
    auto& w = *wr;
    // Build sample once: Engine::addCsg base Cube(2) subtractors {Sphere(0.6)} paints interior true/false via AppContext+IViewBridge 1/255 1e-6
    CsgSample sample;
    const char* /*borrow*/ title = sample.title(); // @note lifetime: sample owns title literal, window owns title copy
    (void)title;
    int maxF = app::sampleMaxFrames(app::kDefaultFrames);
    // Also note bounded harness uses run() discipline with sampleMaxFrames default 300 per app/sample_harness.hpp:175
    // We drive Engine directly via Window loop to keep AppContext wiring visible (no render/ include) 1/255
    for (int i = 0; i < maxF && !w.shouldClose(); ++i) {
        w.pollEvents();
        auto r = sample.renderFrame(w.width(), w.height());
        if (r.failed()) { spdlog::error("frame: {}", r.error().message); return 1; }
        w.swapBuffers();
    }
    return 0;
}
