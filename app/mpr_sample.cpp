// app/mpr_sample.cpp — Multi-Planar Reconstruction (MPR) sample (T14/T15,
// FR-app.2/3; V2 T2 drives its composition through the multi-view workstream,
// SPEC §9 V2.4).
//
// Demonstrates the MPR capability (SPEC §1 goal 6): a single 1280x960 window
// with a 2x2 viewport grid (four 640x480 viewports; T top-left, C top-right,
// S bottom-left, 3D bottom-right, per SPEC FR-app.2). The Transverse (constant
// Z), Coronal (constant Y) and Sagittal (constant X) views render the volume
// slice along their pinned axis (SPEC §4 FR-app.2); the 3D view (bottom-right)
// renders the golden box mesh (FR-app.3). Each slice view also overlays the
// plane∩mesh cross-section contour (FR-app.3): the box's intersection with the
// view's slice plane, computed in closed form and rasterized into the slice
// image before the PlaneRenderer displays it.
//
// Rendering architecture (app-level composition, SPEC §3):
//   - the 2x2 viewport layout and the per-axis slice sampling come from the
//     shared app/mpr_slice scaffolding (mprViewports / makeSliceImage), which
//     the T14 gate tests headlessly;
//   - the contour overlay and the 3D-view camera come from the shared
//     app/mpr_contour scaffolding (meshPlaneContour / overlayContour /
//     make3dCamera), which the T15 gate tests headlessly;
//   - the sample shares ONLY per-view window-section handles (render::ViewRect
//     from mprViewports) + abstract scene objects (render::View, holding Scene
//     dispatch variants) with the engine: render::ViewRenderer (SPEC §9 V2.4)
//     dispatches each view's scene through IRenderer (V2 T1) — the three
//     slice views are PlaneScenes, the 3D view is a MeshScene — renders each
//     into its OWN 640x480 core::Framebuffer, then blits each FBO into its
//     window rect via core::blit. There is NO app-side viewport blending: the
//     textured-quad present pass is gone, the engine present is the whole
//     composition (V2 T2).
//   - the only app-side window state is the clear of the window's default
//     framebuffer behind the viewport grid (a background, not a viewport
//     blend).
//
// The sample exits cleanly (code 0) after RE_SAMPLE_MAX_FRAMES frames (default
// 300) so the gate can run it headlessly under Xvfb within a timeout
// (FR-app.1: exit code 0, no sanitizer reports).

#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/mpr_contour.hpp"
#include "app/mpr_slice.hpp"
#include "app/sample_harness.hpp"
#include "core/draw.hpp"
#include "core/window.hpp"
#include "data/image.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "io/volume/nrrd_volume_loader.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_renderer.hpp" // render::MeshScene / render::Camera
#include "render/phong_material.hpp"
#include "render/plane_renderer.hpp"
#include "render/view_renderer.hpp"
#include "volume/transfer_function.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

// Convenience namespace aliases for the sample's own .cpp (NAMING_CONVENTIONS
// §7: `using`/aliases are allowed inside a .cpp translation unit).
namespace core = re::core;
namespace data = re::data;
namespace volume = re::volume;
namespace render = re::render;
namespace app = re::app;

// The harness window size (SPEC FR-app.2: 1280x960).
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 960;
// Default number of frames before the sample exits cleanly.
constexpr int kDefaultFrames = 300;
// The offscreen slice-view resolution (SPEC FR-app.2: each viewport is
// 640x480).
constexpr std::uint32_t kViewportWidth = 640u;
constexpr std::uint32_t kViewportHeight = 480u;

// The golden box mesh (FR-app.3): a box spanning these voxel-index bounds,
// inside the 128x128x70 CT volume, so every slice view's plane intersects it
// (the Transverse/Coronal/Sagittal contours are the box's cross-section
// rectangles). Integer bounds + half-integer slice planes => non-degenerate
// closed-form cross-sections.
constexpr glm::vec3 kGoldenBoxMin(32.0f, 32.0f, 10.0f);
constexpr glm::vec3 kGoldenBoxMax(96.0f, 96.0f, 60.0f);

// The 3D-view box material: a clean solid color mapping to exact RGBA8 bytes
// (0.2*255=51, 0.4*255=102, 0.8*255=204). Opaque (alpha 1.0), so the v1 opaque
// forward pass draws the box with no OIT engaged. Under the v1 flat +Z
// lighting the +Z face of the box shades to exactly this color; faces with
// normals not aligned to +Z shade to black (docs/render.md / docs/mpr.md).
constexpr glm::vec4 kBoxMaterialColor(0.2f, 0.4f, 0.8f, 1.0f);

/// A CT window/level transfer function over the sample_ct value range
/// ([-3024, 2529], SPEC §7): air (low) transparent, soft tissue opaque/bright.
/// Deterministic control points (FR-vol.1); monotonic alpha ramp. Mirrors the
/// T12 volume sample's TF.
volume::TransferFunction makeCtTransferFunction() {
    using CP = volume::TransferFunction::ControlPoint;
    return volume::TransferFunction(
        {CP{-1024.0f, volume::RgbaColor{0.0f, 0.0f, 0.0f, 0.0f}},
         CP{-300.0f, volume::RgbaColor{0.05f, 0.05f, 0.10f, 0.05f}},
         CP{40.0f, volume::RgbaColor{0.90f, 0.50f, 0.20f, 0.90f}},
         CP{300.0f, volume::RgbaColor{0.90f, 0.50f, 0.20f, 1.00f}},
         CP{2500.0f, volume::RgbaColor{1.00f, 1.00f, 1.00f, 1.00f}}});
}

/// Build the shared slice-state: hold each 2D view on the middle slice of its
/// axis (shared slice-state/camera scaffolding, T14). The slice state drives
/// both the slice views' contour planes and the 3D view camera's look-at
/// target (make3dCamera, T15): the 3D view refocuses on the intersection point
/// of the three slice planes.
app::MprSliceState makeInitialSliceState(const data::VolumeDataset& dataset) {
    app::MprSliceState state;
    state.transverseZ = dataset.sizeZ() / 2u;
    state.coronalY = dataset.sizeY() / 2u;
    state.sagittalX = dataset.sizeX() / 2u;
    return state;
}

/// The orthographic camera for one 2D slice view (the shared 2D-view camera
/// configuration, derived per view from that view's slice image): maps the
/// image's pixel space [0,imgW]x[0,imgH] onto the full 640x480 viewport. The
/// camera looks down -Z from +Z at the quad (which sits at z = 0), so the
/// slice displays with the image's top-left at the viewport's top-left (the
/// PlaneRenderer orientation convention, FR-render.5 /
/// render/plane_renderer.hpp).
render::Camera makeSliceCamera(const data::Image& image) {
    render::Camera camera;
    camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0.0f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(0.0f, static_cast<float>(image.width()), 0.0f,
                             static_cast<float>(image.height()), 0.1f, 10.0f);
    return camera;
}

/// The model matrix scaling the shared unit quad [-1,1]^2 onto the slice
/// image's pixel rectangle [0,imgW]x[0,imgH], so the whole slice image fills
/// the viewport (with the shared camera above).
glm::mat4 makeSliceModel(const data::Image& image) {
    const float halfW = static_cast<float>(image.width()) * 0.5f;
    const float halfH = static_cast<float>(image.height()) * 0.5f;
    return glm::translate(glm::mat4(1.0f), glm::vec3(halfW, halfH, 0.0f)) *
           glm::scale(glm::mat4(1.0f), glm::vec3(halfW, halfH, 1.0f));
}

/// The MPR sample: owns the volume + transfer function + the slice-view
/// scaffold and renders one frame of the 2x2 grid into the window's default
/// framebuffer, composed by the engine multi-view compositor (SPEC §9 V2.4).
class MPRView final : public app::ISample {
   public:
    MPRView(data::VolumeDataset dataset, volume::TransferFunction tf)
        : dataset_(std::move(dataset)),
          tf_(std::move(tf)),
          sliceState_(makeInitialSliceState(dataset_)),
          transverseImage_(makeSliceImage(dataset_, tf_,
                                          app::MprAxis::Transverse,
                                          sliceState_.transverseZ)),
          coronalImage_(makeSliceImage(dataset_, tf_, app::MprAxis::Coronal,
                                       sliceState_.coronalY)),
          sagittalImage_(makeSliceImage(dataset_, tf_, app::MprAxis::Sagittal,
                                        sliceState_.sagittalX)),
          box_(app::makeBoxMesh(kGoldenBoxMin, kGoldenBoxMax)),
          boxMaterial_(kBoxMaterialColor),
          composer_(4u, kViewportWidth, kViewportHeight) {
        // One shared unit quad, scaled per slice view onto that view's image
        // pixel rectangle (makeSliceModel) and viewed through a per-view
        // orthographic camera (makeSliceCamera) — the shared 2D-view camera
        // scaffolding that maps each slice image onto its full viewport.
        quad_ = render::PlaneGeometry::unitQuadXY();

        // The golden box mesh (FR-app.3) with an opaque material, for the 3D
        // view. The box is registered ONCE with the shared registry (SPEC §9
        // V2.5) — the 3D scene carries its AssetHandle, resolved by
        // MeshRenderer through the same registry (graceful degradation on the
        // impossible registration failure, see mesh_sample). The 3D-view camera
        // is driven by the slice state (make3dCamera): it looks at the
        // intersection point of the three slice planes.
        const auto boxHandle = registry_.registerAsset(box_);
        if (boxHandle.failed()) {
            spdlog::error("mpr sample: failed to register box mesh: {}",
                          boxHandle.error().message);
        } else {
            boxScene_.meshes.push_back(render::MeshInstance{
                *boxHandle, &boxMaterial_, glm::mat4(1.0f)});
        }
        boxCamera_ = app::make3dCamera(sliceState_, box_.bounds(),
                                       static_cast<float>(kViewportWidth) /
                                           static_cast<float>(kViewportHeight));

        // Overlay the plane∩mesh cross-section contour on each slice view
        // (FR-app.3): the box's intersection with that view's slice plane,
        // computed in closed form and rasterized into the slice image at the
        // FR-app.3 contour color, so the PlaneRenderer shows the contour on
        // top of the slice.
        const std::array<app::MprAxis, 3> axes = {app::MprAxis::Transverse,
                                                  app::MprAxis::Coronal,
                                                  app::MprAxis::Sagittal};
        const std::array<data::Image*, 3> images = {
            &transverseImage_, &coronalImage_, &sagittalImage_};
        for (std::size_t i = 0u; i < 3u; ++i) {
            const app::SlicePlane plane = app::slicePlane(axes[i], sliceState_);
            const std::vector<app::ContourSegment> curve =
                app::meshPlaneContour(box_, plane);
            *images[i] =
                app::overlayContour(*images[i], curve, app::kContourColor);
        }

        // Register the technique renderers with the engine compositor (SPEC
        // §9 V2.3): the three slice views are PlaneScenes rendered by
        // PlaneRenderer, the 3D view is a MeshScene rendered by MeshRenderer.
        composer_.setRenderer(render::SceneKind::Mesh, &boxRenderer_);
        composer_.setRenderer(render::SceneKind::Plane, &sliceRenderer_);
    }

    data::Result<void> renderFrame(int width, int height) override {
        // Clear the window's default framebuffer behind the viewport grid
        // (a background, not a viewport blend — the views themselves are
        // placed by the engine blit in composer_.render).
        core::bindDefaultFramebuffer();
        core::setViewport(0, 0, width, height);
        core::setClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        core::clearColor();

        // Build the four views: per-view window-section handles (ViewRects
        // from app::mprViewports, SPEC FR-app.2) + abstract scene objects
        // (render::View). The three slice scenes are locals kept alive for the
        // whole frame: the Scene variant holds pointers into them, valid until
        // composer_.render below returns.
        const std::array<app::MprViewport, 4> grid =
            app::mprViewports(width, height);
        const std::array<const data::Image*, 3> sliceImages = {
            &transverseImage_, &coronalImage_, &sagittalImage_};

        std::array<render::View, 4> views;
        std::array<render::PlaneScene, 3> sliceScenes;
        for (std::size_t i = 0u; i < 3u; ++i) {
            // The slice view (T/C/S): the shared unit quad scaled onto the
            // slice image's pixel rectangle, seen through the per-view camera.
            sliceScenes[i].planes.push_back(render::PlaneInstance{
                &quad_, sliceImages[i], makeSliceModel(*sliceImages[i])});
            views[i].scene = &sliceScenes[i];
            views[i].camera = makeSliceCamera(*sliceImages[i]);
            views[i].clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            views[i].rect = render::ViewRect{grid[i].x, grid[i].y,
                                             grid[i].width, grid[i].height};
        }
        // The 3D view (bottom-right): the golden box mesh (FR-app.3) through
        // the slice-state-driven camera (make3dCamera).
        views[3].scene = &boxScene_;
        views[3].camera = boxCamera_;
        views[3].clearColor = glm::vec4(0.10f, 0.10f, 0.14f, 1.0f);
        views[3].rect = render::ViewRect{grid[3].x, grid[3].y, grid[3].width,
                                         grid[3].height};

        // Engine composition (SPEC §9 V2.4, Model B: per-view FBO + engine
        // blit): renderViews() dispatches each scene through IRenderer into
        // its own 640x480 FBO, then present() blits each FBO into its window
        // rect via core::blit (destination nullptr = the window's default
        // framebuffer). No app-side viewport blending.
        const std::vector<render::View> viewList(views.begin(), views.end());
        return composer_.render(viewList, nullptr);
    }

    const char* title() const override {
        return "MPR sample: 2x2 viewport grid (T/C/S slices + contours + 3D)";
    }

    const char* instructions() const noexcept override {
        return "Capability: Multi-Planar Reconstruction (SPEC FR-app.2/3).\n"
               "A single 1280x960 window shows four 640x480 viewports in a "
               "2x2 grid:\n"
               "T (top-left) = Transverse slice (constant Z) + mesh contour,\n"
               "C (top-right) = Coronal slice (constant Y) + mesh contour,\n"
               "S (bottom-left) = Sagittal slice (constant X) + mesh "
               "contour,\n"
               "3D (bottom-right) = the golden box mesh, viewed from the "
               "slice-state crosshair (the intersection of the three slice "
               "planes).\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    data::VolumeDataset dataset_;
    volume::TransferFunction tf_;
    app::MprSliceState sliceState_;

    data::Image transverseImage_;
    data::Image coronalImage_;
    data::Image sagittalImage_;

    // The golden box mesh + material for the 3D view (FR-app.3).
    data::Mesh box_;
    render::PhongMaterial boxMaterial_;
    // The shared asset registry (SPEC §9 V2.5): owns the box's GPU geometry;
    // declared before the renderer so `&registry_` is valid at its
    // construction.
    render::AssetRegistry registry_;
    render::MeshScene boxScene_;
    render::Camera boxCamera_;
    render::MeshRenderer boxRenderer_{&registry_};

    render::PlaneGeometry quad_;
    render::PlaneRenderer sliceRenderer_;

    // The engine multi-view compositor (SPEC §9 V2.4): owns the four per-view
    // 640x480 FBOs and performs the render-into-FBO + blit-into-window-rect
    // composition (core::blit).
    render::ViewRenderer composer_;
};

} // namespace

int main() {
    const std::string volumePath =
        std::string(RE_SOURCE_DIR) + "/data/volumes/sample_ct.nrrd";
    auto volumeResult = re::io::loadNrrdVolume(volumePath);
    if (volumeResult.failed()) {
        spdlog::error("mpr sample: failed to load '{}': {}", volumePath,
                      volumeResult.error().message);
        return 1;
    }

    auto windowResult = re::core::Window::create(kWindowWidth, kWindowHeight,
                                                 "RenderEngine - MPR Sample");
    if (windowResult.failed()) {
        spdlog::error("mpr sample: {}", windowResult.error().message);
        return 1;
    }

    auto sample = std::make_unique<MPRView>(std::move(*volumeResult),
                                            makeCtTransferFunction());
    re::app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.run(re::app::sampleMaxFrames(kDefaultFrames));
}
