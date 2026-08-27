// app/oit_sample.cpp — order-independent transparency (OIT) sample (T19,
// FR-app.1; T20 routed through the broker façade).
//
// Demonstrates the transparency / OIT capability (SPEC §1 capability 5,
// FR-render.2/3) on a scene of REAL meshes instead of flat quads:
//
//   * TWO OPAQUE meshes — a golden box (flat-shaded shell built by
//     app::makeBoxMesh) and the Stanford bunny loaded from
//     data/meshes/bunny.obj (SPEC §7) — placed at different depths;
//   * TWO TRANSPARENT glass boxes (alpha 0.5, red near + blue far) at two
//     more depths, arranged so every glass footprint covers both opaque
//     meshes somewhere and the glasses cover each other.
//
// Per-frame composition through the broker bridge (the shared analytic
// constants live in app/oit_scene.hpp so the tested arrangement IS the shown
// arrangement):
//
//   1. The scene is four MeshObject values in one AppContext composition root;
//      the single full-window view opts into depth-tested rendering
//      (scene::View::setDepthTest — the T18 depth support): its render-side
//      target owns a real depth attachment and its pass prologue enables +
//      clears depth, so overlapping opaque geometry resolves by TRUE OCCLUSION
//      rather than draw order.
//   2. The context is constructed with enableOIT: transparent mesh instances
//      (alpha < 1) are routed by the synchronizer OUT of the inline layers to
//      ViewCompositor's capture stage; after each view's opaque pass it runs
//      begin() → one drawTransparent per glass instance → end(), compositing
//      depth-sorted premultiplied fragments back-to-front over the opaque
//      result inside the same view target (FR-render.2). The pipeline engages
//      because the scene contains transparent materials; an opaque-only scene
//      would never capture (FR-render.3).
//   3. presentAll blits the finished view target to the window's default
//      framebuffer.
//
// Exits cleanly (code 0) after RE_SAMPLE_MAX_FRAMES frames (default 300) so
// the gate can run it headlessly under Xvfb within a timeout (FR-app.1).
//
// Live window size (T23): the view's rect and the arrangement camera's aspect
// are re-derived from the harness pixel dims EVERY frame (the ortho window
// grows/shrinks horizontally with width/height), so a window resize reframes
// the composition instead of stretching it.

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/mpr_slice.hpp" // app::makeBoxMesh
#include "app/oit_scene.hpp"
#include "app/sample_harness.hpp"
#include "broker/app_context.hpp"
#include "core/window.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "io/mesh/obj_mesh_loader.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

namespace app = re::app;
namespace broker = re::broker;
namespace core = re::core;
namespace data = re::data;
namespace oit = re::app::oit_scene;
namespace scene = re::scene;

// Window size and frame count are shared via app::kWindowWidth etc. (AS2
// constants dedup — the OPENING size only; live dims drive every frame).

/// The OIT sample: owns the loaded bunny + the AppContext composition root and
/// drives one bridged frame per renderFrame call.
class OitSample final : public app::ISample {
   public:
    explicit OitSample(data::Mesh bunny)
        : bunny_(std::make_shared<const data::Mesh>(std::move(bunny))),
          // enableOIT wires the linked-list pipeline into the stack: the
          // synchronizer routes alpha<1 mesh instances to the compositor's
          // capture stage instead of inline layers. registerCameraMapper is
          // OFF because this view's pixel contract is an ORTHOGRAPHIC 3D
          // framing (no clip plane); CameraMapper's T4 validation would reject
          // that pairing with a typed error even though the matrices are the
          // pinned analytic ones.
          ctx_(broker::AppContext::Params{.enableOIT = true,
                                          .registerCameraMapper = false}) {
        const data::Aabb& bunnyBounds = bunny_->bounds();

        // Opaque layer values first (draw order within the pass), then the
        // transparent set — the compositor captures them out-of-band anyway,
        // but keeping store order aligned with the documented arrangement
        // makes the scene readable. Capture returned ObjectIds (AS3) instead
        // of hardcoding {1,2,3,4} — stable across store policy changes.
        const uint64_t idGold = ctx_.store().addMeshObject(
            makeBoxObject(oit::kGoldMin, oit::kGoldMax, oit::kGoldColor));
        const uint64_t idBunny =
            ctx_.store().addMeshObject(makeBunnyObject(bunnyBounds));
        const uint64_t idNear = ctx_.store().addMeshObject(makeBoxObject(
            oit::kNearGlassMin, oit::kNearGlassMax, oit::kNearGlassColor));
        const uint64_t idFar = ctx_.store().addMeshObject(makeBoxObject(
            oit::kFarGlassMin, oit::kFarGlassMax, oit::kFarGlassColor));

        view_.id = 1;
        applyLiveDims(app::kWindowWidth, app::kWindowHeight);
        view_.setClearColor(oit::kClearColor);
        view_.setDepthTest(true);
        view_.setItemIds({idGold, idBunny, idNear, idFar});
    }

    /// The resize hook: apply the new pixel dims immediately so the very next
    /// frame (and its sync) already carries the corrected rect + aspect.
    void onResize(int width, int height) noexcept override {
        applyLiveDims(width, height);
    }

    data::Result<void> renderFrame(int width, int height) override {
        // Live aspect from THIS frame's harness pixel size first (the
        // arrangement camera is aspect-corrected so no window shape stretches
        // the scene; change-guarded setters make a no-resize frame free),
        // then the single-site bridge façade (AS2 syncRenderPresent).
        applyLiveDims(width, height);
        views_ = {view_};
        return app::syncRenderPresent(ctx_, views_);
    }

    const char* title() const override {
        return "OIT sample: opaque meshes under two glass boxes (linked-list)";
    }

    const char* instructions() const noexcept override {
        return "Capability: order-independent transparency (FR-render.2/3).\n"
               "Two OPAQUE meshes (golden box + Stanford bunny at different "
               "depths) render first with true depth occlusion into a "
               "depth-buffer-backed target.\n"
               "Two TRANSPARENT glass boxes (red near, blue far, alpha 0.5) "
               "interleave both opaques along the view direction: their "
               "fragments are captured into a per-pixel linked list, sorted "
               "by depth, and composited back-to-front over the opaque image "
               "— correct regardless of draw order.\n"
               "Resize check: drag a window edge — the composition reframes "
               "to the live pixel size (ortho extents follow width/height), "
               "no stretching.\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    /// Re-derive the full-window view rect + the arrangement camera's aspect
    /// from live pixel dims — the one body shared by the resize hook and every
    /// rendered frame. The ortho window keeps NDC [-1,1] vertically and grows
    /// to ±aspect horizontally (the oit_scene::cameraFor contract), so no
    /// window shape stretches the composition.
    void applyLiveDims(int width, int height) {
        view_.setRect(scene::Rect{0, 0, width, height});
        view_.mutateCamera([&](scene::Camera& c) {
            c = oit::cameraFor(app::aspectFromDims(width, height));
        });
    }

    /// A flat-shaded axis-aligned box MeshObject value (identity transform —
    /// extents are baked into the mesh geometry).
    static scene::MeshObject makeBoxObject(const glm::vec3& minCorner,
                                           const glm::vec3& maxCorner,
                                           const glm::vec4& color) {
        scene::MeshObject obj;
        obj.mesh = std::make_shared<const data::Mesh>(
            app::makeBoxMesh(minCorner, maxCorner));
        obj.transform = glm::mat4(1.0f);
        obj.presentation.phong.baseColor = color;
        return obj;
    }

    /// The bunny MeshObject value at its scaled/centered transform.
    scene::MeshObject makeBunnyObject(const data::Aabb& bounds) {
        scene::MeshObject obj;
        obj.mesh = bunny_;
        obj.transform = oit::bunnyModel(bounds);
        obj.presentation.phong.baseColor = oit::kBunnyColor;
        return obj;
    }

    std::shared_ptr<const data::Mesh> bunny_;
    broker::AppContext ctx_;
    scene::View view_{};
    std::vector<scene::View> views_{};
};

} // namespace

int main() {
    // Single-site entry via app::runSample (AS2: load→window→run dedup). The
    // factory encapsulates the bunny load so the main is one call; window size
    // and frame count are the shared constants (AS2).
    return app::runSample(
        "RenderEngine - OIT Sample", app::kWindowWidth, app::kWindowHeight,
        app::kDefaultFrames, []() -> std::unique_ptr<app::ISample> {
            const std::string meshPath =
                std::string(RE_SOURCE_DIR) + "/data/meshes/bunny.obj";
            auto meshResult = re::io::loadObjMesh(meshPath);
            if (meshResult.failed()) {
                spdlog::error("oit sample: failed to load '{}': {}", meshPath,
                              meshResult.error().message);
                return nullptr;
            }
            return std::make_unique<OitSample>(std::move(*meshResult));
        });
}
