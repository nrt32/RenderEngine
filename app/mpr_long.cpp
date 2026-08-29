// app/mpr_long.cpp — long-lived MPR sample: the interactive peer of the bounded 2x2 MPR grid that
// bypasses RE_SAMPLE_MAX_FRAMES and drives SampleHarness::runInteractive() until shouldClose()
// while its renderFrame calls GlfwCameraInteractor::update only on the 3D perspective view before
// syncRenderPresent with left-drag rotate dx*0.5deg / right-drag pan dx*0.01 / zoom exp(-dy*0.02),
// respecting WantCaptureMouse and skipping the three 2D orthographic slice views that carry a
// PlaneDesc so their fixed dataset-extent framing stays pinned and only the 3D camera's viewGen
// bumps via View::setCamera; EXCLUDE_FROM_ALL so ctest never runs it — T10.

#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

using MprAxis = app::MprAxis;

class MprLongSample final : public app::ISample {
   public:
    MprLongSample(data::VolumeDataset dataset, volume::TransferFunction tf)
        : dataset_(std::make_shared<data::VolumeDataset>(std::move(dataset))),
          tf_(std::move(tf)),
          sliceState_({dataset_->sizeZ() / 2u, dataset_->sizeY() / 2u, dataset_->sizeX() / 2u}),
          box_(std::make_shared<const data::Mesh>(app::makeBoxMesh(glm::vec3(32, 32, 10), glm::vec3(96, 96, 60)))),
          ctx_(broker::AppContext::Params{}) {
        const std::array<MprAxis, 3> axes = {MprAxis::Transverse, MprAxis::Coronal, MprAxis::Sagittal};
        for (size_t i = 0; i < 3u; ++i) {
            scene::VolumeSliceObject vs;
            vs.volume = dataset_;
            vs.transferFunction = tf_;
            vs.transform = app::sliceVolumeModel(*dataset_, axes[i]);
            sliceIds_[i] = ctx_.store().addVolumeSliceObject(std::move(vs));
        }
        scene::MeshObject box;
        box.mesh = box_;
        box.transform = glm::mat4(1.0f);
        box.presentation.phong.baseColor = glm::vec4(0.2f, 0.4f, 0.8f, 1.0f);
        boxId_ = ctx_.store().addMeshObject(std::move(box));
        for (size_t i = 0; i < 3u; ++i) {
            scene::ContourObject co;
            co.mesh = box_;
            co.transform = axisDisplayModels()[i];
            co.plane.setNormal(glm::vec3(0, 0, 1));
            co.plane.setPoint(glm::vec3(0, 0, static_cast<float>(sliceState_.transverseZ) + 0.5f));
            co.plane.setSpace(scene::Space::World);
            co.color = app::kContourColor;
            contourIds_[i] = ctx_.store().addContourObject(std::move(co));
        }
        const auto grid = app::mprViewports(app::kMprWindowWidth, app::kMprWindowHeight);
        for (size_t i = 0; i < 3u; ++i) {
            const auto [freeW, freeH] = app::sliceFreeAxes(*dataset_, axes[i]);
            views_[i].id = 10u + static_cast<uint64_t>(i);
            views_[i].rect = scene::Rect{grid[i].x, grid[i].y, grid[i].width, grid[i].height};
            views_[i].camera = broker::makeSliceCamera(static_cast<float>(freeW), static_cast<float>(freeH));
            views_[i].setClearColor(glm::vec4(0, 0, 0, 1));
            views_[i].setItemIds({sliceIds_[i], contourIds_[i]});
        }
        views_[3].id = 13u;
        views_[3].rect = scene::Rect{grid[3].x, grid[3].y, grid[3].width, grid[3].height};
        views_[3].setClearColor(glm::vec4(0.10f, 0.10f, 0.14f, 1.0f));
        views_[3].setItemIds({boxId_});
        grid_ = grid;
        applySliceState();
    }

    void onResize(int width, int height) noexcept override {
        resolveLiveGrid(width, height);
        applySliceState();
    }

    data::Result<void> renderFrame(int width, int height) override {
        resolveLiveGrid(width, height);
        // Long-lived MPR disables auto-scroll so the slice state stays static and the user drives the 3D view
        // interactively; the three 2D views keep their PlaneDesc and orthographic framing.
        applySliceState();
        // T10 MPR 3D-only interaction — the long-lived MPR peer calls GlfwCameraInteractor::update only on
        // the bottom-right 3D perspective view before the broker sync so the three 2D orthographic slice
        // views that carry a PlaneDesc are never orbited, the WantCaptureMouse guard is respected, and only
        // the 3D camera's viewGen bump propagates via View::setCamera to the broker's dirty-field path.
        interactor_.update(views_[3]);
        frame_.assign(views_.begin(), views_.end());
        return app::syncRenderPresent(ctx_, frame_);
    }

    const char* /*borrow*/ title() const override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "MPR long-lived: 2x2 grid interactive 3D only (runInteractive)";
    }

    const char* /*borrow*/ instructions() const noexcept override { // @note lifetime: borrowed — points to static string literal owned by sample, valid for program lifetime
        return "Long-lived MPR — runInteractive until close, 3D view orbit/pan/zoom via interactor, 2D orthographic skip.";
    }

   private:
    void resolveLiveGrid(int width, int height) {
        grid_ = app::mprViewports(width, height);
        for (size_t i = 0; i < views_.size(); ++i) {
            views_[i].setRect(scene::Rect{grid_[i].x, grid_[i].y, grid_[i].width, grid_[i].height});
        }
    }

    void applySliceState() {
        const std::array<MprAxis, 3> axes = {MprAxis::Transverse, MprAxis::Coronal, MprAxis::Sagittal};
        for (size_t i = 0; i < 3u; ++i) {
            glm::vec3 voxelPoint(0.0f);
            voxelPoint[axisComponent(axes[i])] = static_cast<float>(axisComponentValue(i));
            scene::PlaneDesc plane;
            plane.setNormal(glm::vec3(0, 0, 1));
            plane.setPoint(voxelPoint);
            plane.setSpace(scene::Space::VoxelIndex);
            views_[i].setPlane(plane);
        }
        const float aspect3d = app::aspectFromDims(grid_[3].width, grid_[3].height);
        scene::Camera cam = broker::make3dCamera(app::sliceCrosshair(sliceState_), box_->bounds(), aspect3d);
        views_[3].setCamera(std::move(cam));
    }

    float axisComponentValue(size_t i) const {
        if (i == 0) return static_cast<float>(sliceState_.transverseZ);
        if (i == 1) return static_cast<float>(sliceState_.coronalY);
        return static_cast<float>(sliceState_.sagittalX);
    }

    static constexpr int axisComponent(MprAxis axis) noexcept {
        switch (axis) {
            case MprAxis::Transverse: return 2;
            case MprAxis::Coronal: return 1;
            case MprAxis::Sagittal: return 0;
        }
        return 2;
    }

    std::array<glm::mat4, 3> axisDisplayModels() {
        return {glm::mat4(1.0f),
                glm::mat4(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
                glm::mat4(0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f)};
    }

    std::shared_ptr<data::VolumeDataset> dataset_;
    volume::TransferFunction tf_;
    app::MprSliceState sliceState_;
    std::shared_ptr<const data::Mesh> box_;
    broker::AppContext ctx_;
    std::array<uint64_t, 3> sliceIds_{};
    uint64_t boxId_{0};
    std::array<uint64_t, 3> contourIds_{};
    std::array<scene::View, 4> views_{};
    std::array<app::MprViewport, 4> grid_{};
    std::vector<scene::View> frame_{};
    scene::CameraController controller_{};
    app::GlfwCameraInteractor interactor_{controller_};
};

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--help" || a == "-h") {
            spdlog::info("re_sample_mpr_long — long-lived MPR sample (T10) 3D interactive only");
            return 0;
        }
    }
    auto windowResult = core::Window::create(app::kMprWindowWidth, app::kMprWindowHeight, "MPR Long - Interactive");
    if (windowResult.failed()) {
        spdlog::error("window: {}", windowResult.error().message);
        return 1;
    }
    const std::string volumePath = std::string(RE_SOURCE_DIR) + "/data/volumes/sample_ct.nrrd";
    auto volumeResult = re::io::loadNrrdVolume(volumePath);
    if (volumeResult.failed()) {
        spdlog::error("mpr long: failed to load '{}': {}", volumePath, volumeResult.error().message);
        return 1;
    }
    auto sample = std::make_unique<MprLongSample>(std::move(*volumeResult), app::RE_CT_TF());
    app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.runInteractive();
}
