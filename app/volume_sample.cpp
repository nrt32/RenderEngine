// app/volume_sample.cpp — volume rendering sample (T12, FR-app.1).
//
// Demonstrates the volume capability: loads the downsampled CT sample
// (data/volumes/sample_ct.nrrd, SPEC §7), maps its scalar values to RGBA with a
// CT window/level transfer function, and drives it through the shared
// app::SampleHarness (visible window + ImGui overlay + run loop) via
// render::VolumeRenderer into the window's default framebuffer (null
// RenderTarget::framebuffer binds the default, T12).
//
// The sample exits cleanly (code 0) after RE_SAMPLE_MAX_FRAMES frames (default
// 300), so the gate can run it headlessly under Xvfb within a timeout
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
#include <vector>

#include "app/sample_harness.hpp"
#include "core/window.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "io/volume/nrrd_volume_loader.hpp"
#include "render/volume_renderer.hpp"
#include "volume/transfer_function.hpp"

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

/// A CT window/level transfer function over the sample_ct value range
/// ([-3024, 2529], SPEC §7): air (low) transparent, soft tissue opaque/bright.
/// Deterministic control points (FR-vol.1); monotonic alpha ramp.
re::volume::TransferFunction makeCtTransferFunction() {
    using CP = re::volume::TransferFunction::ControlPoint;
    return re::volume::TransferFunction(
        {CP{-1024.0f, re::volume::RgbaColor{0.0f, 0.0f, 0.0f, 0.0f}},
         CP{-300.0f, re::volume::RgbaColor{0.05f, 0.05f, 0.10f, 0.05f}},
         CP{40.0f, re::volume::RgbaColor{0.90f, 0.50f, 0.20f, 0.90f}},
         CP{300.0f, re::volume::RgbaColor{0.90f, 0.50f, 0.20f, 1.00f}},
         CP{2500.0f, re::volume::RgbaColor{1.00f, 1.00f, 1.00f, 1.00f}}});
}

/// The volume sample: owns the CT dataset + transfer function and renders one
/// frame.
class VolumeSample final : public re::app::ISample {
   public:
    VolumeSample(re::data::VolumeDataset dataset,
                 re::volume::TransferFunction tf)
        : dataset_(std::make_shared<re::data::VolumeDataset>(std::move(dataset))),
          tf_(std::move(tf)) {
        // Ownership split: the voxel bytes go in as a SHARED reference
        // (co-owned by scene object and sample, so neither can dangle the
        // other), while the transfer function is copied BY VALUE — it is a
        // tiny immutable ramp and copying removes the last pointer from this
        // path.
        scene_.volumes.push_back(re::render::VolumeInstance{
            dataset_, tf_, glm::mat4(1.0f)});

        const glm::vec3 center(0.5f, 0.5f, 0.5f);
        camera_.position = glm::vec3(0.5f, 0.5f, 3.0f);
        camera_.view =
            glm::lookAt(camera_.position, center, glm::vec3(0.0f, 1.0f, 0.0f));
        camera_.proj = glm::perspective(kFovY,
                                        static_cast<float>(kWindowWidth) /
                                            static_cast<float>(kWindowHeight),
                                        0.1f, 10.0f);
    }

    re::data::Result<void> renderFrame(int width, int height) override {
        re::render::RenderTarget target;
        target.framebuffer = nullptr;
        // null framebuffer = render straight into the window's on-screen
        // default framebuffer (samples have no offscreen ViewTarget; the
        // harness hands us its pixel size each frame).
        target.width = static_cast<unsigned>(width);
        target.height = static_cast<unsigned>(height);
        target.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        return renderer_.render(scene_, camera_, target);
    }

    const char* title() const override {
        return "Volume sample: ray-cast CT chest (128x128x70)";
    }

    const char* instructions() const noexcept override {
        return "Capability: basic ray-cast volume rendering (SPEC FR-render.6).\n"
               "The CT chest is sampled along each view ray and composited "
               "front-to-back through render::VolumeRenderer with a CT "
               "window/level transfer function.\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    std::shared_ptr<re::data::VolumeDataset> dataset_;
    re::volume::TransferFunction tf_;
    re::render::VolumeScene scene_;
    re::render::Camera camera_;
    // The shared GPU asset store (SPEC §7 T14): one GPU 3D texture per
    // distinct dataset content, co-owned by every renderer that resolves
    // through it. Declared before its renderer and injected as a shared_ptr
    // copy, so member-init order can never dangle it (T13).
    std::shared_ptr<re::render::AssetRegistry> assets_{
        std::make_shared<re::render::AssetRegistry>()};
    re::render::VolumeRenderer renderer_{assets_};
};

} // namespace

int main() {
    const std::string volumePath =
        std::string(RE_SOURCE_DIR) + "/data/volumes/sample_ct.nrrd";
    auto volumeResult = re::io::loadNrrdVolume(volumePath);
    if (volumeResult.failed()) {
        spdlog::error("volume sample: failed to load '{}': {}", volumePath,
                      volumeResult.error().message);
        return 1;
    }

    auto windowResult = re::core::Window::create(
        kWindowWidth, kWindowHeight, "RenderEngine - Volume Sample");
    if (windowResult.failed()) {
        spdlog::error("volume sample: {}", windowResult.error().message);
        return 1;
    }

    auto sample = std::make_unique<VolumeSample>(std::move(*volumeResult),
                                                 makeCtTransferFunction());
    re::app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.run(re::app::sampleMaxFrames(kDefaultFrames));
}
