// app/mpr_sample.cpp — Multi-Planar Reconstruction (MPR) sample (T14/T15,
// FR-app.2/3; T20 routed fully through the broker façade).
//
// Demonstrates the MPR capability (SPEC §1 goal 6): a single 1280x960 window
// with a 2x2 viewport grid (four 640x480 viewports; T top-left, C top-right,
// S bottom-left, 3D bottom-right, per SPEC FR-app.2). The Transverse (constant
// Z), Coronal (constant Y) and Sagittal (constant X) views show a slice of the
// volume EXTRACTED ON THE GPU (the broker stack wires render::VolumeSliceRenderer,
// which samples the cached R32F 3D texture exactly where each pixel ray crosses
// that view's clip plane, texel mapping (idx+0.5)/dim), plus the plane∩mesh
// contour overlay computed on the GPU by the geometry-shader contour renderer.
//
// Rendering architecture (T20, SPEC §11 ACL): the ENTIRE scene is scene/ values
// in ONE broker::AppContext composition root — three VolumeSliceObjects (one
// per axis display frame), three ContourObjects (the golden box outline in
// each display frame), one MeshObject (the golden box for the 3D view) — and
// four scene::View values. Each 2D view carries its extraction plane in
// VOXEL-INDEX space (the broker contextual rule: the View owns the plane);
// broker::VolumeSliceObjectMapper converts it to world through the PlaneMapper
// voxel-center rule against the object's OWN display-frame transform, so the
// mapped plane is exactly the display-z = heldIndex + 0.5 plane the previous
// direct composition baked in by hand. Every frame drives the IViewBridge
// façade (sync → renderAll → presentAll); the sample never includes render/
// and never holds a mapper or renderer handle.
//
// Scrolling: every kFramesPerStep frames the round-robin next axis advances
// one voxel layer. The scroll mutates only scene values (view planes +
// contour planes + the 3D crosshair camera); the poll path sees the bumped
// generations and re-translates exactly the dirty fields — scrolling touches
// no pixels on the CPU (the CPU oracle app::makeSliceImage is retained ONLY
// as the gate tests' reference implementation, never called here).
//
// Exits cleanly (code 0) after RE_SAMPLE_MAX_FRAMES frames (default 300) so
// the gate can run it headlessly under Xvfb within a timeout (FR-app.1).
//
// Live window size (T23): the window OPENS at the SPEC 1280x960, but the 2x2
// grid is re-resolved from the LIVE framebuffer dims every frame via
// app::mprViewports(width, height) (four equal quadrants of the current size)
// and the 3D view's camera aspect follows its live quadrant — a resize
// re-splits the layout instead of stretching stale rects.

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

#include "app/ct_transfer_function.hpp"
#include "app/mpr_slice.hpp"
#include "app/sample_harness.hpp"
#include "broker/app_context.hpp"
#include "broker/slice_display.hpp"
#include "core/draw.hpp"
#include "core/window.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "io/volume/nrrd_volume_loader.hpp"
#include "utils/pixel_reader.hpp"
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

// MPR window size via app::kMprWindowWidth/kMprWindowHeight (AS2) — the
// OPENING size only; live grid re-resolves from framebuffer dims. Frame count
// via app::kDefaultFrames; CT ramp via the shared factory in
// app/ct_transfer_function.hpp (AS1 single definition).
// Auto-scroll cadence: every
// kFramesPerStep rendered frames the round-robin next slice axis advances one
// voxel layer. Deterministic, and slow enough that the scroll is watchable
// interactively while a bounded headless smoke run (RE_SAMPLE_MAX_FRAMES=20)
// stays on the initial mid-volume state.
constexpr std::uint32_t kFramesPerStep = 45u;

// The golden box mesh (FR-app.3): a box spanning these voxel-index bounds,
// inside the 128x128x70 CT volume, so every slice view's plane intersects it
// at ANY slice index (the Transverse/Coronal/Sagittal contours are the box's
// cross-section rectangles).
constexpr glm::vec3 kGoldenBoxMin(32.0f, 32.0f, 10.0f);
constexpr glm::vec3 kGoldenBoxMax(96.0f, 96.0f, 60.0f);

// The 3D-view box material: a clean solid color mapping to exact RGBA8 bytes
// (0.2*255=51, 0.4*255=102, 0.8*255=204). Opaque (alpha 1.0). Under the v1
// flat +Z lighting the +Z face of the box shades to exactly this color; faces
// with normals not aligned to +Z shade to black (docs/render.md / docs/mpr.md).
constexpr glm::vec4 kBoxMaterialColor(0.2f, 0.4f, 0.8f, 1.0f);

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

/// The axis-permutation display models shared with the MPR gates: they place
/// the CONTOUR geometry (already in voxel-index units) into each view's
/// display frame so the displayed free axes are always display (x, y) —
/// Transverse identity, Coronal swaps Y/Z, Sagittal maps (x,y,z)->(y,z,x).
/// glm::mat4's constructor takes COLUMNS, so each initializer list is read
/// DOWN the matrix (a transposed permutation silently misplaces the Sagittal
/// geometry — the T11 review finding these literal matrices were pinned
/// against).
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

/// The held coordinate of each view's slice plane (voxel-index units, through
/// the sliced voxel layer's centers) under `state`, in axis order
/// {Transverse, Coronal, Sagittal}.
std::array<float, 3> heldCoords(const app::MprSliceState& state) {
    return {static_cast<float>(state.transverseZ) + 0.5f,
            static_cast<float>(state.coronalY) + 0.5f,
            static_cast<float>(state.sagittalX) + 0.5f};
}

/// The RAW voxel-layer index each 2D view holds under `state` (no +0.5 center
/// offset — the PlaneMapper conversion adds it).
std::array<float, 3> rawIndices(const app::MprSliceState& state) {
    return {static_cast<float>(state.transverseZ),
            static_cast<float>(state.coronalY),
            static_cast<float>(state.sagittalX)};
}

/// The MPR sample: owns the volume + transfer function + AppContext and
/// renders the 2x2 grid through the bridge every frame.
class MPRView final : public app::ISample {
   public:
    MPRView(data::VolumeDataset dataset, volume::TransferFunction tf)
        : dataset_(std::make_shared<data::VolumeDataset>(std::move(dataset))),
          tf_(std::move(tf)),
          sliceState_(makeInitialSliceState(*dataset_)),
          box_(std::make_shared<const data::Mesh>(
              app::makeBoxMesh(kGoldenBoxMin, kGoldenBoxMax))),
          ctx_(broker::AppContext::Params{}) {
        // --- Scene values ---------------------------------------------------
        // Three volume-slice objects: each carries the shared dataset ref +
        // TF and ITS axis display-frame transform (voxel-center index i ->
        // display coordinate i + 0.5 on the free axes). Store ids start at 1
        // and are sequential in add order.
        const std::array<MprAxis, 3> axes = {MprAxis::Transverse,
                                             MprAxis::Coronal,
                                             MprAxis::Sagittal};
        for (std::size_t i = 0; i < 3u; ++i) {
            scene::VolumeSliceObject vs;
            vs.volume = dataset_;
            vs.transferFunction = tf_;
            vs.transform = app::sliceVolumeModel(*dataset_, axes[i]);
            sliceIds_[i] = ctx_.store().addVolumeSliceObject(std::move(vs));
        }

        // The golden box mesh (FR-app.3) for the 3D view, opaque material.
        scene::MeshObject box;
        box.mesh = box_;
        box.transform = glm::mat4(1.0f);
        box.presentation.phong.baseColor = kBoxMaterialColor;
        boxId_ = ctx_.store().addMeshObject(std::move(box));

        // Three contour objects: the box outline in each display frame, red
        // stroke (FR-app.3 exact bytes 255,0,0,255). Their planes live ON the
        // object (a contour is meaningless without its own plane) and are
        // updated per frame from the slice state.
        for (std::size_t i = 0; i < 3u; ++i) {
            scene::ContourObject co;
            co.mesh = box_;
            co.transform = axisDisplayModels()[i];
            co.plane.setNormal(glm::vec3(0.0f, 0.0f, 1.0f));
            co.plane.setPoint(glm::vec3(0.0f, 0.0f, heldCoords(sliceState_)[i]));
            co.plane.setSpace(scene::Space::World);
            co.color = app::kContourColor;
            contourIds_[i] = ctx_.store().addContourObject(std::move(co));
        }

        // --- Views ----------------------------------------------------------
        // The initial 2x2 grid at the OPENING window size; every frame
        // re-resolves it from the live pixel dims (see renderFrame).
        const std::array<app::MprViewport, 4> grid =
            app::mprViewports(app::kMprWindowWidth, app::kMprWindowHeight);
        for (std::size_t i = 0; i < 3u; ++i) {
            const auto [freeW, freeH] =
                app::sliceFreeAxes(*dataset_, axes[i]);
            views_[i].id = 10u + static_cast<uint64_t>(i);
            views_[i].rect = scene::Rect{grid[i].x, grid[i].y, grid[i].width,
                                         grid[i].height};
            views_[i].camera = broker::makeSliceCamera(
                static_cast<float>(freeW), static_cast<float>(freeH));
            views_[i].setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            views_[i].setItemIds({sliceIds_[i], contourIds_[i]});
        }
        views_[3].id = 13u;
        views_[3].rect = scene::Rect{grid[3].x, grid[3].y, grid[3].width,
                                     grid[3].height};
        views_[3].setClearColor(glm::vec4(0.10f, 0.10f, 0.14f, 1.0f));
        views_[3].setItemIds({boxId_});

        grid_ = grid;
        applySliceState();
    }

    /// The resize hook: re-split the 2x2 grid over the new pixel size and
    /// push the state (incl. the 3D camera's live quadrant aspect) so the
    /// very next frame already carries the corrected layout.
    void onResize(int width, int height) noexcept override {
        resolveLiveGrid(width, height);
        applySliceState();
    }

    data::Result<void> renderFrame(int width, int height) override {
        // Clear the window's default framebuffer behind the viewport grid
        // (a background, not a viewport blend — the views themselves are
        // placed by the engine blit).
        core::bindDefaultFramebuffer();
        core::setViewport(0, 0, width, height);
        core::setClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        core::clearColor();

        // Re-resolve the 2x2 grid from THIS frame's pixel dims first: four
        // equal quadrants of the live window (change-guarded setters make a
        // no-resize frame free), then the slice state below derives its
        // cameras against the current quadrant sizes.
        resolveLiveGrid(width, height);

        // Advance the deterministic auto-scroll: mutates ONLY the slice state
        // (three integers). Everything downstream derives from it below.
        advanceSliceState();

        // Push the CURRENT slice state into the scene values: view planes
        // (voxel-index, converted by the broker), contour planes (world,
        // display-frame), cameras (slice extents + crosshair interplay).
        applySliceState();

        // Bridge façade via the single-site helper (AS2 syncRenderPresent):
        // sync → renderAll → presentAll blits each target 1:1 into its pinned
        // window rect. The helper is the ONE site for the triple previously
        // pasted into six renderFrame implementations (the andThen chain is
        // equivalent to the helper's sequential checks).
        frame_.assign(views_.begin(), views_.end());
        auto presented = app::syncRenderPresent(ctx_, frame_);
        if (presented.failed()) {
            return presented;
        }
        // Optional single-frame capture of the composed window content
        // (defect-verification aid): with RE_SAMPLE_DUMP_FRAME=<path> set,
        // the FIRST frame is written as a binary PPM (P6) so the live window
        // path can be verified pixel-wise. Off by default and free when unset
        // (pixels are read through utils::PixelReader, which delegates to the
        // core/ readback anchor; no raw readback call lives in app/).
        if (!frameDumped_) {
            frameDumped_ = true;
            const char* dumpPath = std::getenv("RE_SAMPLE_DUMP_FRAME");
            if (dumpPath != nullptr && dumpPath[0] != '\0') {
                auto dumped = dumpWindowFramePpm(
                    dumpPath, static_cast<std::uint32_t>(width),
                    static_cast<std::uint32_t>(height));
                if (dumped.ok()) {
                    spdlog::info("mpr sample: first-frame window capture "
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
               "The window opens at 1280x960 with four equal viewports in a "
               "2x2 grid:\n"
               "T (top-left) = Transverse plane (constant Z) + GPU mesh "
               "contour,\n"
               "C (top-right) = Coronal plane (constant Y) + GPU mesh "
               "contour,\n"
               "S (bottom-left) = Sagittal plane (constant X) + GPU mesh "
               "contour,\n"
               "3D (bottom-right) = the golden box mesh, viewed from the "
               "slice-state crosshair.\n"
               "The whole scene is mediated by the broker (AppContext + "
               "IViewBridge); the slices are EXTRACTED ON THE GPU from the "
               "cached 3D texture: every few frames the next axis advances "
               "one voxel layer and all views track it.\n"
               "Resize check: drag a window edge — the grid re-splits into "
               "four equal quadrants of the live pixel size, no stale rects.\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    /// Re-resolve the 2x2 viewport grid from live pixel dims (four equal
    /// quadrants of the CURRENT window via app::mprViewports) and push the
    /// rects into the views — the one body shared by the resize hook and
    /// every rendered frame. Change-guarded setters keep a no-resize frame
    /// free of generation churn.
    void resolveLiveGrid(int width, int height) {
        grid_ = app::mprViewports(width, height);
        for (std::size_t i = 0; i < views_.size(); ++i) {
            views_[i].setRect(scene::Rect{grid_[i].x, grid_[i].y,
                                          grid_[i].width, grid_[i].height});
        }
    }

    /// One deterministic auto-scroll step every kFramesPerStep frames: the
    /// round-robin next axis advances +1, wrapping at its dimension (valid
    /// voxel-layer indices are 0..dim-1). Pure integer state mutation.
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

    /// Push the CURRENT slice state into the scene values: each 2D view's
    /// plane moves to its new voxel-index layer (setPlane bumps planeGen, so
    /// the poll path re-translates exactly those fields), each contour
    /// object's world-space plane tracks its display-frame coordinate (the
    /// store push-dirty marks the change), and both cameras derive from the
    /// current state.
    void applySliceState() {
        const std::array<MprAxis, 3> axes = {MprAxis::Transverse,
                                             MprAxis::Coronal,
                                             MprAxis::Sagittal};
        const std::array<float, 3> raw = rawIndices(sliceState_);
        const std::array<float, 3> held = heldCoords(sliceState_);

        for (std::size_t i = 0; i < 3u; ++i) {
            // View plane in VOXEL-INDEX space: the held axis coordinate is
            // the raw layer index (PlaneMapper's center rule adds the +0.5).
            // The normal is already world-space (display +Z) per the
            // PlaneDesc contract; under the axis permutation the held dataset
            // axis maps to display Z, so the world plane is exactly display
            // z = heldIndex + 0.5.
            glm::vec3 voxelPoint(0.0f);
            voxelPoint[axisComponent(axes[i])] = raw[i];
            scene::PlaneDesc plane;
            plane.setNormal(glm::vec3(0.0f, 0.0f, 1.0f));
            plane.setPoint(voxelPoint);
            plane.setSpace(scene::Space::VoxelIndex);
            views_[i].setPlane(plane);

            // Contour plane in the object's LOCAL (= display) frame at the
            // held coordinate — matching the post-model evaluation, exactly
            // like the extraction plane's world result.
            scene::ContourObject* /*borrow*/ co =
                ctx_.store().getContourObjectMut(contourIds_[i]);
            if (co != nullptr) {
                scene::PlaneDesc contourPlane;
                contourPlane.setNormal(glm::vec3(0.0f, 0.0f, 1.0f));
                contourPlane.setPoint(glm::vec3(0.0f, 0.0f, held[i]));
                contourPlane.setSpace(scene::Space::World);
                if (!(co->plane == contourPlane)) {
                    co->setPlane(contourPlane);
                    ctx_.store().markDirty(contourIds_[i],
                                           scene::FieldId::Plane);
                }
            }
        }

        // Cameras: the 2D slice cameras are static per axis extents; the 3D
        // camera tracks the crosshair (the slice-state ↔ 3D-view interplay).
        // Its aspect follows the LIVE bottom-right quadrant dims, so a window
        // resize reframes the 3D view instead of stretching it.
        const float aspect3d = app::aspectFromDims(grid_[3].width,
                                                   grid_[3].height);
        views_[3].mutateCamera([&](scene::Camera& c) {
            c = broker::make3dCamera(app::sliceCrosshair(sliceState_),
                                     box_->bounds(), aspect3d);
        });
    }

    /// The dataset axis a slice view holds constant: Transverse → Z,
    /// Coronal → Y, Sagittal → X (SPEC FR-app.2 pinned convention).
    static constexpr int axisComponent(MprAxis axis) noexcept {
        switch (axis) {
            case MprAxis::Transverse:
                return 2; // z
            case MprAxis::Coronal:
                return 1; // y
            case MprAxis::Sagittal:
                return 0; // x
        }
        return 2;
    }

    /// Read the composed window content back (default framebuffer, via
    /// utils::PixelReader -> core::readRgba8) and write it as a binary PPM
    /// (P6, top-down rows). Diagnostic only: used to verify the LIVE sample
    /// path pixel-wise.
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

    std::shared_ptr<data::VolumeDataset> dataset_;
    volume::TransferFunction tf_;   // immutable value; copied into instances
    app::MprSliceState sliceState_; // the live slice state (auto-scrolled)
    std::uint32_t frameCounter_{0}; // auto-scroll clock (frames rendered)

    // The golden box mesh (FR-app.3): co-owned with the scene objects that
    // reference it (shared asset ref — no borrow, nothing to dangle).
    std::shared_ptr<const data::Mesh> box_;

    // The composition root (store + Broker + full mapper inventory + bridge).
    broker::AppContext ctx_;

    // Stable ids assigned by the store (add order: slices, box, contours).
    std::array<uint64_t, 3> sliceIds_{};
    uint64_t boxId_{0};
    std::array<uint64_t, 3> contourIds_{};

    std::array<scene::View, 4> views_{};
    /// The live 2x2 grid (re-resolved from the current pixel size each frame;
    /// the 3D camera aspect derives from its quadrant).
    std::array<app::MprViewport, 4> grid_{};
    std::vector<scene::View> frame_{};

    /// One-shot guard for the RE_SAMPLE_DUMP_FRAME diagnostic capture.
    bool frameDumped_{false};
};

} // namespace

int main() {
    // Single-site entry via app::runSample (AS2).
    return app::runSample(
        "RenderEngine - MPR Sample", app::kMprWindowWidth,
        app::kMprWindowHeight, app::kDefaultFrames,
        []() -> std::unique_ptr<app::ISample> {
            const std::string volumePath =
                std::string(RE_SOURCE_DIR) + "/data/volumes/sample_ct.nrrd";
            auto volumeResult = re::io::loadNrrdVolume(volumePath);
            if (volumeResult.failed()) {
                spdlog::error("mpr sample: failed to load '{}': {}",
                              volumePath, volumeResult.error().message);
                return nullptr;
            }
            return std::make_unique<MPRView>(std::move(*volumeResult),
                                             app::RE_CT_TF());
        });
}
