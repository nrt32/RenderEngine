// app/plane_long.cpp — long-lived plane sample: the interactive peer of the bounded two-view GPU-extracted
// CT plane sample that bypasses RE_SAMPLE_MAX_FRAMES and drives SampleHarness::runInteractive()
// until shouldClose() while its renderFrame calls GlfwCameraInteractor::update per view before
// syncRenderPresent with the same left-drag rotate dx*0.5deg / right-drag pan dx*0.01 / zoom
// exp(-dy*0.02) mapping, but the interactor's orthographic/PlaneDesc guard vetoes any mutation for
// the two slice views that carry a PlaneDesc so those orthographic displays keep their fixed dataset-extent
// framing and only View::setCamera on non-orthographic views bumps viewGen — T10.

#include <spdlog/spdlog.h>

#include <array>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <utility>

#include "app/ct_transfer_function.hpp"
#include "app/glfw_camera_interactor.hpp"
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

class PlaneLongSample final : public app::ISample {
   public:
    PlaneLongSample(data::VolumeDataset dataset, volume::TransferFunction tf)
        : dataset_(std::make_shared<const data::VolumeDataset>(std::move(dataset))),
          tf_(std::move(tf)),
          ctx_(broker::AppContext::Params{}) {
        const auto [freeW, freeH] = app::sliceFreeAxes(*dataset_, app::MprAxis::Transverse);

        scene::VolumeSliceObject axial;
        axial.volume = dataset_;
        axial.transferFunction = tf_;
        axial.transform = app::sliceVolumeModel(*dataset_, app::MprAxis::Transverse);
        const uint64_t axialId = ctx_.store().addVolumeSliceObject(std::move(axial));

        views_[0].id = 1;
        views_[0].rect = scene::Rect{0, 0, app::kWindowWidth / 2, app::kWindowHeight};
        views_[0].camera = broker::makeSliceCamera(static_cast<float>(freeW), static_cast<float>(freeH));
        views_[0].setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        views_[0].setItemIds({axialId});
        scene::PlaneDesc axialPlane;
        axialPlane.setNormal(glm::vec3(0.0f, 0.0f, 1.0f));
        axialPlane.setPoint(glm::vec3(0.0f, 0.0f, static_cast<float>(dataset_->sizeZ() / 2u)));
        axialPlane.setSpace(scene::Space::VoxelIndex);
        views_[0].setPlane(axialPlane);

        scene::VolumeSliceObject oblique;
        oblique.volume = dataset_;
        oblique.transferFunction = tf_;
        oblique.transform = glm::mat4(1.0f);
        const uint64_t obliqueId = ctx_.store().addVolumeSliceObject(std::move(oblique));
        const glm::vec3 obliqueNormal = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));
        const glm::vec3 obliquePoint(0.5f, 0.5f, 0.5f);
        views_[1].id = 2;
        views_[1].rect = scene::Rect{app::kWindowWidth / 2, 0, app::kWindowWidth / 2, app::kWindowHeight};
        views_[1].camera = scene::Camera(obliquePoint + obliqueNormal * 3.0f, obliquePoint, glm::vec3(0.0f, 1.0f, 0.0f));
        views_[1].camera.setOrtho(-0.75f, 0.75f, -0.75f, 0.75f, 0.1f, 10.0f);
        views_[1].setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        views_[1].setItemIds({obliqueId});
        scene::PlaneDesc obliquePlane;
        obliquePlane.setNormal(obliqueNormal);
        obliquePlane.setPoint(obliquePoint);
        obliquePlane.setSpace(scene::Space::World);
        views_[1].setPlane(obliquePlane);
    }

    void onResize(int width, int height) noexcept override {
        applyLiveRects(width, height);
    }

    data::Result<void> renderFrame(int width, int height) override {
        // T10 plane guard ordering — the long-lived plane peer iterates both views and calls
        // GlfwCameraInteractor::update on each before the broker sync; the interactor's own
        // WantCaptureMouse and orthographic/PlaneDesc guard leaves the two slice views untouched
        // so their dataset-extent orthographic framing stays fixed while still respecting the
        // overlay capture flag and only dirty camera views bump viewGen via View::setCamera.
        for (auto& v : views_) {
            interactor_.update(v);
        }
        applyLiveRects(width, height);
        frame_.assign(views_.begin(), views_.end());
        return app::syncRenderPresent(ctx_, frame_);
    }

    const char* /*borrow*/ title() const override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "Plane long-lived: GPU-extracted CT planes interactive (plane guard skip)";
    }

    const char* /*borrow*/ instructions() const noexcept override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "Long-lived plane — runInteractive until close, interactor per view with WantCaptureMouse + orthographic plane skip, view.setCamera bump.";
    }

   private:
    void applyLiveRects(int width, int height) noexcept {
        const int leftWidth = width / 2;
        views_[0].setRect(scene::Rect{0, 0, leftWidth, height});
        views_[1].setRect(scene::Rect{leftWidth, 0, width - leftWidth, height});
    }

    std::shared_ptr<const data::VolumeDataset> dataset_;
    volume::TransferFunction tf_;
    broker::AppContext ctx_;
    std::array<scene::View, 2> views_{};
    std::vector<scene::View> frame_{};
    scene::CameraController controller_{};
    app::GlfwCameraInteractor interactor_{controller_};
};

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--help" || a == "-h") {
            spdlog::info("re_sample_plane_long — long-lived plane sample (T10) plane guard ensures no rotate on slice views");
            return 0;
        }
    }
    auto windowResult = core::Window::create(app::kWindowWidth, app::kWindowHeight, "Plane Long - Interactive");
    if (windowResult.failed()) {
        spdlog::error("window: {}", windowResult.error().message);
        return 1;
    }
    const std::string volumePath = std::string(RE_SOURCE_DIR) + "/data/volumes/sample_ct.nrrd";
    auto volumeResult = re::io::loadNrrdVolume(volumePath);
    if (volumeResult.failed()) {
        spdlog::error("plane long: failed to load '{}': {}", volumePath, volumeResult.error().message);
        return 1;
    }
    auto sample = std::make_unique<PlaneLongSample>(std::move(*volumeResult), app::RE_CT_TF());
    app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.runInteractive();
}
