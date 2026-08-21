// app/plane_sample.cpp — plane rendering sample (T12, FR-app.1).
//
// Demonstrates the textured-plane capability: builds a deterministic procedural
// RGBA gradient image in code (no asset dependency), textures the unit XY quad
// with it, and drives it through the shared app::SampleHarness (visible window
// + ImGui overlay + run loop) via render::PlaneRenderer into the window's
// default framebuffer (null RenderTarget::framebuffer binds the default, T12).
//
// The sample exits cleanly (code 0) after RE_SAMPLE_MAX_FRAMES frames (default
// 300), so the gate can run it headlessly under Xvfb within a timeout
// (FR-app.1: exit code 0, no sanitizer reports).

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
#include "data/image.hpp"
#include "data/result.hpp"
#include "render/plane_renderer.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

// The harness window size.
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
// Default number of frames before the sample exits cleanly.
constexpr int kDefaultFrames = 300;
// The procedural texture size (square).
constexpr int kTexSize = 256;
// Perspective vertical field of view in radians (~60 deg).
constexpr float kFovY = 1.0471975511965976f;

/// Build a deterministic RGBA gradient image: R = x, G = y, B = (x+y)/2,
/// A = 255. Closed-form, so the sample is reproducible with no asset file.
re::data::Image makeGradientImage() {
    std::vector<std::uint8_t> pixels;
    pixels.reserve(static_cast<std::size_t>(kTexSize) * kTexSize * 4u);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            pixels.push_back(static_cast<std::uint8_t>(x));           // R
            pixels.push_back(static_cast<std::uint8_t>(y));           // G
            pixels.push_back(static_cast<std::uint8_t>((x + y) / 2)); // B
            pixels.push_back(255u);                                   // A
        }
    }
    return re::data::Image(kTexSize, kTexSize, 4, std::move(pixels));
}

/// The plane sample: owns the procedural image + quad and renders one frame.
class PlaneSample final : public re::app::ISample {
   public:
    PlaneSample() : image_(makeGradientImage()) {
        geometry_ = re::render::PlaneGeometry::unitQuadXY();
        scene_.planes.push_back(
            re::render::PlaneInstance{&geometry_, &image_, glm::mat4(1.0f)});

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
        return "Plane sample: textured gradient quad";
    }

    const char* instructions() const noexcept override {
        return "Capability: textured plane (SPEC FR-render.5).\n"
               "A unit XY quad textured with a closed-form RGBA gradient is "
               "drawn through render::PlaneRenderer (feeds the MPR views).\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    re::render::PlaneGeometry geometry_;
    re::data::Image image_;
    re::render::PlaneScene scene_;
    re::render::Camera camera_;
    re::render::PlaneRenderer renderer_;
};

} // namespace

int main() {
    auto windowResult = re::core::Window::create(kWindowWidth, kWindowHeight,
                                                 "RenderEngine - Plane Sample");
    if (windowResult.failed()) {
        spdlog::error("plane sample: {}", windowResult.error().message);
        return 1;
    }

    auto sample = std::make_unique<PlaneSample>();
    re::app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.run(re::app::sampleMaxFrames(kDefaultFrames));
}
