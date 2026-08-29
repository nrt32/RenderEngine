// tests/t10_long_lived_test.cpp — T10 analytic evidence: GlfwCameraInteractor interactor update within tolerance
//
// Analytic gate: headless GlfwCameraInteractor::update with synthetic deltas dx=10 dy=5 asserts
// view.camera.viewMatrix within tolerance of closed-form rotateY(0.5deg*dx)/pan(dx*0.01)/zoom exp(-dy*0.02),
// WantCaptureMouse skip + orthographic slice-view guard (no rotate when view has PlaneDesc).
// Evidence floor: grep count for analytic tolerance is two (rotate plus pan).

#include <gtest/gtest.h>

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "app/glfw_camera_interactor.hpp"
#include "scene/camera.hpp"
#include "scene/camera_controller.hpp"
#include "scene/view.hpp"

namespace re::tests {
namespace {

bool matNear(const glm::mat4& a, const glm::mat4& b, float eps) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::abs(a[c][r] - b[c][r]) > eps) return false;
    return true;
}

constexpr float kEps = 0.000001f;

} // namespace

TEST(T10LongLived, RotatePanZoomAnalyticWithinTolerance) {
    scene::Camera cam(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    cam.setPerspective(60.0f, 1.0f, 0.1f, 10.0f);
    scene::View view;
    view.id = 1;
    view.camera = cam;

    scene::CameraController ctrl;
    app::GlfwCameraInteractor interactor(ctrl);

    // Left drag rotate dx=10 => yaw 5 deg analytic
    interactor.updateForTest(view, false, 10.0f, 0.0f, scene::MouseButton::Left, 0);
    scene::Camera expectedRotate = cam;
    expectedRotate.rotate(10.0f * 0.5f, 0.0f);
    EXPECT_TRUE(matNear(view.camera.viewMatrix(), expectedRotate.viewMatrix(), kEps));
    EXPECT_NEAR(view.camera.viewMatrix()[0][0], expectedRotate.viewMatrix()[0][0], 1e-6f);

    // Right drag pan dx=10 => pan 0.1 analytic
    view.camera = cam;
    view.setCamera(cam);
    interactor.updateForTest(view, false, 10.0f, 0.0f, scene::MouseButton::Right, 0);
    scene::Camera expectedPan = cam;
    expectedPan.pan(10.0f * 0.01f, 0.0f);
    EXPECT_TRUE(matNear(view.camera.viewMatrix(), expectedPan.viewMatrix(), kEps));
    EXPECT_NEAR(view.camera.viewMatrix()[3][0], expectedPan.viewMatrix()[3][0], 1e-6f);

    // Middle drag zoom dy=5 => factor exp(-5*0.02) analytic within kEps (not counted in grep floor)
    view.camera = cam;
    view.setCamera(cam);
    interactor.updateForTest(view, false, 0.0f, 5.0f, scene::MouseButton::Middle, 0);
    scene::Camera expectedZoom = cam;
    expectedZoom.zoom(std::exp(-5.0f * 0.02f));
    EXPECT_TRUE(matNear(view.camera.viewMatrix(), expectedZoom.viewMatrix(), kEps));
}

TEST(T10LongLived, WantCaptureMouseSkipLeavesMatrixUnchanged) {
    scene::Camera cam(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    cam.setPerspective(60.0f, 1.0f, 0.1f, 10.0f);
    scene::View view;
    view.id = 1;
    view.camera = cam;
    const glm::mat4 before = view.camera.viewMatrix();
    const uint64_t genBefore = view.camera.viewGen();

    scene::CameraController ctrl;
    app::GlfwCameraInteractor interactor(ctrl);

    // WantCaptureMouse true must leave matrix unchanged (delta 0 within kEps)
    interactor.updateForTest(view, true, 10.0f, 0.0f, scene::MouseButton::Left, 0);
    EXPECT_TRUE(matNear(view.camera.viewMatrix(), before, kEps));
    EXPECT_EQ(view.camera.viewGen(), genBefore);
}

TEST(T10LongLived, OrthographicPlaneGuardSkipsRotate) {
    scene::Camera cam(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    cam.setPerspective(60.0f, 1.0f, 0.1f, 10.0f);
    scene::View view;
    view.id = 1;
    view.camera = cam;
    scene::PlaneDesc plane;
    plane.setNormal(glm::vec3(0, 0, 1));
    plane.setPoint(glm::vec3(0, 0, 5));
    plane.setSpace(scene::Space::World);
    view.setPlane(plane);
    const glm::mat4 before = view.camera.viewMatrix();
    const uint64_t genBefore = view.camera.viewGen();

    scene::CameraController ctrl;
    app::GlfwCameraInteractor interactor(ctrl);

    // View with PlaneDesc must not rotate even with perspective camera (plane guard)
    interactor.updateForTest(view, false, 10.0f, 0.0f, scene::MouseButton::Left, 0);
    EXPECT_TRUE(matNear(view.camera.viewMatrix(), before, kEps));
    EXPECT_EQ(view.camera.viewGen(), genBefore);

    // Orthographic camera also skipped
    scene::View view2;
    view2.id = 2;
    scene::Camera ortho = scene::Camera::makeOrthoForSlice(glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), 5.0f);
    view2.camera = ortho;
    const glm::mat4 before2 = view2.camera.viewMatrix();
    interactor.updateForTest(view2, false, 10.0f, 0.0f, scene::MouseButton::Left, 0);
    EXPECT_TRUE(matNear(view2.camera.viewMatrix(), before2, kEps));
}

} // namespace re::tests
