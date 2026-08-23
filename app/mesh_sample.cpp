// app/mesh_sample.cpp — mesh rendering sample (T12, FR-app.1).
//
// Demonstrates the mesh capability: loads the Stanford bunny (data/meshes,
// SPEC §7), shades it with an opaque Phong material, and drives it through the
// shared app::SampleHarness (visible window + ImGui overlay + run loop). The
// scene renders into the window's default framebuffer via render::MeshRenderer
// (a null RenderTarget::framebuffer binds the default framebuffer, T12).
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
#include "core/window.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "io/mesh/obj_mesh_loader.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"

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

/// The mesh sample: owns the loaded bunny + material and renders one frame.
class MeshSample final : public re::app::ISample {
   public:
    explicit MeshSample(re::data::Mesh mesh)
        : mesh_(std::move(mesh)),
          material_(std::make_shared<re::render::PhongMaterial>(
              glm::vec4(0.85f, 0.45f, 0.15f, 1.0f))),
          camera_(makeFramingCamera(mesh_)) {
        // Register the mesh once with the shared registry (SPEC §9 V2.5): the
        // scene carries its AssetHandle, resolved by the renderer. The window
        // (and thus a GL context) exists before the sample is constructed, so
        // the upload succeeds; on the impossible failure the scene stays empty
        // and the sample degrades gracefully (logged, never silent).
        const auto handle = registry_->registerAsset(mesh_);
        if (handle.failed()) {
            spdlog::error("mesh sample: failed to register mesh: {}",
                          handle.error().message);
            return;
        }
        scene_.meshes.push_back(
            re::render::MeshInstance{*handle, material_, glm::mat4(1.0f)});
    }

    re::data::Result<void> renderFrame(int width, int height) override {
        re::render::RenderTarget target;
        target.framebuffer = nullptr; // the window's default framebuffer (T12)
        target.width = static_cast<unsigned>(width);
        target.height = static_cast<unsigned>(height);
        target.clearColor = glm::vec4(0.10f, 0.10f, 0.12f, 1.0f);
        return renderer_.render(scene_, camera_, target);
    }

    const char* title() const override {
        return "Mesh sample: Stanford bunny (Phong opaque)";
    }

    const char* instructions() const noexcept override {
        return "Capability: shaded triangle mesh (SPEC FR-render.1).\n"
               "A Phong (opaque) mesh is drawn through render::MeshRenderer.\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    re::data::Mesh mesh_;
    std::shared_ptr<re::render::PhongMaterial> material_;
    re::render::Camera camera_;
    // Shared asset registry (SPEC §9 V2.5, T13): self-initializing NSDMI, so
    // the renderer group below has NO declaration-order hazard — it holds a
    // shared reference and validates it per draw (a null registry would fail
    // with typed error code 4, never crash).
    std::shared_ptr<re::render::AssetRegistry> registry_{
        std::make_shared<re::render::AssetRegistry>()};
    re::render::MeshScene scene_;
    re::render::MeshRenderer renderer_{registry_};
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

    auto windowResult = re::core::Window::create(kWindowWidth, kWindowHeight,
                                                 "RenderEngine - Mesh Sample");
    if (windowResult.failed()) {
        spdlog::error("mesh sample: {}", windowResult.error().message);
        return 1;
    }

    auto sample = std::make_unique<MeshSample>(std::move(*meshResult));
    re::app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.run(re::app::sampleMaxFrames(kDefaultFrames));
}
