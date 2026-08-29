// app/volume_long.cpp — long-lived volume sample: the interactive peer of the bounded 300-frame volume
// ray-cast sample that bypasses RE_SAMPLE_MAX_FRAMES and drives SampleHarness::runInteractive()
// until shouldClose() while its renderFrame calls GlfwCameraInteractor::update before
// syncRenderPresent with left-drag rotate dx*0.5deg, right-drag pan dx*0.01 and scroll/middle-drag
// zoom exp(-dy*0.02), respecting WantCaptureMouse and skipping orthographic/PlaneDesc views so
// View::setCamera bumps viewGen only on dirty camera fields; EXCLUDE_FROM_ALL so ctest never runs it — T10.

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

class VolumeLongSample final : public app::ISample {
   public:
    VolumeLongSample(data::VolumeDataset dataset, volume::TransferFunction tf)
        : dataset_(std::make_shared<const data::VolumeDataset>(std::move(dataset))),
          tf_(std::move(tf)),
          ctx_(broker::AppContext::Params{}) {
        scene::VolumeObject vo;
        vo.volume = dataset_;
        vo.transferFunction = tf_;
        vo.transform = glm::mat4(1.0f);
        const uint64_t volId = ctx_.store().addVolumeObject(std::move(vo));

        const glm::vec3 center(0.5f, 0.5f, 0.5f);
        scene::SceneViewBuilder bld(1, scene::Rect{0, 0, app::kWindowWidth, app::kWindowHeight},
                                    {app::kDefaultFovYDeg, 0.1f, 10.0f});
        bld.withCamera(scene::Camera(glm::vec3(0.5f, 0.5f, 3.0f), center, glm::vec3(0.0f, 1.0f, 0.0f)));
        view_ = bld.build();
        bld.syncLive(app::kWindowWidth, app::kWindowHeight);
        view_ = bld.view();
        view_.setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        view_.setItemIds({volId});
        builder_ = std::move(bld);
        builder_.view() = view_;
    }

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
        return "Volume long-lived: ray-cast CT chest interactive (runInteractive)";
    }

    const char* instructions() const noexcept override {
        return "Long-lived volume — runInteractive until close, left rotate dx*0.5deg, right pan dx*0.01, zoom exp(-dy*0.02), WantCaptureMouse guard, view.setCamera bump.";
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

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--help" || a == "-h") {
            spdlog::info("re_sample_volume_long — long-lived volume sample (T10) runInteractive until close");
            spdlog::info("Camera: left-drag rotate dx*0.5deg, right-drag pan dx*0.01, zoom exp(-dy*0.02)");
            return 0;
        }
    }
    auto windowResult = core::Window::create(app::kWindowWidth, app::kWindowHeight, "Volume Long - Interactive");
    if (windowResult.failed()) {
        spdlog::error("window: {}", windowResult.error().message);
        return 1;
    }
    const std::string volumePath = std::string(RE_SOURCE_DIR) + "/data/volumes/sample_ct.nrrd";
    auto volumeResult = re::io::loadNrrdVolume(volumePath);
    if (volumeResult.failed()) {
        spdlog::error("volume long: failed to load '{}': {}", volumePath, volumeResult.error().message);
        return 1;
    }
    auto sample = std::make_unique<VolumeLongSample>(std::move(*volumeResult), app::RE_CT_TF());
    app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.runInteractive();
}
