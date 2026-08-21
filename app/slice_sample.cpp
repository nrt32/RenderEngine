// app/slice_sample.cpp — mesh slice rendering sample (T13, FR-app.1).
//
// Demonstrates the mesh-slice capability (SPEC §1 capability 4, FR-render.4):
// loads the Utah teapot (data/meshes, SPEC §7), defines a horizontal clip plane
// through its vertical midpoint, and renders the clipped mesh (the kept
// half-space `dot(normal, p - point) >= 0`) through render::SliceRenderer into
// the window's default framebuffer (null RenderTarget::framebuffer binds the
// default, T12). Slicing is geometry, not compositing (SPEC §3): the geometry
// shader clips each triangle against the plane purely on the GPU and emits the
// on-plane cross-section, so the sample shows the mesh cut open along the
// plane.
//
// The sample is driven through the shared app::SampleHarness (visible window +
// ImGui overlay + run loop) exactly like the T12 samples, and exits cleanly
// (code 0) after RE_SAMPLE_MAX_FRAMES frames (default 300), so the gate can run
// it headlessly under Xvfb within a timeout (FR-app.1: exit code 0, no
// sanitizer reports).

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
#include "core/window.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "io/mesh/obj_mesh_loader.hpp"
#include "render/phong_material.hpp"
#include "render/slice_renderer.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

// The harness window size.
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
// Default number of frames before the sample exits cleanly (gate overrides via
// RE_SAMPLE_MAX_FRAMES).
constexpr int kDefaultFrames = 300;
// Perspective vertical field of view in radians (~60 deg).
constexpr float kFovY = 1.0471975511965976f;

/// Build a perspective camera framing `mesh`: eye pulled back along +Z from the
/// mesh's AABB center by `radius / tan(fov/2)`, looking at the center.
re::render::Camera makeFramingCamera(const re::data::Mesh& mesh) {
    const re::data::Aabb& b = mesh.bounds();
    const glm::vec3 center = 0.5f * (b.min + b.max);
    const glm::vec3 extent = b.max - b.min;
    const float radius = 0.5f * glm::length(extent);
    const float dist = radius / std::tan(0.5f * kFovY);

    re::render::Camera camera;
    camera.position = center + glm::vec3(0.0f, 0.0f, dist);
    camera.view =
        glm::lookAt(camera.position, center, glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::perspective(
        kFovY,
        static_cast<float>(kWindowWidth) / static_cast<float>(kWindowHeight),
        0.1f, 2.0f * (dist + radius));
    return camera;
}

/// Build a horizontal clip plane at the mesh's vertical midpoint: normal +Y
/// through `y = 0.5*(min.y + max.y)`. The kept side is `y >= midpoint`, so the
/// teapot is cut open around its equator (FR-render.4: every emitted
/// cross-section vertex lies on the plane).
re::render::ClipPlane makeMidplane(const re::data::Mesh& mesh) {
    const re::data::Aabb& b = mesh.bounds();
    re::render::ClipPlane plane;
    plane.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    plane.point = glm::vec3(0.0f, 0.5f * (b.min.y + b.max.y), 0.0f);
    return plane;
}

/// The slice sample: owns the loaded teapot + material + clip plane and renders
/// one clipped frame.
class SliceSample final : public re::app::ISample {
   public:
    explicit SliceSample(re::data::Mesh mesh)
        : mesh_(std::move(mesh)),
          material_(glm::vec4(0.25f, 0.55f, 0.85f, 1.0f)),
          plane_(makeMidplane(mesh_)),
          camera_(makeFramingCamera(mesh_)) {
        scene_.meshes.push_back(
            re::render::MeshInstance{&mesh_, &material_, glm::mat4(1.0f)});
    }

    re::data::Result<void> renderFrame(int width, int height) override {
        re::render::RenderTarget target;
        target.framebuffer = nullptr; // the window's default framebuffer (T12)
        target.width = static_cast<unsigned>(width);
        target.height = static_cast<unsigned>(height);
        target.clearColor = glm::vec4(0.10f, 0.10f, 0.12f, 1.0f);
        return renderer_.render(scene_, camera_, plane_, target);
    }

    const char* title() const override {
        return "Slice sample: teapot clipped by a horizontal plane";
    }

    const char* instructions() const noexcept override {
        return "Capability: mesh slice rendering / plane clip (SPEC "
               "FR-render.4).\n"
               "The teapot is clipped by a horizontal plane at its vertical "
               "midpoint; the geometry shader keeps the upper half and emits "
               "the on-plane cross-section (slicing is geometry, not "
               "compositing, SPEC §3).\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    re::data::Mesh mesh_;
    re::render::PhongMaterial material_;
    re::render::ClipPlane plane_;
    re::render::Camera camera_;
    re::render::SliceScene scene_;
    re::render::SliceRenderer renderer_;
};

} // namespace

int main() {
    const std::string meshPath =
        std::string(RE_SOURCE_DIR) + "/data/meshes/teapot.obj";
    auto meshResult = re::io::loadObjMesh(meshPath);
    if (meshResult.failed()) {
        spdlog::error("slice sample: failed to load '{}': {}", meshPath,
                      meshResult.error().message);
        return 1;
    }

    auto windowResult = re::core::Window::create(
        kWindowWidth, kWindowHeight, "RenderEngine - Slice Sample");
    if (windowResult.failed()) {
        spdlog::error("slice sample: {}", windowResult.error().message);
        return 1;
    }

    auto sample = std::make_unique<SliceSample>(std::move(*meshResult));
    re::app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.run(re::app::sampleMaxFrames(kDefaultFrames));
}
