// app/plane_sample.cpp — plane-capability sample: GPU-extracted volume planes
// (FR-app.1 smoke; extends FR-render.5 with the extraction path; T20 routed
// through the broker façade).
//
// In this engine a "plane" semantically means a slice extracted from volume
// data, so this sample demonstrates exactly that: it loads the committed CT
// dataset (data/volumes/sample_ct.nrrd via io::loadNrrdVolume) and shows two
// GPU-EXTRACTED planes as VolumeSliceObject scene values translated through
// broker::VolumeSliceObjectMapper into render::VolumeSliceRenderer layers —
// no gradient quad, no mesh, no CPU voxel loop anywhere in the file:
//
//   - left view  (axial):    the constant-Z plane through the middle voxel
//     layer, expressed as a Space::VoxelIndex PlaneDesc on the VIEW (the
//     broker contextual rule: the view owns the plane) and lifted to world by
//     PlaneMapper's voxel-center convention (index + 0.5) against the object's
//     display-frame model;
//   - right view (oblique):  the same dataset cut by the diagonal plane
//     x + z = 1 through the cube center (a World-space PlaneDesc), viewed
//     orthographically along its normal — demonstrating that the extraction
//     is fully general.
//
// Both views are scene::View values in one broker::AppContext composition
// root; each frame drives the IViewBridge façade (sync → renderAll →
// presentAll): each ReView renders its extraction layer into its own FBO and
// presents it with core::blit. A slice index enters only through uniforms (the
// clip-plane point / model matrix), which is what makes slice scrolling cheap
// — the MPR sample drives exactly this path interactively.
//
// Bounded, clean exit contract: after RE_SAMPLE_MAX_FRAMES frames (default
// 300) or a window close, the harness stops the loop and returns exit code 0
// (FR-app.1).
//
// Live window size (T23): both views' rects are re-derived from the harness
// pixel dims EVERY frame (left = left half, right = right half of the CURRENT
// size), so a window resize re-splits the layout instead of stretching stale
// rects; the extraction cameras keep their dataset-extent ortho windows (the
// MPR display convention), so the extracted planes rescale with the view.
//
// (Capability acceptance: FR-app.1.)

#include <spdlog/spdlog.h>

#include <array>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <utility>

#include "app/ct_transfer_function.hpp"
#include "app/mpr_slice.hpp"
#include "app/sample_harness.hpp"
#include "broker/app_context.hpp"
#include "broker/slice_display.hpp"
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

// Window size via app::kWindowWidth etc. (AS2); view halves derive from live
// dims (AS2). Frame count via app::kDefaultFrames; CT ramp via the shared
// factory in app/ct_transfer_function.hpp (AS1 single definition).

/// The plane sample: owns the CT dataset + transfer function + AppContext and
/// shows two GPU-extracted planes through two bridged ReViews.
class PlaneSample final : public app::ISample {
   public:
    PlaneSample(data::VolumeDataset dataset, volume::TransferFunction tf)
        : dataset_(std::make_shared<const data::VolumeDataset>(std::move(dataset))),
          tf_(std::move(tf)),
          ctx_(broker::AppContext::Params{}) {
        // --- Left view: the axial (constant-Z) extraction -----------------
        // Shared display scaffolding: the slice object's transform maps the
        // dataset's unit cube into the Transverse display frame (voxel-center
        // index i -> display coordinate i + 0.5 on the free axes). The VIEW
        // carries the extraction plane in VOXEL-INDEX space (constant Z,
        // middle voxel layer); PlaneMapper lifts it to display z =
        // index + 0.5 through the object's own transform — the same world
        // plane the previous direct-render composition baked in by hand.
        const auto [freeW, freeH] =
            app::sliceFreeAxes(*dataset_, app::MprAxis::Transverse);

        scene::VolumeSliceObject axial;
        axial.volume = dataset_;
        axial.transferFunction = tf_;
        axial.transform =
            app::sliceVolumeModel(*dataset_, app::MprAxis::Transverse);
        const uint64_t axialId = ctx_.store().addVolumeSliceObject(std::move(axial));

        views_[0].id = 1;
        views_[0].rect = scene::Rect{0, 0, app::kWindowWidth / 2, app::kWindowHeight};
        views_[0].camera =
            broker::makeSliceCamera(static_cast<float>(freeW),
                                    static_cast<float>(freeH));
        views_[0].setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        views_[0].setItemIds({axialId});

        scene::PlaneDesc axialPlane;
        axialPlane.setNormal(glm::vec3(0.0f, 0.0f, 1.0f));
        axialPlane.setPoint(glm::vec3(0.0f, 0.0f, static_cast<float>(dataset_->sizeZ() / 2u)));
        axialPlane.setSpace(scene::Space::VoxelIndex);
        views_[0].setPlane(axialPlane);

        // --- Right view: the oblique extraction ---------------------------
        // Identity transform leaves the dataset's unit cube at [0,1]^3 in
        // world space; the extraction plane is the cube's diagonal plane
        // x + z = 1 through the center (normal normalize(1,0,1)), already
        // World-space so it passes through the mapper unchanged.
        scene::VolumeSliceObject oblique;
        oblique.volume = dataset_;
        oblique.transferFunction = tf_;
        oblique.transform = glm::mat4(1.0f);
        const uint64_t obliqueId =
            ctx_.store().addVolumeSliceObject(std::move(oblique));

        const glm::vec3 obliqueNormal = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));
        const glm::vec3 obliquePoint(0.5f, 0.5f, 0.5f);

        views_[1].id = 2;
        views_[1].rect = scene::Rect{app::kWindowWidth / 2, 0, app::kWindowWidth / 2, app::kWindowHeight};
        // Camera looking straight down the oblique plane's normal at the cube
        // center: eye on +n at distance 3, square ortho window ±0.75 (the
        // diagonal cross-section rectangle of the unit cube is √2 × 1, so
        // half-extents of 0.75 cover it with margin).
        views_[1].camera = scene::Camera(obliquePoint + obliqueNormal * 3.0f,
                                         obliquePoint,
                                         glm::vec3(0.0f, 1.0f, 0.0f));
        views_[1].camera.setOrtho(-0.75f, 0.75f, -0.75f, 0.75f, 0.1f, 10.0f);
        views_[1].setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        views_[1].setItemIds({obliqueId});

        scene::PlaneDesc obliquePlane;
        obliquePlane.setNormal(obliqueNormal);
        obliquePlane.setPoint(obliquePoint);
        obliquePlane.setSpace(scene::Space::World);
        views_[1].setPlane(obliquePlane);
    }

    /// The resize hook: apply the new pixel dims immediately so the very next
    /// frame (and its sync) already carries the corrected view rects.
    void onResize(int width, int height) noexcept override {
        applyLiveRects(width, height);
    }

    data::Result<void> renderFrame(int width, int height) override {
        // Clear the window behind the two-view split (a background, not a
        // viewport blend — the views are placed by the engine blit; stashed
        // until presentAll compositing lands per AS7).
        core::bindDefaultFramebuffer();
        core::setViewport(0, 0, width, height);
        core::setClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        core::clearColor();

        // Live dims first, then the single-site bridge façade (AS2).
        applyLiveRects(width, height);
        frame_.assign(views_.begin(), views_.end());
        return app::syncRenderPresent(ctx_, frame_);
    }

    const char* title() const override {
        return "Plane sample: GPU-extracted CT planes (axial + oblique)";
    }

    const char* instructions() const noexcept override {
        return "Capability: volume-plane extraction on the GPU.\n"
               "Left: the axial plane (constant Z, middle voxel layer) "
               "extracted from data/volumes/sample_ct.nrrd by sampling the "
               "cached 3D texture where each pixel ray crosses the plane.\n"
               "Right: the oblique diagonal plane x + z = 1 through the same "
               "volume - extraction is fully general, no CPU slicing.\n"
               "Both layers are mediated by broker::VolumeSliceObjectMapper "
               "(the view owns the plane).\n"
               "Resize check: drag a window edge — the two-view split "
               "re-resolves to the live pixel size (left/right halves of the "
               "current window), no stale rects.\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    /// Re-derive both view rects from live pixel dims: left view = the left
    /// half, right view = the right half of the CURRENT window (odd widths
    /// give the extra pixel to the right view via width - width/2) — the one
    /// body shared by the resize hook and every rendered frame.
    void applyLiveRects(int width, int height) noexcept {
        const int leftWidth = width / 2;
        views_[0].setRect(scene::Rect{0, 0, leftWidth, height});
        views_[1].setRect(scene::Rect{leftWidth, 0, width - leftWidth, height});
    }

    std::shared_ptr<const data::VolumeDataset> dataset_;
    volume::TransferFunction tf_; // immutable value; copied into instances
    broker::AppContext ctx_;
    std::array<scene::View, 2> views_{};
    std::vector<scene::View> frame_{};
};

} // namespace

int main() {
    // Single-site entry via app::runSample (AS2: load→window→run dedup).
    return app::runSample(
        "RenderEngine - Plane Sample", app::kWindowWidth, app::kWindowHeight,
        app::kDefaultFrames, []() -> std::unique_ptr<app::ISample> {
            const std::string volumePath =
                std::string(RE_SOURCE_DIR) + "/data/volumes/sample_ct.nrrd";
            auto volumeResult = re::io::loadNrrdVolume(volumePath);
            if (volumeResult.failed()) {
                spdlog::error("plane sample: failed to load '{}': {}",
                              volumePath, volumeResult.error().message);
                return nullptr;
            }
            return std::make_unique<PlaneSample>(std::move(*volumeResult),
                                                 app::RE_CT_TF());
        });
}
