// app/plane_sample.cpp — plane-capability sample: GPU-extracted volume planes
// (FR-app.1 smoke; extends FR-render.5 with the extraction path).
//
// In this engine a "plane" semantically means a slice extracted from volume
// data, so this sample demonstrates exactly that: it loads the committed CT
// dataset (data/volumes/sample_ct.nrrd via io::loadNrrdVolume), uploads it ONCE
// into the shared asset store as an R32F core::Texture3D, and renders two
// GPU-EXTRACTED planes through render::VolumeSliceRenderer — no gradient quad,
// no mesh, no CPU voxel loop anywhere in the file:
//
//   - left view  (axial):    the constant-Z plane through the middle voxel
//     layer, displayed in the shared MPR display frame (app::sliceVolumeModel
//     + app::makeSliceCamera over the free-axis extents), i.e. the exact view
//     an MPR Transverse viewport shows;
//   - right view (oblique):  the same dataset cut by the diagonal plane
//     x + z = 1 through the cube center, viewed orthographically along its
//     normal — demonstrating that the extraction is fully general (the
//     fragment shader intersects every pixel ray with the plane), not limited
//     to axis-aligned slabs.
//
// Both views compose through one full-window split of render::View objects
// (ReView per screen section owning ViewTarget + IRenderable list): each View
// renders the VolumeSliceScene layer into its own FBO via
// VolumeSliceRenderer::drawLayer and presents it with core::blit. A slice index
// enters only through uniforms (the clip-plane point / model matrix), which is
// what makes slice scrolling cheap — the MPR sample drives exactly this path
// interactively.
//
// Bounded, clean exit contract: after RE_SAMPLE_MAX_FRAMES frames (default
// 300) or a window close, the harness stops the loop, tears down ImGui/GL
// cleanly and returns exit code 0 — any frame error returns 1 instead. This
// shape is what lets the gate run the sample headlessly under Xvfb inside a
// timeout and assert "exits cleanly, no sanitizer reports, window opened"
// without a human watching it.
//
// (Capability acceptance: FR-app.1.)

#include <spdlog/spdlog.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <utility>

#include "app/mpr_camera.hpp"
#include "app/mpr_slice.hpp"
#include "app/sample_harness.hpp"
#include "core/draw.hpp"
#include "core/window.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "io/volume/nrrd_volume_loader.hpp"
#include "render/types.hpp" // render::Camera / ClipPlane
#include "render/view.hpp"
#include "render/volume_slice_renderer.hpp"
#include "volume/transfer_function.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

namespace core = re::core;
namespace data = re::data;
namespace render = re::render;
namespace volume = re::volume;

// The harness window size, split into two side-by-side views.
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
constexpr int kViewWidth = kWindowWidth / 2;
constexpr int kViewHeight = kWindowHeight;
// Default number of frames before the sample exits cleanly.
constexpr int kDefaultFrames = 300;

/// A CT window/level transfer function over the sample_ct value range
/// ([-3024, 2529]): air (low densities) transparent, soft tissue opaque and
/// bright. Deterministic control points (exact-at-breakpoint piecewise-linear
/// ramp); mirrors the ramp the volume and MPR samples use, so all three
/// samples display consistent tissue colors.
volume::TransferFunction makeCtTransferFunction() {
    using CP = volume::TransferFunction::ControlPoint;
    return volume::TransferFunction(
        {CP{-1024.0f, volume::RgbaColor{0.0f, 0.0f, 0.0f, 0.0f}},
         CP{-300.0f, volume::RgbaColor{0.05f, 0.05f, 0.10f, 0.05f}},
         CP{40.0f, volume::RgbaColor{0.90f, 0.50f, 0.20f, 0.90f}},
         CP{300.0f, volume::RgbaColor{0.90f, 0.50f, 0.20f, 1.00f}},
         CP{2500.0f, volume::RgbaColor{1.00f, 1.00f, 1.00f, 1.00f}}});
}

/// The camera looking straight down the oblique plane's normal at the cube
/// center: eye on +n at distance 3, square ortho window ±0.75 (the diagonal
/// cross-section rectangle of the unit cube is √2 × 1, so half-extents of
/// 0.75 horizontally and vertically cover it with margin in both directions).
render::Camera makeObliqueCamera(const glm::vec3& center,
                                 const glm::vec3& planeNormal) {
    render::Camera camera;
    camera.position = center + planeNormal * 3.0f;
    camera.view =
        glm::lookAt(camera.position, center, glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(-0.75f, 0.75f, -0.75f, 0.75f, 0.1f, 10.0f);
    return camera;
}

/// The plane sample: owns the CT dataset + transfer function and shows two
/// GPU-extracted planes through two ReViews.
class PlaneSample final : public re::app::ISample {
   public:
    PlaneSample(data::VolumeDataset dataset, volume::TransferFunction tf)
        : dataset_(std::make_shared<data::VolumeDataset>(std::move(dataset))),
          tf_(std::move(tf)) {
        // --- Left view: the axial (constant-Z) extraction -----------------
        // Shared display scaffolding: the model maps the dataset's unit cube
        // into the Transverse display frame (voxel-center index i -> display
        // coordinate i + 0.5 on the free axes), the clip plane cuts the
        // middle voxel layer (index sizeZ/2 -> display z = index + 0.5), and
        // the orthographic camera spans the free-axis rectangle — the same
        // three values an MPR Transverse view composes with.
        const auto [freeW, freeH] =
            re::app::sliceFreeAxes(*dataset_, re::app::MprAxis::Transverse);
        const float midZ = static_cast<float>(dataset_->sizeZ() / 2u) + 0.5f;

        render::VolumeSliceInstance axial;
        axial.dataset = dataset_;
        axial.transferFunction = tf_;
        axial.model =
            re::app::sliceVolumeModel(*dataset_, re::app::MprAxis::Transverse);
        axial.plane.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        axial.plane.point = glm::vec3(0.0f, 0.0f, midZ);
        axialScene_.slices.push_back(axial);
        axialCamera_ = re::app::makeSliceCamera(static_cast<float>(freeW),
                                                static_cast<float>(freeH));

        // --- Right view: the oblique extraction ---------------------------
        // Identity model leaves the dataset's unit cube at [0,1]^3 in world
        // space; the extraction plane is the cube's diagonal plane
        // x + z = 1 through the center (normal normalize(1,0,1)), proving the
        // path handles arbitrary orientations without any special casing.
        render::VolumeSliceInstance oblique;
        oblique.dataset = dataset_;
        oblique.transferFunction = tf_;
        oblique.model = glm::mat4(1.0f);
        oblique.plane.normal = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));
        oblique.plane.point = glm::vec3(0.5f, 0.5f, 0.5f);
        obliqueScene_.slices.push_back(oblique);
        obliqueCamera_ =
            makeObliqueCamera(oblique.plane.point, oblique.plane.normal);
    }

    re::data::Result<void> renderFrame(int width, int height) override {
        // Clear the window behind the two-view split (a background, not a
        // viewport blend — the views are placed by the engine blit).
        core::bindDefaultFramebuffer();
        core::setViewport(0, 0, width, height);
        core::setClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        core::clearColor();

        // Left rect: axial slice; right rect: oblique slice. Each ReView owns
        // its ViewTarget and renders its extraction layer via drawLayer, then
        // blits 1:1 into its window rect (rect sizes match the ViewTargets).
        const std::array<render::ViewRect, 2> rects = {
            render::ViewRect{0, 0, kViewWidth, kViewHeight},
            render::ViewRect{kViewWidth, 0, kViewWidth, kViewHeight}};
        const std::array<const render::VolumeSliceScene*, 2> scenes = {
            &axialScene_, &obliqueScene_};
        const std::array<render::Camera, 2> cameras = {axialCamera_,
                                                       obliqueCamera_};

        for (std::size_t i = 0u; i < 2u; ++i) {
            render::View view(rects[i], glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            view.setCamera(cameras[i]);
            view.addItem(*scenes[i], sliceRenderer_);
            core::DrawContext ctx;
            auto rendered = view.renderWithEnsure(ctx);
            if (rendered.failed()) {
                return rendered;
            }
            auto blitted = view.blitTo(nullptr);
            if (blitted.failed()) {
                return blitted;
            }
        }
        return re::data::Result<void>(re::data::value);
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
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    std::shared_ptr<data::VolumeDataset> dataset_;
    volume::TransferFunction tf_; // immutable value; copied into instances

    render::VolumeSliceScene axialScene_;
    render::VolumeSliceScene obliqueScene_;
    render::Camera axialCamera_{};
    render::Camera obliqueCamera_{};

    // The shared GPU asset store (unified multi-kind asset registry): one R32F
    // Texture3D per distinct dataset content, co-owned by every renderer that
    // resolves through it — the extraction here and any ray-cast elsewhere
    // share a single upload. Declared before its renderer and injected as a
    // shared_ptr copy, so member-init order can never dangle it.
    std::shared_ptr<render::AssetRegistry> assets_{
        std::make_shared<render::AssetRegistry>()};
    // Shared renderer: the Views' IRenderable items co-own it via shared_ptr,
    // so view and renderer lifetimes can never race at teardown.
    std::shared_ptr<render::VolumeSliceRenderer> sliceRenderer_{
        std::make_shared<render::VolumeSliceRenderer>(assets_)};
};

} // namespace

int main() {
    // Load the committed CT volume before anything else: the extracted planes
    // come from real scan data, not from procedural stand-ins. The dataset is
    // a committed, licensed, checksum-pinned asset under data/volumes/, and
    // loading it first means a missing or corrupt file fails fast with a
    // typed error and exit code 1 — never half-initialized GL state.
    const std::string volumePath =
        std::string(RE_SOURCE_DIR) + "/data/volumes/sample_ct.nrrd";
    auto volumeResult = re::io::loadNrrdVolume(volumePath);
    if (volumeResult.failed()) {
        spdlog::error("plane sample: failed to load '{}': {}", volumePath,
                      volumeResult.error().message);
        return 1;
    }

    auto windowResult = core::Window::create(kWindowWidth, kWindowHeight,
                                             "RenderEngine - Plane Sample");
    if (windowResult.failed()) {
        spdlog::error("plane sample: {}", windowResult.error().message);
        return 1;
    }

    auto sample = std::make_unique<PlaneSample>(std::move(*volumeResult),
                                                makeCtTransferFunction());
    re::app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.run(re::app::sampleMaxFrames(kDefaultFrames));
}
