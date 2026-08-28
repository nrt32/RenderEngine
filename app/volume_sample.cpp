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
// Live window size (T23, T7): the view's rect and the camera's projection aspect
// are re-derived from the harness pixel dims EVERY frame via
// the SceneViewBuilder live-dims helper (scene/builders.hpp) — the compile-time
// kWindowWidth/kWindowHeight constants only pick the OPENING window size, never
// feed projections — so resizing the window reframes the ray-cast instead of
// stretching it (T7 single helper, not six private duplicates).

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/ct_transfer_function.hpp"
#include "app/glfw_camera_interactor.hpp"
#include "app/sample_harness.hpp"
#include "broker/app_context.hpp"
#include "core/window.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "io/volume/nrrd_volume_loader.hpp"
#include "scene/builders.hpp"
#include "scene/camera_controller.hpp"
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

// Window size, FOV and frame count are shared via app::kWindowWidth etc.
// (AS2 constants dedup).

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
        // T7 V5: framing via builder (no private duplicate) — the builder stores the framing type value (fov/near/far) and its syncLive(w,h) one call does rect := {0,0,w,h} plus camera.setPerspectiveFromFraming at aspect w/h, which is the single helper that replaces the six hand-copied private methods previously duplicating the live-aspect rule in each sample; this keeps per-frame projection derivation cheap and change-guarded (T7).
        scene::SceneViewBuilder bld(1, scene::Rect{0, 0, app::kWindowWidth, app::kWindowHeight}, {app::kDefaultFovYDeg, 0.1f, 10.0f});
        bld.withCamera(scene::Camera(glm::vec3(0.5f, 0.5f, 3.0f), center, glm::vec3(0.0f, 1.0f, 0.0f)));
        view_ = bld.build();
        bld.syncLive(app::kWindowWidth, app::kWindowHeight);
        view_ = bld.view();
        view_.setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        view_.setItemIds({volId});
        builder_ = std::move(bld);
    }

    /// The resize hook: one builder call (T7 V5) — the builder stores the framing type (fov/near/far) and its syncLive(w,h) does rect := {0,0,w,h} plus camera.setPerspectiveFromFraming at aspect w/h, which is the single helper that replaces the six private duplicates; this hook therefore forwards the live harness pixel size through the builder so the projection stays derived from the current size without re-deriving framing distance (T7).
    void onResize(int width, int height) noexcept override {
        builder_.syncLive(width, height);
        view_ = builder_.view();
    }

    data::Result<void> renderFrame(int width, int height) override {
        interactor_.update(builder_.view());
        builder_.syncLive(width, height);
        view_ = builder_.view();
        views_ = {view_};
        return app::syncRenderPresent(ctx_, views_);
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
               "Controls: left-drag orbits, right-drag pans, middle/wheel "
               "zooms via scene::CameraController with WantCaptureMouse guard; "
               "View::mutateCamera bumps viewGen so broker re-translates only "
               "dirty camera fields.\n"
               "Resize check: drag a window edge — the view reframes to the "
               "live pixel size (camera aspect follows width/height), no "
               "stretching.\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    std::shared_ptr<const data::VolumeDataset> dataset_;
    volume::TransferFunction tf_;
    broker::AppContext ctx_;
    scene::View view_{};
    scene::SceneViewBuilder builder_{1, scene::Rect{0, 0, 800, 600}};
    std::vector<scene::View> views_{};
    scene::CameraController controller_{};
    app::GlfwCameraInteractor interactor_{controller_};
};

} // namespace

int main() {
    // Single-site entry via app::runSample (AS2).
    return app::runSample(
        "RenderEngine - Volume Sample", app::kWindowWidth, app::kWindowHeight,
        app::kDefaultFrames, []() -> std::unique_ptr<app::ISample> {
            const std::string volumePath =
                std::string(RE_SOURCE_DIR) + "/data/volumes/sample_ct.nrrd";
            auto volumeResult = re::io::loadNrrdVolume(volumePath);
            if (volumeResult.failed()) {
                spdlog::error("volume sample: failed to load '{}': {}",
                              volumePath, volumeResult.error().message);
                return nullptr;
            }
            return std::make_unique<VolumeSample>(
                std::move(*volumeResult), app::RE_CT_TF());
        });
}
