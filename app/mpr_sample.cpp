// app/mpr_sample.cpp — Multi-Planar Reconstruction (MPR) sample (T14/T15,
// FR-app.2/3; T5 V3.4 drives its composition through
// ReView/ViewTarget/IRenderable, SPEC §3.2; V3.8b T11 moved the contour overlay
// to the GPU ContourRenderer).
//
// Demonstrates the MPR capability (SPEC §1 goal 6): a single 1280x960 window
// with a 2x2 viewport grid (four 640x480 viewports; T top-left, C top-right,
// S bottom-left, 3D bottom-right, per SPEC FR-app.2). The Transverse (constant
// Z), Coronal (constant Y) and Sagittal (constant X) views show a slice of the
// volume EXTRACTED ON THE GPU: render::VolumeSliceRenderer samples the cached
// R32F 3D texture exactly where each pixel ray crosses that view's clip plane
// (texel mapping (idx+0.5)/dim, the same mapping the ray-cast uses), so there
// is NO frozen CPU slice image anywhere on this path and scrolling is a pure
// uniform/state change. The sample scrolls deterministically: every
// kFramesPerStep frames the round-robin next axis advances one voxel layer,
// and all three 2D views plus the 3D camera track the shared slice state —
// demonstrating interactive slice navigation without any CPU re-looping
// (the CPU oracle app::makeSliceImage is retained ONLY as the gate tests'
// reference implementation, never called here).
//
// Rendering architecture (app-level composition, SPEC §3):
//   - the 2x2 viewport layout comes from the shared app/mpr_slice scaffolding
//     (mprViewports); each slice view's display frame comes from the shared
//     sliceVolumeModel + makeSliceCamera(free-axis extents) pair — the exact
//     functions the gate tests drive, so a sample-vs-test wiring divergence
//     cannot reintroduce itself (the T11 defect lesson);
//   - each slice view layers TWO ReView items: the GPU-extracted volume plane
//     (VolumeSliceRenderer::drawLayer) and the plane∩mesh contour overlay
//     translated scene→render through broker::ContourMapper (SPEC §11) and
//     drawn by render::ContourRenderer's geometry shader (FR-app.3) — both
//     rebuilt from the CURRENT slice state each frame, so outlines stay glued
//     to the displayed layer while scrolling;
//   - the 3D-view camera comes from app::make3dCamera (app/mpr_camera.hpp),
//     which looks at the intersection point of the three CURRENT slice planes
//     (the crosshair) — the slice-state ↔ 3D-view camera interplay;
//   - each ReView renders into its OWN 640×480 ViewTarget via DrawContext and
//     presents it with core::blit (T5 V3.4 engine present): no app-side
//     viewport blending, no ViewRenderer;
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
#include <cstdlib>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/mpr_camera.hpp"
#include "app/mpr_slice.hpp"
#include "app/sample_harness.hpp"
#include "broker/broker.hpp"
#include "broker/contour_mapper.hpp"
#include "core/draw.hpp"
#include "core/window.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "io/volume/nrrd_volume_loader.hpp"
#include "render/asset_registry.hpp"
#include "render/contour_renderer.hpp"
#include "render/mesh_renderer.hpp" // render::MeshScene / render::Camera
#include "render/phong_material.hpp"
#include "render/view.hpp"
#include "render/volume_slice_renderer.hpp"
#include "scene/object.hpp"
#include "scene/plane_desc.hpp"
#include "utils/pixel_reader.hpp"
#include "volume/transfer_function.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

// Convenience namespace aliases for the sample's own .cpp (NAMING_CONVENTIONS
// §7: `using`/aliases are allowed inside a .cpp translation unit).
namespace broker = re::broker;
namespace core = re::core;
namespace data = re::data;
namespace volume = re::volume;
namespace render = re::render;
namespace scene = re::scene;
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

// Auto-scroll cadence: every kFramesPerStep rendered frames the round-robin
// next slice axis advances one voxel layer. Deterministic, and slow enough
// that the scroll is watchable interactively while a bounded headless smoke
// run (RE_SAMPLE_MAX_FRAMES=20) stays on the initial mid-volume state.
constexpr std::uint32_t kFramesPerStep = 45u;

// The golden box mesh (FR-app.3): a box spanning these voxel-index bounds,
// inside the 128x128x70 CT volume, so every slice view's plane intersects it
// at ANY slice index (the Transverse/Coronal/Sagittal contours are the box's
// cross-section rectangles).
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
/// other samples' TF.
volume::TransferFunction makeCtTransferFunction() {
    using CP = volume::TransferFunction::ControlPoint;
    return volume::TransferFunction(
        {CP{-1024.0f, volume::RgbaColor{0.0f, 0.0f, 0.0f, 0.0f}},
         CP{-300.0f, volume::RgbaColor{0.05f, 0.05f, 0.10f, 0.05f}},
         CP{40.0f, volume::RgbaColor{0.90f, 0.50f, 0.20f, 0.90f}},
         CP{300.0f, volume::RgbaColor{0.90f, 0.50f, 0.20f, 1.00f}},
         CP{2500.0f, volume::RgbaColor{1.00f, 1.00f, 1.00f, 1.00f}}});
}

/// Build the initial slice-state: hold each 2D view on the middle slice of its
/// axis. This is the state the auto-scroll animation advances from; it drives
/// the extraction planes, the contour planes, and the 3D camera alike.
app::MprSliceState makeInitialSliceState(const data::VolumeDataset& dataset) {
    app::MprSliceState state;
    state.transverseZ = dataset.sizeZ() / 2u;
    state.coronalY = dataset.sizeY() / 2u;
    state.sagittalX = dataset.sizeX() / 2u;
    return state;
}

/// The axis-permutation display models shared with the MPR gates: they map
/// voxel-index space into each view's display space so the displayed free
/// axes are always display (x, y) — Transverse identity, Coronal swaps Y/Z,
/// Sagittal maps (x,y,z)->(y,z,x). glm::mat4's constructor takes COLUMNS, so
/// each initializer list is read DOWN the matrix (a transposed permutation
/// silently misplaces the Sagittal geometry — the T11 review finding these
/// literal matrices were pinned against).
std::array<glm::mat4, 3> axisDisplayModels() {
    return {glm::mat4(1.0f),
            // Coronal: rows (x'|y'|z') = (x|z|y).
            glm::mat4(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                      1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
            // Sagittal: rows (x'|y'|z') = (y|z|x) => columns (read DOWN)
            // (0,0,1)(1,0,0)(0,1,0).
            glm::mat4(0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f)};
}

/// The MPR sample: owns the volume + transfer function + the slice-view
/// scaffold and renders one frame of the 2x2 grid into the window's default
/// framebuffer, composed by the engine multi-view present (T5 V3.4).
class MPRView final : public app::ISample {
   public:
    MPRView(data::VolumeDataset dataset, volume::TransferFunction tf)
        : dataset_(std::make_shared<data::VolumeDataset>(std::move(dataset))),
          tf_(std::move(tf)),
          sliceState_(makeInitialSliceState(*dataset_)),
          box_(std::make_shared<data::Mesh>(
              app::makeBoxMesh(kGoldenBoxMin, kGoldenBoxMax))),
          boxMaterial_(
              std::make_shared<render::PhongMaterial>(kBoxMaterialColor)) {
        // The golden box mesh (FR-app.3) with an opaque material, for the 3D
        // view. The box is registered ONCE with the shared registry — the 3D
        // scene carries its AssetHandle, resolved by MeshRenderer through the
        // same registry (graceful degradation on the impossible registration
        // failure). The registry is ALSO where the CT dataset's single R32F
        // Texture3D lives once the first extraction draws resolve it, so the
        // whole sample shares one GPU asset store instance.
        const auto boxHandle = registry_->registerAsset(*box_);
        if (boxHandle.failed()) {
            spdlog::error("mpr sample: failed to register box mesh: {}",
                          boxHandle.error().message);
        } else {
            boxScene_.meshes.push_back(render::MeshInstance{
                *boxHandle, boxMaterial_, glm::mat4(1.0f)});
        }

        // Composition root: register the contour mapper ONCE (one mapper per
        // AppT, OCP via type_index — SPEC §11); afterwards translation goes
        // only through the type-erased IMapper interface fetched from the
        // Broker — app never holds a concrete mapper handle. The slice layers
        // need no mapper on THIS path: the extraction consumes the dataset +
        // TF + plane values directly, and the full scene→render mediation for
        // volume slices is the broker inventory follow-up task's charter.
        broker_.registerMapper(
            std::make_unique<broker::ContourMapper>(registry_));
    }

    data::Result<void> renderFrame(int width, int height) override {
        // Clear the window's default framebuffer behind the viewport grid
        // (a background, not a viewport blend — the views themselves are
        // placed by the engine blit).
        core::bindDefaultFramebuffer();
        core::setViewport(0, 0, width, height);
        core::setClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        core::clearColor();

        // Advance the deterministic auto-scroll: this mutates ONLY the slice
        // state (three integers). Every downstream consumer below derives
        // from it per frame — extraction uniforms, contour planes, 3D camera
        // — so scrolling touches no pixels on the CPU.
        advanceSliceState();

        // Build the four views: per-view window-section handles (ViewRects
        // from app::mprViewports, SPEC FR-app.2) + ReView per screen section.
        const std::array<app::MprViewport, 4> grid =
            app::mprViewports(width, height);
        auto* contourMapper =
            broker_.get<scene::ContourObject, render::ContourObject>();

        const std::array<app::MprAxis, 3> axes = {app::MprAxis::Transverse,
                                                  app::MprAxis::Coronal,
                                                  app::MprAxis::Sagittal};
        const std::array<const char*, 3> kAxisNames = {"Transverse", "Coronal",
                                                       "Sagittal"};
        // The held coordinate of each view's slice plane (voxel-index units,
        // through the sliced voxel layer's centers) under the CURRENT state.
        const std::array<float, 3> heldCoord = {
            static_cast<float>(sliceState_.transverseZ) + 0.5f,
            static_cast<float>(sliceState_.coronalY) + 0.5f,
            static_cast<float>(sliceState_.sagittalX) + 0.5f};
        const std::array<glm::mat4, 3> displayModels = axisDisplayModels();

        for (std::size_t i = 0u; i < 3u; ++i) {
            // --- Layer 1: the GPU-extracted slice -------------------------
            // The instance carries the dataset ref, the TF value, the
            // display-frame model (voxel-center index -> display coord) and
            // the constant-Z plane at the CURRENT held coordinate. A slice
            // change therefore reaches the GPU exclusively through uniforms.
            render::VolumeSliceInstance slice;
            slice.dataset = dataset_;
            slice.transferFunction = tf_;
            slice.model = app::sliceVolumeModel(*dataset_, axes[i]);
            slice.plane.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            slice.plane.point = glm::vec3(0.0f, 0.0f, heldCoord[i]);
            render::VolumeSliceScene sliceScene;
            sliceScene.slices.push_back(slice);

            // --- Layer 2: the GPU contour of the SAME plane ---------------
            // Translated scene→render through the Broker-mediated ContourMapper
            // each frame (a pure translation; the box's GPU geometry is deduped
            // by the shared registry, so repeated translations upload nothing).
            // The clip plane lives in the object's local (= display) frame —
            // constant Z at the held coordinate — matching render::ClipPlane's
            // post-model evaluation, exactly like the extraction plane above,
            // so outline and slice stay pixel-glued while scrolling.
            render::ContourScene contourScene;
            if (contourMapper != nullptr) {
                scene::ContourObject appContour;
                appContour.mesh = box_;
                appContour.transform = displayModels[i];
                appContour.plane.setNormal(glm::vec3(0.0f, 0.0f, 1.0f));
                appContour.plane.setPoint(glm::vec3(0.0f, 0.0f, heldCoord[i]));
                appContour.color = app::kContourColor;

                auto mapped =
                    contourMapper->map(appContour, scene::TranslateContext{});
                if (mapped.failed()) {
                    // Typed errors are surfaced, never swallowed: a skipped
                    // contour layer is visually indistinguishable from "no
                    // contour", so the log names the view axis and code.
                    spdlog::error(
                        "mpr sample: {} contour translation failed "
                        "(code {}): {}",
                        kAxisNames[i], mapped.error().code,
                        mapped.error().message);
                } else {
                    contourScene.contours.push_back(*mapped);
                }
            }

            // --- Compose the view ------------------------------------------
            const auto [freeW, freeH] = app::sliceFreeAxes(*dataset_, axes[i]);
            render::ViewRect rect{grid[i].x, grid[i].y, grid[i].width,
                                  grid[i].height};
            render::View view(rect, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            view.setCamera(app::makeSliceCamera(static_cast<float>(freeW),
                                                static_cast<float>(freeH)));
            view.addItem(sliceScene, sliceRenderer_);
            if (!contourScene.contours.empty()) {
                // Second layer: drawn WITHOUT clearing, so the contour strokes
                // overwrite exactly their own pixels of the extracted slice.
                view.addItem(contourScene, contourRenderer_);
            }
            core::DrawContext ctx;
            auto r = view.renderWithEnsure(ctx);
            if (r.failed())
                return r;
            auto b = view.blitTo(nullptr);
            if (b.failed())
                return b;
        }

        // 3D view (bottom-right): the golden box mesh (FR-app.3), viewed from
        // a camera derived from the CURRENT slice state — moving any slice
        // moves the crosshair and refocuses the 3D view (the interplay).
        {
            boxCamera_ =
                app::make3dCamera(sliceState_, box_->bounds(),
                                  static_cast<float>(kViewportWidth) /
                                      static_cast<float>(kViewportHeight));
            render::ViewRect rect{grid[3].x, grid[3].y, grid[3].width,
                                  grid[3].height};
            render::View view(rect, glm::vec4(0.10f, 0.10f, 0.14f, 1.0f));
            view.setCamera(boxCamera_);
            view.addItem(boxScene_, boxRenderer_);
            core::DrawContext ctx;
            auto r = view.renderWithEnsure(ctx);
            if (r.failed())
                return r;
            auto b = view.blitTo(nullptr);
            if (b.failed())
                return b;
        }

        // Optional single-frame capture of the composed window content
        // (defect-verification aid): with RE_SAMPLE_DUMP_FRAME=<path> set,
        // the FIRST frame is written as a binary PPM (P6) so the live window
        // path — the exact composition the interactive sample shows — can be
        // verified pixel-wise without a display-side screenshot tool. Off by
        // default and free when unset (pixels are read through
        // utils::PixelReader, which delegates to the core/ readback anchor;
        // no raw readback call lives in app/).
        if (!frameDumped_) {
            frameDumped_ = true;
            const char* dumpPath = std::getenv("RE_SAMPLE_DUMP_FRAME");
            if (dumpPath != nullptr && dumpPath[0] != '\0') {
                auto dumped = dumpWindowFramePpm(
                    dumpPath, static_cast<std::uint32_t>(width),
                    static_cast<std::uint32_t>(height));
                if (dumped.ok()) {
                    spdlog::info(
                        "mpr sample: first-frame window capture "
                        "written to {}",
                        dumpPath);
                } else {
                    spdlog::error("mpr sample: frame capture failed: {}",
                                  dumped.error().message);
                }
            }
        }
        return data::Result<void>(data::value);
    }

    const char* title() const override {
        return "MPR sample: 2x2 viewport grid (GPU slices + contours + 3D)";
    }

    const char* instructions() const noexcept override {
        return "Capability: Multi-Planar Reconstruction (interactive "
               "scrolling).\n"
               "A single 1280x960 window shows four 640x480 viewports in a "
               "2x2 grid:\n"
               "T (top-left) = Transverse plane (constant Z) + GPU mesh "
               "contour,\n"
               "C (top-right) = Coronal plane (constant Y) + GPU mesh "
               "contour,\n"
               "S (bottom-left) = Sagittal plane (constant X) + GPU mesh "
               "contour,\n"
               "3D (bottom-right) = the golden box mesh, viewed from the "
               "slice-state crosshair.\n"
               "The slices are EXTRACTED ON THE GPU from the cached 3D "
               "texture (no CPU slicing): every few frames the next axis "
               "advances one voxel layer and all views track it.\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    /// One deterministic auto-scroll step every kFramesPerStep frames: the
    /// round-robin next axis advances +1, wrapping at its dimension (valid
    /// voxel-layer indices are 0..dim-1). Pure integer state mutation — the
    /// pixels move because the GPU re-extracts from the new plane uniform.
    void advanceSliceState() {
        ++frameCounter_;
        if (frameCounter_ % kFramesPerStep != 0u) {
            return;
        }
        const std::size_t axisIndex =
            (frameCounter_ / kFramesPerStep - 1u) % 3u;
        switch (axisIndex) {
            case 0u:
                sliceState_.transverseZ =
                    (sliceState_.transverseZ + 1u) % dataset_->sizeZ();
                break;
            case 1u:
                sliceState_.coronalY =
                    (sliceState_.coronalY + 1u) % dataset_->sizeY();
                break;
            case 2u:
                sliceState_.sagittalX =
                    (sliceState_.sagittalX + 1u) % dataset_->sizeX();
                break;
            default:
                break;
        }
    }

    /// Read the composed window content back (default framebuffer, via
    /// utils::PixelReader -> core::readRgba8) and write it as a binary PPM
    /// (P6, top-down rows). Diagnostic only: used to verify the LIVE sample
    /// path pixel-wise (the readback tests render into offscreen FBOs and
    /// cannot see a window-path regression like the camera-enclosure defect
    /// that hid every contour quad).
    data::Result<void> dumpWindowFramePpm(const std::string& path,
                                          std::uint32_t width,
                                          std::uint32_t height) {
        // The blits left the draw framebuffer at the window's default FB;
        // bind it explicitly so the read source is deterministic.
        core::bindDefaultFramebuffer();
        re::utils::PixelReader reader;
        std::vector<std::uint8_t> rgba;
        auto read = reader.read(0u, 0u, width, height, rgba);
        if (read.failed()) {
            return data::makeError<void>(read.error().code,
                                         read.error().message);
        }
        // GL readback is bottom-up; PPM is top-down — flip rows.
        std::vector<std::uint8_t> ppm(static_cast<std::size_t>(width) * height *
                                      3u);
        for (std::uint32_t row = 0u; row < height; ++row) {
            const std::size_t src =
                static_cast<std::size_t>(height - 1u - row) * width * 4u;
            const std::size_t dst = static_cast<std::size_t>(row) * width * 3u;
            for (std::uint32_t col = 0; col < width; ++col) {
                ppm[dst + col * 3u + 0u] = rgba[src + col * 4u + 0u];
                ppm[dst + col * 3u + 1u] = rgba[src + col * 4u + 1u];
                ppm[dst + col * 3u + 2u] = rgba[src + col * 4u + 2u];
            }
        }
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            return data::makeError<void>(1, "cannot open '" + path + "'");
        }
        out << "P6\n" << width << " " << height << "\n255\n";
        out.write(reinterpret_cast<const char*>(ppm.data()),
                  static_cast<std::streamsize>(ppm.size()));
        if (!out) {
            return data::makeError<void>(2, "short write to '" + path + "'");
        }
        return data::Result<void>(data::value);
    }

    // Shared asset + renderer handles (ownership discipline): every member
    // below that participates in a shared-ownership graph is a shared_ptr
    // with a self-initializing NSDMI. Reordering these declarations can
    // therefore never dangle or silently break init — a not-yet-initialized
    // shared handle degrades to a typed per-draw error (e.g. registry code
    // 4), surfaced via spdlog, never to undefined behavior.
    std::shared_ptr<data::VolumeDataset> dataset_;
    volume::TransferFunction tf_;   // immutable value; copied into instances
    app::MprSliceState sliceState_; // the live slice state (auto-scrolled)
    std::uint32_t frameCounter_{0}; // auto-scroll clock (frames rendered)

    // The golden box mesh + material for the 3D view (FR-app.3); both shared:
    // the mesh is co-owned by scene-side contour objects, the material by the
    // render::MeshInstance.
    std::shared_ptr<data::Mesh> box_;
    std::shared_ptr<render::PhongMaterial> boxMaterial_;

    // The shared GPU asset store (unified multi-kind registry): ONE GL object
    // per distinct CPU asset content, co-owned by every renderer here — the
    // box geometry uploaded for the 3D view and the CT dataset's single R32F
    // Texture3D resolved by the extraction renderer share one store instance.
    std::shared_ptr<render::AssetRegistry> registry_{
        std::make_shared<render::AssetRegistry>()};
    render::MeshScene boxScene_;
    render::Camera boxCamera_;
    std::shared_ptr<render::MeshRenderer> boxRenderer_{
        std::make_shared<render::MeshRenderer>(registry_)};

    // Per-slice-view GPU extraction layers (FR-app.2): each frame builds the
    // three VolumeSliceInstances fresh from the CURRENT slice state and draws
    // them through one shared VolumeSliceRenderer as the first ReView layer.
    // There is no CPU slice image anywhere on this path — a slice-index change
    // is a uniform change, which is what makes scrolling interactive.
    broker::Broker broker_;
    std::shared_ptr<render::VolumeSliceRenderer> sliceRenderer_{
        std::make_shared<render::VolumeSliceRenderer>(registry_)};

    // Per-slice-view GPU contour layers (FR-app.3): rebuilt per frame from the
    // current planes through the Broker-mediated ContourMapper and drawn by
    // the ContourRenderer as the second ReView layer. The Broker owns the
    // mapper; app only fetches the type-erased IMapper interface from it.
    std::shared_ptr<render::ContourRenderer> contourRenderer_{
        std::make_shared<render::ContourRenderer>(registry_)};

    /// One-shot guard for the RE_SAMPLE_DUMP_FRAME diagnostic capture.
    bool frameDumped_{false};
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

    auto windowResult = core::Window::create(kWindowWidth, kWindowHeight,
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
