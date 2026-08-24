// app/volume_sample.cpp — volume rendering sample (T12, FR-app.1; T20 routed
// through the broker façade).
//
// Demonstrates the volume capability: loads the downsampled CT sample
// (data/volumes/sample_ct.nrrd, SPEC §7), maps its scalar values to RGBA with a
// CT window/level transfer function, and drives it through the shared
// app::SampleHarness (visible window + ImGui overlay + run loop). The scene is
// expressed ENTIRELY as scene/ values (VolumeObject carrying the dataset ref +
// transfer function) in a broker::AppContext composition root and rendered
// through IViewBridge (sync → renderAll → presentAll): the synchronizer maps
// the object through VolumeObjectMapper into a REAL ray-cast layer drawn by
// render::VolumeRenderer — per the SPEC §11 ACL the sample never includes
// render/ and never holds a mapper or renderer handle.
//
// The sample exits cleanly (code 0) after RE_SAMPLE_MAX_FRAMES frames (default
// 300), so the gate can run it headlessly under Xvfb within a timeout
// (FR-app.1: exit code 0, no sanitizer reports).
//
// Live window size (T23): the view's rect and the camera's projection aspect
// are re-derived from the harness pixel dims EVERY frame via
// app::fitPerspectiveViewToPixels — the compile-time kWindowWidth/kWindowHeight
// constants only pick the OPENING window size, never feed projections — so
// resizing the window reframes the ray-cast instead of stretching it.

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/sample_harness.hpp"
#include "broker/app_context.hpp"
#include "core/window.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "io/volume/nrrd_volume_loader.hpp"
#include "volume/transfer_function.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

namespace app = re::app;
namespace broker = re::broker;
namespace core = re::core;
namespace data = re::data;
namespace scene = re::scene;
namespace volume = re::volume;

// The harness window size: the OPENING size only — every per-frame value
// (view rect, camera aspect) derives from the live framebuffer dims instead.
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
// Default number of frames before the sample exits cleanly.
constexpr int kDefaultFrames = 300;
// Perspective vertical field of view in degrees (~60 deg).
constexpr float kFovYDeg = 60.0f;

/// A CT window/level transfer function over the sample_ct value range
/// ([-3024, 2529], SPEC §7): air (low) transparent, soft tissue opaque/bright.
/// Deterministic control points (FR-vol.1); monotonic alpha ramp.
volume::TransferFunction makeCtTransferFunction() {
    using CP = volume::TransferFunction::ControlPoint;
    return volume::TransferFunction(
        {CP{-1024.0f, volume::RgbaColor{0.0f, 0.0f, 0.0f, 0.0f}},
         CP{-300.0f, volume::RgbaColor{0.05f, 0.05f, 0.10f, 0.05f}},
         CP{40.0f, volume::RgbaColor{0.90f, 0.50f, 0.20f, 0.90f}},
         CP{300.0f, volume::RgbaColor{0.90f, 0.50f, 0.20f, 1.00f}},
         CP{2500.0f, volume::RgbaColor{1.00f, 1.00f, 1.00f, 1.00f}}});
}

/// The volume sample: owns the CT dataset + transfer function + AppContext and
/// renders one bridged frame per renderFrame call.
class VolumeSample final : public app::ISample {
   public:
    VolumeSample(data::VolumeDataset dataset, volume::TransferFunction tf)
        : dataset_(std::make_shared<const data::VolumeDataset>(std::move(dataset))),
          tf_(std::move(tf)),
          ctx_(broker::AppContext::Params{}) {
        // Ownership split: the voxel bytes go in as a SHARED reference
        // (co-owned by scene object and sample, so neither can dangle the
        // other), while the transfer function is copied BY VALUE — a tiny
        // immutable ramp carried on the scene object itself.
        scene::VolumeObject vo;
        vo.volume = dataset_;
        vo.transferFunction = tf_;
        vo.transform = glm::mat4(1.0f);
        const uint64_t volId = ctx_.store().addVolumeObject(std::move(vo));

        const glm::vec3 center(0.5f, 0.5f, 0.5f);
        framing_.fovDeg = kFovYDeg;
        framing_.nearPlane = 0.1f;
        framing_.farPlane = 10.0f;
        view_.id = 1;
        view_.camera =
            scene::Camera(glm::vec3(0.5f, 0.5f, 3.0f), center,
                          glm::vec3(0.0f, 1.0f, 0.0f));
        app::fitPerspectiveViewToPixels(view_, framing_, kWindowWidth,
                                        kWindowHeight);
        view_.setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        view_.setItemIds({volId});
    }

    /// The resize hook: apply the new pixel dims immediately so the very next
    /// frame (and its sync) already carries the corrected rect + aspect.
    void onResize(int width, int height) noexcept override {
        applyLiveDims(width, height);
    }

    data::Result<void> renderFrame(int width, int height) override {
        // Live dims first: rect + camera aspect always derive from THIS
        // frame's framebuffer size (change-guarded setters make a no-resize
        // frame free), then the bridge path: sync → renderAll (ray-cast into
        // the ReView target) → presentAll blits 1:1 to the window's default
        // framebuffer (a null framebuffer destination means the on-screen
        // default framebuffer; the view rect equals the live pixel size).
        applyLiveDims(width, height);
        views_ = {view_};
        auto s = ctx_.bridge().sync(views_, ctx_.store());
        if (s.failed()) {
            return s;
        }
        auto r = ctx_.bridge().renderAll();
        if (r.failed()) {
            return r;
        }
        return ctx_.bridge().presentAll(nullptr);
    }

    const char* title() const override {
        return "Volume sample: ray-cast CT chest (128x128x70)";
    }

    const char* instructions() const noexcept override {
        return "Capability: basic ray-cast volume rendering (SPEC FR-render.6).\n"
               "The CT chest is sampled along each view ray and composited "
               "front-to-back: the scene VolumeObject translates through "
               "broker::VolumeObjectMapper into a real VolumeRenderer layer "
               "driven by the IViewBridge façade.\n"
               "Resize check: drag a window edge — the view reframes to the "
               "live pixel size (camera aspect follows width/height), no "
               "stretching.\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    /// Re-derive the view rect + camera aspect from live pixel dims — the one
    /// body shared by the resize hook and every rendered frame.
    void applyLiveDims(int width, int height) noexcept {
        app::fitPerspectiveViewToPixels(view_, framing_, width, height);
    }

    std::shared_ptr<const data::VolumeDataset> dataset_;
    volume::TransferFunction tf_;
    broker::AppContext ctx_;
    scene::View view_{};
    app::PerspectiveFraming framing_{}; // fov/near/far fixed; aspect is live
    std::vector<scene::View> views_{};
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

    auto windowResult = core::Window::create(
        kWindowWidth, kWindowHeight, "RenderEngine - Volume Sample");
    if (windowResult.failed()) {
        spdlog::error("volume sample: {}", windowResult.error().message);
        return 1;
    }

    auto sample = std::make_unique<VolumeSample>(std::move(*volumeResult),
                                                 makeCtTransferFunction());
    app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.run(app::sampleMaxFrames(kDefaultFrames));
}
