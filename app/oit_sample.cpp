// app/oit_sample.cpp — order-independent transparency (OIT) sample (T13,
// FR-app.1).
//
// Demonstrates the transparency / OIT capability (SPEC §1 capability 5,
// FR-render.2/3): three overlapping transparent +Z-facing quads at different
// depths (each a golden data::Mesh built in code — no asset dependency) are
// rendered through render::MeshRenderer with an injected render::LinkedListOIT
// pipeline into the window's default framebuffer (null
// RenderTarget::framebuffer binds the default, T12). The pipeline captures each
// quad's fragments, depth-sorts them per pixel, and composites them back-to-
// front, so the final color is order-independent (the correct depth-ordered
// blend regardless of draw order) — the transparency capability.
//
// Because the quads are transparent, MeshRenderer auto-engages the injected
// pipeline (FR-render.3); an opaque-only scene would never engage it. The three
// quads overlap in the viewport, so the center region visibly shows all three
// blended in depth order.
//
// The sample is driven through the shared app::SampleHarness (visible window +
// ImGui overlay + run loop) exactly like the T12 samples, and exits cleanly
// (code 0) after RE_SAMPLE_MAX_FRAMES frames (default 300), so the gate can run
// it headlessly under Xvfb within a timeout (FR-app.1: exit code 0, no
// sanitizer reports).

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/sample_harness.hpp"
#include "core/window.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "render/linked_list_oit.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

// The harness window size.
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
// Default number of frames before the sample exits cleanly.
constexpr int kDefaultFrames = 300;
// Perspective vertical field of view in radians (~60 deg).
constexpr float kFovY = 1.0471975511965976f;

/// Build a golden +Z-facing quad mesh covering [-1,1]^2 at z=0 (two triangles).
re::data::Mesh makeQuadMesh() {
    std::vector<glm::vec3> positions = {
        glm::vec3(-1.0f, -1.0f, 0.0f), // v0
        glm::vec3(1.0f, -1.0f, 0.0f),  // v1
        glm::vec3(1.0f, 1.0f, 0.0f),   // v2
        glm::vec3(-1.0f, 1.0f, 0.0f),  // v3
    };
    std::vector<std::uint32_t> indices = {0u, 1u, 2u, 0u, 2u, 3u};
    return re::data::Mesh::fromTriangles(std::move(positions), std::move(indices));
}

/// The OIT sample: owns the three transparent quads + materials + the injected
/// LinkedListOIT pipeline and renders one frame.
class OitSample final : public re::app::ISample {
   public:
    OitSample()
        : near_(glm::vec4(0.85f, 0.20f, 0.20f, 0.55f)),   // red, closest
          middle_(glm::vec4(0.20f, 0.85f, 0.20f, 0.55f)), // green
          far_(glm::vec4(0.20f, 0.35f, 0.90f, 0.55f)),    // blue, farthest
          quad_(makeQuadMesh()) {
        // Three transparent quads stacked along -Z from the camera: near at
        // z=0.5, middle at z=0.0, far at z=-0.5. Each is a MeshInstance with a
        // transparent PhongMaterial (alpha 0.55 => isTransparent(), FR-render.3),
        // so MeshRenderer auto-engages the injected LinkedListOIT pipeline.
        scene_.meshes.push_back(re::render::MeshInstance{
            &quad_, &near_, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.5f))});
        scene_.meshes.push_back(re::render::MeshInstance{
            &quad_, &middle_, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f))});
        scene_.meshes.push_back(re::render::MeshInstance{
            &quad_, &far_, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -0.5f))});

        camera_.position = glm::vec3(0.0f, 0.0f, 3.0f);
        camera_.view =
            glm::lookAt(camera_.position, glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f));
        camera_.proj = glm::perspective(kFovY,
                                        static_cast<float>(kWindowWidth) /
                                            static_cast<float>(kWindowHeight),
                                        0.1f, 10.0f);
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
        return "OIT sample: three overlapping transparent quads (linked-list)";
    }

    const char* instructions() const noexcept override {
        return "Capability: order-independent transparency (SPEC FR-render.2/3).\n"
               "Three overlapping transparent quads (red near, green middle, "
               "blue far) are captured, depth-sorted, and composited by "
               "render::LinkedListOIT through render::MeshRenderer — the blend "
               "is correct regardless of draw order.\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    re::render::PhongMaterial near_;
    re::render::PhongMaterial middle_;
    re::render::PhongMaterial far_;
    re::data::Mesh quad_;
    re::render::MeshScene scene_;
    re::render::Camera camera_;
    re::render::LinkedListOIT pipeline_;
    re::render::MeshRenderer renderer_{&pipeline_};
};

} // namespace

int main() {
    auto windowResult = re::core::Window::create(
        kWindowWidth, kWindowHeight, "RenderEngine - OIT Sample");
    if (windowResult.failed()) {
        spdlog::error("oit sample: {}", windowResult.error().message);
        return 1;
    }

    auto sample = std::make_unique<OitSample>();
    re::app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.run(re::app::sampleMaxFrames(kDefaultFrames));
}
