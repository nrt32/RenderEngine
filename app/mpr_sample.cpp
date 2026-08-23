// app/mpr_sample.cpp — Multi-Planar Reconstruction (MPR) sample (T14/T15,
// FR-app.2/3; T5 V3.4 drives its composition through ReView/ViewTarget/IRenderable,
// SPEC §3.2; V3.8b T11 moves the contour overlay to the GPU ContourRenderer).
//
// Demonstrates the MPR capability (SPEC §1 goal 6): a single 1280x960 window
// with a 2x2 viewport grid (four 640x480 viewports; T top-left, C top-right,
// S bottom-left, 3D bottom-right, per SPEC FR-app.2). The Transverse (constant
// Z), Coronal (constant Y) and Sagittal (constant X) views render the volume
// slice along their pinned axis (SPEC §4 FR-app.2); the 3D view (bottom-right)
// renders the golden box mesh (FR-app.3). Each slice view also overlays the
// plane∩mesh cross-section contour (FR-app.3): the box's intersection with
// the view's slice plane, computed ON THE GPU by render::ContourRenderer's
// geometry shader and layered over the slice image as a second ReView item —
// no CPU rasterization pass (the former CPU contour image-copy overlay of
// app/mpr_contour.* is gone).
//
// Rendering architecture (app-level composition, SPEC §3):
//   - the 2x2 viewport layout and the per-axis slice sampling come from the
//     shared app/mpr_slice scaffolding (mprViewports / makeSliceImage), which
//     the T14 gate tests headlessly;
//   - each slice view's GPU contour is translated scene→render through the
//     broker mediation registry (SPEC §11): the scene-side ContourObject
//     carries the mesh pointer + abstract PlaneDesc + color; the registered
//     mapper produces the RE-minimal render::ContourObject{AssetHandle,
//     ClipPlane, color};
//   - each slice view's textured slice image goes through the SAME mediation
//     (V3.4b T12): the scene-side PlaneObject carries only {image asset ref,
//     transform, presentation}; the registered PlaneMapper binds the shared
//     unit quad geometry and produces the render::PlaneInstance that
//     render::PlaneRenderer draws. app/ never names the RE-side quad geometry
//     and never parses quad corners/UVs into vertex buffers — every
//     textured-plane draw reaches the GPU exclusively through PlaneRenderer's
//     own unit-quad VAO (plane.vert/frag.glsl), composed by ReView's
//     drawLayer list and presented by core::blit.
//   - the 3D-view camera comes from app::make3dCamera (app/mpr_camera.hpp),
//     which looks at the slice-state crosshair;
//   - the sample shares per-view window-section handles (render::ViewRect
//     from mprViewports) + ReView objects (render::View per screen section
//     owning ViewTarget + IRenderable list via drawLayer, T5 V3.4) with the
//     engine: each ReView renders its IRenderables into its OWN 640×480
//     ViewTarget (Texture2D+Framebuffer) via DrawContext, then blits each FBO
//     into its window rect via core::blit. There is NO app-side viewport
//     blending and no ViewRenderer: the textured-quad present pass is gone,
//     the engine present (View::blitTo) is the whole composition (T5).
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
#include "broker/plane_mapper.hpp"
#include "core/draw.hpp"
#include "core/window.hpp"
#include "data/image.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "io/volume/nrrd_volume_loader.hpp"
#include "render/asset_registry.hpp"
#include "render/contour_renderer.hpp"
#include "render/mesh_renderer.hpp" // render::MeshScene / render::Camera
#include "render/phong_material.hpp"
#include "render/plane_renderer.hpp"
#include "render/view.hpp"
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

/// The orthographic camera for one 2D slice view and the model matrix scaling
/// the shared unit quad onto that view's slice image are SHARED SCAFFOLDING
/// (app::makeSliceCamera / app::makeSliceModel in app/mpr_camera.hpp): the
/// gate tests drive the exact same functions, so a sample-vs-test camera
/// divergence like the T11 user-verified defect (a slice camera whose clip
/// volume excluded the contour geometry — every GPU outline quad silently
/// clipped away) cannot reintroduce itself.

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
          boxMaterial_(kBoxMaterialColor) {
        // Each slice view's textured layer is expressed scene-side as a
        // PlaneObject{image asset ref, transform} whose transform scales the
        // shared unit quad onto that view's image pixel rectangle
        // (app::makeSliceModel), viewed through the per-view orthographic
        // camera (app::makeSliceCamera) — the shared 2D-view camera/model
        // scaffolding (app/mpr_camera.hpp). The RE-side instance is produced
        // by broker::PlaneMapper below; app/ never touches the quad geometry
        // itself (V3.4b T12: no CPU quad parsing outside render/).

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

        // Build each slice view's GPU contour layer (FR-app.3): a scene-side
        // ContourObject carrying the box, translated scene→render through the
        // broker mediation layer (SPEC §11) into the RE-minimal
        // render::ContourObject{AssetHandle, ClipPlane}. The outline itself
        // (plane∩mesh segments + thick-line expansion) is computed ON THE GPU
        // by render::ContourRenderer's geometry shader at draw time; there is
        // no CPU rasterization pass.
        //
        // Mediation discipline: the composition root registers the contour
        // mapper in the Broker registry ONCE (below) and afterwards translates
        // only through the type-erased IMapper interface fetched from the
        // Broker — app never holds a concrete mapper handle.
        //
        // Display-space alignment: every slice view shares the same ortho
        // down-Z camera and the quad model that maps its slice image onto the
        // viewport (app::makeSliceCamera / app::makeSliceModel, shared
        // scaffolding in app/mpr_camera.hpp), so the image's pixel space is
        // [0,W]x[0,H] at z=0 for ALL views. Each view's contour
        // object therefore carries the axis-permutation MODEL that maps mesh
        // voxel-index space into ITS image space (Transverse identity;
        // Coronal swaps Y/Z; Sagittal maps (x,y,z)->(y,z,x)), and a clip plane
        // already expressed in that local/display frame — matching
        // render::ClipPlane's post-model evaluation in the shader. The
        // constant-Z display plane then cuts exactly the voxel layer the image
        // shows, so the GPU outline lands pixel-exact on the displayed slice.
        const float heldCoord[3] = {
            static_cast<float>(sliceState_.transverseZ) + 0.5f,
            static_cast<float>(sliceState_.coronalY) + 0.5f,
            static_cast<float>(sliceState_.sagittalX) + 0.5f};
        // Axis-permutation model per view (column-major glm::mat4 — the
        // constructor takes COLUMNS, so each initializer list below is read
        // down the matrix, not across; getting this wrong silently transposes
        // the permutation, which the cubic golden box of the direct-render
        // gate cannot see but the sample's non-cubic box immediately shows as
        // a misplaced/clipped Sagittal outline — T11 review finding 2):
        //   Transverse: identity — display (x,y) = voxel (x,y).
        //   Coronal:    swap Y/Z  — display (x,y) = voxel (x,z).
        //   Sagittal:   (x,y,z)->(y,z,x) — display (x,y) = voxel (y,z).
        const std::array<glm::mat4, 3> axisModel = {
            glm::mat4(1.0f),
            // Coronal: rows (x'|y'|z') = (x|z|y) => columns (1,0,0)(0,0,1)(0,1,0)
            glm::mat4(1.0f, 0.0f, 0.0f, 0.0f, // col0: row0 gets x
                      0.0f, 0.0f, 1.0f, 0.0f, // col1: row2 gets y -> y' = z
                      0.0f, 1.0f, 0.0f, 0.0f, // col2: row1 gets z -> z' = y
                      0.0f, 0.0f, 0.0f, 1.0f),
            // Sagittal: rows (x'|y'|z') = (y|z|x) => reading the matrix DOWN,
            // columns = (0,0,1)(1,0,0)(0,1,0) — NOT the cyclic shift
            // (0,1,0)(0,0,1)(1,0,0), which encodes the transposed (z,x,y)
            // permutation that put the live Sagittal outline half off-screen.
            glm::mat4(0.0f, 0.0f, 1.0f, 0.0f, // col0: row2 gets x -> z' = x
                      1.0f, 0.0f, 0.0f, 0.0f, // col1: row0 gets y -> x' = y
                      0.0f, 1.0f, 0.0f, 0.0f, // col2: row1 gets z -> y' = z
                      0.0f, 0.0f, 0.0f, 1.0f)};
        // Composition root: register the mappers with the Broker registry
        // (one mapper per AppT, OCP via type_index — SPEC §11): ContourMapper
        // for the contour overlay (V3.8b T11) and PlaneMapper for the
        // textured slice layers (V3.4b T12).
        broker_.registerMapper(std::make_unique<broker::ContourMapper>(&registry_));
        broker_.registerMapper(std::make_unique<broker::PlaneMapper>());
        auto* contourMapper =
            broker_.get<scene::ContourObject, render::ContourObject>();
        auto* planeMapper =
            broker_.get<scene::PlaneObject, render::PlaneInstance>();

        constexpr std::array<const char*, 3> kAxisNames = {"Transverse",
                                                           "Coronal",
                                                           "Sagittal"};

        // Translate each slice view's textured layer scene→render through the
        // type-erased IMapper interface fetched from the Broker (app never
        // holds a concrete mapper handle). The mapped render::PlaneInstance
        // borrows this sample's image members and PlaneMapper's shared unit
        // quad — both outlive every draw below.
        const std::array<const data::Image*, 3> sliceImages = {
            &transverseImage_, &coronalImage_, &sagittalImage_};
        for (std::size_t i = 0u; i < 3u; ++i) {
            scene::PlaneObject appPlane;
            appPlane.image = sliceImages[i];
            appPlane.transform = app::makeSliceModel(*sliceImages[i]);
            scene::TranslateContext planeCtx;
            auto mappedPlane = planeMapper->map(appPlane, planeCtx);
            if (mappedPlane.failed()) {
                // Typed errors are surfaced, never swallowed: a missing slice
                // layer would look exactly like a black viewport (same
                // discipline as the contour translation below).
                spdlog::error(
                    "mpr sample: {} slice-plane translation failed (code {})"
                    ": {} — the view will show NO slice image",
                    kAxisNames[i], mappedPlane.error().code,
                    mappedPlane.error().message);
                continue;
            }
            sliceScenes_[i].planes.push_back(*mappedPlane);
        }

        for (std::size_t i = 0u; i < 3u; ++i) {
            scene::ContourObject appContour;
            appContour.mesh = &box_;
            appContour.transform = axisModel[i];
            // The clip plane in the object's local (= display) frame: constant
            // Z at the sliced voxel layer's coordinate.
            appContour.plane.setNormal(glm::vec3(0.0f, 0.0f, 1.0f));
            appContour.plane.setPoint(glm::vec3(0.0f, 0.0f, heldCoord[i]));
            appContour.color = app::kContourColor;

            scene::TranslateContext ctx;
            auto mapped = contourMapper->map(appContour, ctx);
            if (mapped.failed()) {
                // Typed errors are surfaced, never swallowed: a skipped
                // contour layer is visually indistinguishable from "no
                // contour", so the log names the view axis and the typed code
                // (T11 review checklist item 4).
                spdlog::error(
                    "mpr sample: {} contour translation failed (code {}): {}"
                    " — the view will show NO contour overlay",
                    kAxisNames[i], mapped.error().code,
                    mapped.error().message);
                continue;
            }
            contourScenes_[i].contours.push_back(*mapped);
        }
    }

    data::Result<void> renderFrame(int width, int height) override {
        // Clear the window's default framebuffer behind the viewport grid
        // (a background, not a viewport blend — the views themselves are
        // placed by the engine blit).
        core::bindDefaultFramebuffer();
        core::setViewport(0, 0, width, height);
        core::setClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        core::clearColor();

        // Build the four views: per-view window-section handles (ViewRects
        // from app::mprViewports, SPEC FR-app.2) + ReView per screen section
        // (T5 V3.4). Each ReView owns its ViewTarget + IRenderable list via
        // drawLayer; no ViewRenderer.
        const std::array<app::MprViewport, 4> grid =
            app::mprViewports(width, height);

        // Create 4 ReViews with their ViewTargets and renderables.
        // Three slice views (T/C/S) + one 3D box view.
        // Use DrawContext per View for per-frame cache isolation. The slice
        // layers are the broker-translated PlaneObject instances built once
        // in the constructor (the held slice images are static per run).
        std::vector<render::View> views;
        views.reserve(4u);
        const std::array<const data::Image*, 3> sliceImages = {
            &transverseImage_, &coronalImage_, &sagittalImage_};
        for (std::size_t i = 0u; i < 3u; ++i) {
            render::ViewRect rect{grid[i].x, grid[i].y, grid[i].width, grid[i].height};
            render::View view(rect, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            view.setCamera(app::makeSliceCamera(*sliceImages[i]));
            if (!sliceScenes_[i].planes.empty()) {
                view.addItem(sliceScenes_[i], &sliceRenderer_);
            }
            // Second layer: the GPU-computed plane∩mesh contour over the
            // slice (FR-app.3). View::render draws layers without clearing,
            // so the contour strokes overwrite exactly their own pixels.
            if (!contourScenes_[i].contours.empty()) {
                view.addItem(contourScenes_[i], &contourRenderer_);
            }
            core::DrawContext ctx;
            auto r = view.renderWithEnsure(ctx);
            if (r.failed()) return r;
            auto b = view.blitTo(nullptr);
            if (b.failed()) return b;
            views.push_back(std::move(view));
        }
        // 3D view (bottom-right): the golden box mesh (FR-app.3).
        {
            render::ViewRect rect{grid[3].x, grid[3].y, grid[3].width, grid[3].height};
            render::View view(rect, glm::vec4(0.10f, 0.10f, 0.14f, 1.0f));
            view.setCamera(boxCamera_);
            view.addItem(boxScene_, &boxRenderer_);
            core::DrawContext ctx;
            auto r = view.renderWithEnsure(ctx);
            if (r.failed()) return r;
            auto b = view.blitTo(nullptr);
            if (b.failed()) return b;
            views.push_back(std::move(view));
        }

        // Optional single-frame capture of the composed window content
        // (T11 user-verified defect verification aid): with
        // RE_SAMPLE_DUMP_FRAME=<path> set, the FIRST frame is written as a
        // binary PPM (P6) so the live window path — the exact composition the
        // interactive sample shows — can be verified pixel-wise without a
        // display-side screenshot tool. Off by default and free when unset
        // (pixels are read through utils::PixelReader, which delegates to the
        // core/ readback anchor; no raw readback call lives in app/).
        if (!frameDumped_) {
            frameDumped_ = true;
            const char* dumpPath = std::getenv("RE_SAMPLE_DUMP_FRAME");
            if (dumpPath != nullptr && dumpPath[0] != '\0') {
                auto dumped = dumpWindowFramePpm(dumpPath,
                                                 static_cast<std::uint32_t>(width),
                                                 static_cast<std::uint32_t>(height));
                if (dumped.ok()) {
                    spdlog::info("mpr sample: first-frame window capture "
                                 "written to {}", dumpPath);
                } else {
                    spdlog::error("mpr sample: frame capture failed: {}",
                                  dumped.error().message);
                }
            }
        }
        return data::Result<void>(data::value);
    }

    const char* title() const override {
        return "MPR sample: 2x2 viewport grid (T/C/S slices + contours + 3D)";
    }

    const char* instructions() const noexcept override {
        return "Capability: Multi-Planar Reconstruction (SPEC FR-app.2/3).\n"
               "A single 1280x960 window shows four 640x480 viewports in a "
               "2x2 grid:\n"
               "T (top-left) = Transverse slice (constant Z) + GPU mesh "
               "contour,\n"
               "C (top-right) = Coronal slice (constant Y) + GPU mesh "
               "contour,\n"
               "S (bottom-left) = Sagittal slice (constant X) + GPU mesh "
               "contour,\n"
               "3D (bottom-right) = the golden box mesh, viewed from the "
               "slice-state crosshair (the intersection of the three slice "
               "planes).\n"
               "Contours are computed on the GPU (ContourRenderer geometry "
               "shader).\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    /// Read the composed window content back (default framebuffer, via
    /// utils::PixelReader -> core::readRgba8) and write it as a binary PPM
    /// (P6, top-down rows). Diagnostic only: used by the T11 defect gate to
    /// verify the LIVE sample path pixel-wise (the readback tests render into
    /// offscreen FBOs and cannot see a window-path regression like the
    /// camera-enclosure defect that hid every contour quad).
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
        std::vector<std::uint8_t> ppm(static_cast<std::size_t>(width) *
                                      height * 3u);
        for (std::uint32_t row = 0u; row < height; ++row) {
            const std::size_t src =
                static_cast<std::size_t>(height - 1u - row) * width * 4u;
            const std::size_t dst = static_cast<std::size_t>(row) * width * 3u;
            for (std::uint32_t col = 0u; col < width; ++col) {
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
    // declared before the renderer(s) and the mapper so `&registry_` is valid
    // at their construction.
    render::AssetRegistry registry_;
    render::MeshScene boxScene_;
    render::Camera boxCamera_;
    render::MeshRenderer boxRenderer_{&registry_};

    // Per-slice-view textured slice layers (FR-app.2), translated scene→render
    // through the Broker-mediated PlaneMapper (V3.4b T12) and drawn by the
    // PlaneRenderer as the first ReView layer of each slice view. app/ holds
    // only scene::PlaneObject values — no RE-side quad geometry, no quad
    // vertex parsing (the unit-quad VAO belongs to PlaneRenderer alone).
    broker::Broker broker_;
    std::array<render::PlaneScene, 3> sliceScenes_{};
    render::PlaneRenderer sliceRenderer_;

    // Per-slice-view GPU contour layers (FR-app.3): translated scene→render
    // through the Broker-mediated contour mapper and drawn by the
    // ContourRenderer as the second ReView layer of each slice view (V3.8b
    // T11). The Broker owns the mappers; app only fetches the type-erased
    // IMapper interfaces from it (no mapper handle held).
    std::array<render::ContourScene, 3> contourScenes_{};
    render::ContourRenderer contourRenderer_{&registry_};

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
