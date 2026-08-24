// app/oit_sample.cpp — order-independent transparency (OIT) sample (T19,
// FR-app.1).
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
// Per-frame composition (shared with the T19 gate through app/oit_scene.hpp
// so the tested arrangement IS the shown arrangement):
//
//   1. The opaque meshes render first through a render::View whose per-view
//      depthTest flag is ON (render::View::setDepthTest, the T18 depth
//      support): the view owns a DepthMode::Enabled target — a framebuffer
//      with a real depth attachment — so overlapping opaque geometry
//      resolves by true occlusion rather than draw order.
//   2. render::LinkedListOIT captures each transparent mesh's fragments into
//      its per-pixel linked list, sorts them by depth per pixel, and
//      composites them back-to-front with premultiplied-alpha "over" onto
//      the opaque result inside the view target (FR-render.2). The pipeline
//      is engaged because the scene contains transparent materials; an
//      opaque-only scene would never engage it (FR-render.3).
//   3. core::blit presents the finished view target to the window's default
//      framebuffer.
//
// The sample is driven through the shared app::SampleHarness (visible window
// + ImGui overlay + run loop) exactly like the other capability samples, and
// exits cleanly (code 0) after RE_SAMPLE_MAX_FRAMES frames (default 300), so
// the gate can run it headlessly under Xvfb within a timeout (FR-app.1: exit
// code 0, no sanitizer reports).

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "app/oit_scene.hpp"
#include "app/sample_harness.hpp"
#include "core/window.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "io/mesh/obj_mesh_loader.hpp"
#include "render/asset_registry.hpp"
#include "render/linked_list_oit.hpp"
#include "render/mesh_renderer.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

// The harness window size.
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
// Default number of frames before the sample exits cleanly.
constexpr int kDefaultFrames = 300;

/// The OIT sample: owns the shared scene rig (meshes + materials + handles),
/// the occlusion-capable view, and the injected LinkedListOIT pipeline, and
/// composes one frame per renderFrame call.
class OitSample final : public re::app::ISample {
   public:
    explicit OitSample(re::data::Mesh bunny)
        : registry_{std::make_shared<re::render::AssetRegistry>()},
          rig_{registry_, std::move(bunny)},
          renderer_{std::make_shared<re::render::MeshRenderer>(registry_)},
          pipeline_{std::make_shared<re::render::LinkedListOIT>()},
          view_{re::render::ViewRect{0, 0, kWindowWidth, kWindowHeight},
                re::app::oit_scene::kClearColor} {
        // Depth-enabled composition (T18 consumption): this view renders its
        // opaque layer with the depth test ON into a target that physically
        // owns a depth attachment, so the golden box and the bunny occlude
        // each other correctly regardless of draw order.
        view_.setDepthTest(true);
        // One opaque layer item, added once (View items persist across
        // frames; only rect/camera are refreshed per frame). The item
        // co-owns the renderer (shared_ptr), so it can never outlive it.
        view_.addItem(rig_.opaqueScene(), renderer_);

        if (!rig_.handlesRegistered()) {
            // Impossible in practice (registration fails only without a GL
            // context); degrade gracefully with a loud log instead of
            // crashing — the renderer skips unresolvable instances (SPEC §5).
            spdlog::error("oit sample: failed to register scene meshes");
        }
    }

    re::data::Result<void> renderFrame(int width, int height) override {
        // A fresh draw-state context per frame: the context owns the
        // dirty-flag cache + spy for the pass prologue and depth/blend
        // transitions of THIS frame only, so no state decision can bleed
        // from the previous frame (per-frame ownership beats a process-
        // global cache — the 2026-08-23 architecture decision recorded as
        // SPEC §11.6 EOL-5).
        re::core::DrawContext ctx;
        auto composed = re::app::oit_scene::composeFrame(
            view_, *pipeline_, rig_, static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height), ctx);
        if (composed.failed()) {
            return composed;
        }
        // Present the composed view target to the window's default
        // framebuffer (1:1 GL_NEAREST blit — the view rect equals the window
        // pixel size handed to us by the harness).
        return view_.blitTo(nullptr);
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
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    std::shared_ptr<re::render::AssetRegistry> registry_;
    re::app::oit_scene::Rig rig_;
    std::shared_ptr<re::render::MeshRenderer> renderer_;
    std::shared_ptr<re::render::LinkedListOIT> pipeline_;
    re::render::View view_;
};

} // namespace

int main() {
    const std::string meshPath =
        std::string(RE_SOURCE_DIR) + "/data/meshes/bunny.obj";
    auto meshResult = re::io::loadObjMesh(meshPath);
    if (meshResult.failed()) {
        spdlog::error("oit sample: failed to load '{}': {}", meshPath,
                      meshResult.error().message);
        return 1;
    }

    auto windowResult = re::core::Window::create(kWindowWidth, kWindowHeight,
                                                 "RenderEngine - OIT Sample");
    if (windowResult.failed()) {
        spdlog::error("oit sample: {}", windowResult.error().message);
        return 1;
    }

    auto sample =
        std::make_unique<OitSample>(std::move(*meshResult));
    re::app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.run(re::app::sampleMaxFrames(kDefaultFrames));
}
