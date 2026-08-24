#pragma once

// app/oit_scene.hpp — the OIT sample's analytic scene rig (T19).
//
// ONE shared definition of the order-independent-transparency demo scene,
// consumed by BOTH the sample executable (app/oit_sample.cpp) and its gate
// (tests/t19_oit_sample_test.cpp), so the gate asserts exactly the
// arrangement the sample shows — zero drift between them.
//
// The scene replaces the former three-coplanar-quad arrangement with REAL
// meshes interleaved along the view direction:
//
//   * TWO OPAQUE meshes — a golden box built by app::makeBoxMesh (flat-shaded
//     shell: every +Z face shades to EXACTLY its base color under the v1
//     head-on light) and the Stanford bunny loaded from data/meshes/bunny.obj
//     (SPEC §7 asset), placed at a DIFFERENT depth than the box;
//   * TWO TRANSPARENT glass boxes (alpha 0.5 => PhongMaterial::isTransparent)
//     at two more depths, nested/overlapping so that each glass box's screen
//     footprint covers BOTH opaque meshes somewhere and the two glass boxes
//     cover each other — every depth ordering the transparency pipeline must
//     handle is present in one frame.
//
// Composition contract (the "real OIT contract" of FR-render.2/3, consuming
// the T18 depth support):
//
//   1. The opaque meshes render FIRST through a render::View whose per-view
//      depthTest flag is ON (render::View::setDepthTest): the view owns a
//      DepthMode::Enabled ViewTarget (a real depth attachment) and its pass
//      prologue enables + clears the depth test, so overlapping opaque
//      geometry resolves by TRUE OCCLUSION instead of painter's draw order.
//   2. The transparent set is then captured through an injected
//      ITransparencyPipeline (LinkedListOIT): begin() -> drawTransparent()
//      per glass mesh -> end(). Capture runs with the depth test OFF — the
//      v1 pipeline semantic frozen by the T18 task ("OIT capture/composite
//      explicitly disable depth as today") — so EVERY glass fragment in a
//      column is captured, depth-sorted per pixel, and composited back-to-
//      front over the opaque result with premultiplied-alpha "over".
//   3. Because the capture does not depth-cut, a glass surface BEHIND an
//      opaque mesh still composites over it (documented v1 limitation); the
//      scene therefore places every probe column's glass surfaces strictly
//      IN FRONT of the opaque base surface so the analytic expectations are
//      exact. The bunny-in-front-of-the-far-shell relationship remains part
//      of the arrangement and is covered by the alpha==255 invariant probe.
//
// All constants below are analytic: byte expectations derive from the capture
// shader (premultiplied base color, no lighting) and the composite blend
// (docs/render.md "LinkedListOIT"); opaque bytes from the flat-shading rule
// (+Z face => shade factor 1). The full derivations live next to the gate.

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "app/mpr_slice.hpp"
#include "core/draw.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "render/asset_registry.hpp"
#include "render/imaterial.hpp"
#include "render/itransparency_pipeline.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "render/view.hpp"

namespace re::app::oit_scene {

// ---------------------------------------------------------------------------
// Analytic arrangement constants (world units; ortho [-aspect,aspect]x[-1,1]
// camera at (0,0,5) looking down -Z). See docs/render.md for the probe table.
// ---------------------------------------------------------------------------

/// Opaque golden box: a wide slab at the DEEPEST slot of the arrangement.
inline constexpr glm::vec3 kGoldMin{-0.90f, -0.75f, -0.95f};
inline constexpr glm::vec3 kGoldMax{+0.90f, +0.75f, -0.75f};
/// Its material (opaque): the +Z front face shades to exactly this color.
inline constexpr glm::vec4 kGoldColor{0.85f, 0.45f, 0.15f, 1.0f};

/// NEAR glass box (red): nearest surfaces of the whole arrangement, offset
/// LEFT so a column exists that it covers while the far shell does not.
inline constexpr glm::vec3 kNearGlassMin{-0.66f, -0.70f, +0.72f};
inline constexpr glm::vec3 kNearGlassMax{+0.34f, +0.70f, +0.92f};
/// Straight RGBA, alpha 0.5 => transparent (PhongMaterial::isTransparent).
inline constexpr glm::vec4 kNearGlassColor{0.90f, 0.20f, 0.20f, 0.50f};

/// FAR glass box (blue): behind both opaque meshes' front surfaces where the
/// probes look, offset RIGHT so it overlaps the near shell on [x=-0.14,+0.34].
inline constexpr glm::vec3 kFarGlassMin{-0.14f, -0.70f, -0.56f};
inline constexpr glm::vec3 kFarGlassMax{+0.86f, +0.70f, -0.36f};
inline constexpr glm::vec4 kFarGlassColor{0.20f, 0.35f, 0.90f, 0.50f};

/// The bunny is scaled uniformly so its LONGEST AABB side becomes exactly
/// kBunnyMaxSide world units, then centered at kBunnyCenter: its whole body
/// lands strictly BETWEEN the two shells (in front of the far shell's front
/// face at z=-0.36, behind the near shell's front face at z=+0.72) and its
/// footprint stays inside BOTH shells' footprints.
inline constexpr float kBunnyMaxSide = 0.24f;
inline constexpr glm::vec3 kBunnyCenter{+0.06f, -0.50f, -0.08f};
/// Opaque greenish tint for the bunny (never probed numerically — its smooth
/// vertex normals make the shaded color vary; only its ALPHA is asserted).
inline constexpr glm::vec4 kBunnyColor{0.30f, 0.65f, 0.40f, 1.0f};

/// Camera eye on the +Z axis; ortho maps NDC [-1,1]^2 onto the viewport with
/// horizontal extent grown to the aspect ratio (no stretch on any window).
inline constexpr glm::vec3 kEye{0.0f, 0.0f, 5.0f};
/// Near/far clip planes enclosing the whole arrangement (eye distances
/// 4.08..5.95 world units).
inline constexpr float kNearPlane = 0.1f;
inline constexpr float kFarPlane = 10.0f;

/// Frame clear color (probes never sample the background).
inline constexpr glm::vec4 kClearColor{0.0f, 0.0f, 0.0f, 1.0f};

// ---------------------------------------------------------------------------
// Rig — meshes, materials, handles, scenes, camera.
// ---------------------------------------------------------------------------

/// Owns the four CPU meshes + materials, registers them once in the shared
/// AssetRegistry (one GPU object per CPU mesh, SPEC §9 V2.5 asset sharing),
/// and builds the opaque / transparent MeshScenes. Co-owns the registry with
/// the renderer(s) it feeds (T13 shared-ownership rule: no declaration-order
/// or teardown-order hazards).
class Rig {
   public:
    /// Register all four meshes in `registry` and build the materials/scenes.
    /// `bunny` is the mesh loaded by the caller (sample: RE_SOURCE_DIR asset;
    /// gate: TEST_SOURCE_DIR asset — same committed file). A registration
    /// failure leaves that instance's handle null; the renderer skips null
    /// handles gracefully, and callers that need hard guarantees (the gate)
    /// assert handlesRegistered().
    explicit Rig(const std::shared_ptr<render::AssetRegistry>& registry,
                 data::Mesh bunny)
        : registry_(registry),
          gold_(makeBox(kGoldMin, kGoldMax)),
          nearGlass_(makeBox(kNearGlassMin, kNearGlassMax)),
          farGlass_(makeBox(kFarGlassMin, kFarGlassMax)),
          bunny_(std::move(bunny)),
          goldMaterial_(std::make_shared<render::PhongMaterial>(kGoldColor)),
          nearGlassMaterial_(
              std::make_shared<render::PhongMaterial>(kNearGlassColor)),
          farGlassMaterial_(
              std::make_shared<render::PhongMaterial>(kFarGlassColor)),
          bunnyMaterial_(std::make_shared<render::PhongMaterial>(kBunnyColor)) {
        goldHandle_ = registerOr(gold_);
        nearGlassHandle_ = registerOr(nearGlass_);
        farGlassHandle_ = registerOr(farGlass_);
        bunnyHandle_ = registerOr(bunny_);

        // Uniform scale so the longest AABB side is exactly kBunnyMaxSide,
        // then center the mesh at kBunnyCenter. Computed from the committed
        // asset's bounds, so the transform is deterministic for the golden
        // file and identical between sample and gate.
        const data::Aabb& b = bunny_.bounds();
        const glm::vec3 mid = 0.5f * (b.min + b.max);
        const glm::vec3 extent = b.max - b.min;
        const float longest =
            std::max(extent.x, std::max(extent.y, extent.z));
        const float s = kBunnyMaxSide / longest;
        bunnyModel_ = glm::translate(glm::mat4(1.0f), kBunnyCenter) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(s)) *
                      glm::translate(glm::mat4(1.0f), -mid);

        // Opaque layer: the golden box (identity model — extents are baked
        // into the mesh) plus the bunny at its own depth.
        opaqueScene_.meshes.push_back(
            render::MeshInstance{goldHandle_, goldMaterial_, glm::mat4(1.0f)});
        opaqueScene_.meshes.push_back(render::MeshInstance{
            bunnyHandle_, bunnyMaterial_, bunnyModel_});

        // Transparent layer: near red glass, then far blue glass. Draw order
        // between them is deliberately NOT the depth order's reverse — the
        // linked-list pipeline makes the composite order-independent
        // (FR-render.2), which is the capability being demonstrated.
        transparentScene_.meshes.push_back(render::MeshInstance{
            nearGlassHandle_, nearGlassMaterial_, glm::mat4(1.0f)});
        transparentScene_.meshes.push_back(render::MeshInstance{
            farGlassHandle_, farGlassMaterial_, glm::mat4(1.0f)});

        // Full mixed scene (opaque first, transparent second) for the
        // pipeline-engagement spy check via the direct MeshRenderer path.
        fullScene_.meshes = opaqueScene_.meshes;
        fullScene_.meshes.insert(fullScene_.meshes.end(),
                                 transparentScene_.meshes.begin(),
                                 transparentScene_.meshes.end());
    }

    /// True when every mesh registered successfully (the gate asserts this;
    /// the sample degrades gracefully instead).
    [[nodiscard]] bool handlesRegistered() const {
        return !goldHandle_.isNull() && !nearGlassHandle_.isNull() &&
               !farGlassHandle_.isNull() && !bunnyHandle_.isNull();
    }

    /// The registry the handles resolve through (shared ownership).
    [[nodiscard]] const std::shared_ptr<render::AssetRegistry>& registry()
        const {
        return registry_;
    }

    /// The OPAQUE-only layer (golden box + bunny): drawn through the
    /// depth-enabled View pass so the two meshes self-occlude correctly.
    [[nodiscard]] const render::MeshScene& opaqueScene() const {
        return opaqueScene_;
    }

    /// The TRANSPARENT-only layer (two glass boxes): captured through the
    /// injected ITransparencyPipeline.
    [[nodiscard]] const render::MeshScene& transparentScene() const {
        return transparentScene_;
    }

    /// The mixed scene for direct MeshRenderer::render flows (spy gate).
    [[nodiscard]] const render::MeshScene& fullScene() const {
        return fullScene_;
    }

    /// Number of transparent instances the pipeline must capture per frame —
    /// exactly what the engagement spy must count (FR-render.3 acceptance).
    static constexpr std::size_t kTransparentCount = 2;

    /// Ortho camera framing the arrangement: eye on +Z looking down -Z, NDC
    /// [-1,1] vertically, [-aspect,+aspect] horizontally (aspect = width /
    /// height of the target) so no window shape stretches the scene.
    [[nodiscard]] render::Camera cameraFor(float aspect) const {
        render::Camera camera;
        camera.position = kEye;
        camera.view =
            glm::lookAt(kEye, glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f));
        camera.proj = glm::ortho(-aspect, aspect, -1.0f, 1.0f, kNearPlane,
                                 kFarPlane);
        return camera;
    }

   private:
    /// Flat-shaded axis-aligned box shell (each face owns its vertices, so
    /// MeshGeometry's area-weighted vertex normals equal the face normals and
    /// every +Z face shades to EXACTLY the base color — the property the
    /// analytic byte expectations stand on).
    static data::Mesh makeBox(glm::vec3 minCorner, glm::vec3 maxCorner) {
        return makeBoxMesh(minCorner, maxCorner);
    }

    /// Register `mesh` in the shared registry and return its handle. On the
    /// (practically impossible) failure — no current GL context at
    /// construction time — this returns the null AssetHandle instead of
    /// throwing or aborting: scenes carrying a null handle are rendered by
    /// SKIPPING that instance inside MeshRenderer's draw loop, so the frame
    /// degrades gracefully and every failure surfaces through the caller's
    /// handlesRegistered() check or a logged error, never a crash (typed
    /// errors over exceptions, SPEC §5).
    render::AssetHandle registerOr(const data::Mesh& mesh) {
        const auto registered = registry_->registerAsset(mesh);
        if (registered.failed()) {
            return render::AssetHandle{};
        }
        return *registered;
    }

    std::shared_ptr<render::AssetRegistry> registry_;
    data::Mesh gold_;
    data::Mesh nearGlass_;
    data::Mesh farGlass_;
    data::Mesh bunny_;
    std::shared_ptr<render::PhongMaterial> goldMaterial_;
    std::shared_ptr<render::PhongMaterial> nearGlassMaterial_;
    std::shared_ptr<render::PhongMaterial> farGlassMaterial_;
    std::shared_ptr<render::PhongMaterial> bunnyMaterial_;
    render::AssetHandle goldHandle_{};
    render::AssetHandle nearGlassHandle_{};
    render::AssetHandle farGlassHandle_{};
    render::AssetHandle bunnyHandle_{};
    glm::mat4 bunnyModel_{1.0f};
    render::MeshScene opaqueScene_{};
    render::MeshScene transparentScene_{};
    render::MeshScene fullScene_{};
};

// ---------------------------------------------------------------------------
// composeFrame — the per-frame composition both consumers run.
// ---------------------------------------------------------------------------

/// Render ONE frame of the rig's scene into `view`'s target using the real
/// OIT contract (see the header comment):
///
///   1. `view.renderWithEnsure(ctx)` draws the OPAQUE layer — the caller has
///      configured `view` once with setDepthTest(true) and one
///      addItem(rig.opaqueScene(), opaqueLayerRenderer) item — through the
///      depth-enabled pass prologue (true occlusion among the opaque meshes).
///   2. `ctx.disableDepthTest()` turns the depth test back OFF through the
///      SAME DrawContext instance that enabled it (its cache tracks the
///      enable, so the disable always issues its raw call regardless of the
///      global-function cache state) — the v1 pipeline runs its capture and
///      composite with the depth test off, exactly like every established
///      LinkedListOIT flow.
///   3. pipeline.begin -> drawTransparent per glass instance (geometry
///      resolved through the rig's registry by AssetHandle) -> pipeline.end
///      composites the depth-sorted premultiplied fragments OVER the opaque
///      result inside `view`'s target.
///
/// The blit/present step is intentionally NOT here: the sample blits to the
/// window afterwards; the gate reads pixels straight from the view target.
/// Returns a typed error on the first failing stage (SPEC §5, never silent).
inline data::Result<void> composeFrame(render::View& view,
                                       render::ITransparencyPipeline& pipeline,
                                       const Rig& rig, std::uint32_t width,
                                       std::uint32_t height,
                                       core::DrawContext& ctx) {
    const render::Camera camera =
        rig.cameraFor(static_cast<float>(width) / static_cast<float>(height));
    view.setRect(render::ViewRect{0, 0, static_cast<int>(width),
                                  static_cast<int>(height)});
    view.setCamera(camera);

    // Stage 1: opaque pass into the depth-enabled target (occlusion-capable).
    const auto rendered = view.renderWithEnsure(ctx);
    if (rendered.failed()) {
        return rendered;
    }

    // Stage 2: hand the depth state back to the established pipeline
    // configuration (depth OFF during capture AND composite). Done through
    // the same ctx instance that enabled the test inside View::render, so
    // the transition is exact regardless of the global draw-state cache.
    ctx.disableDepthTest();

    render::RenderTarget target;
    target.framebuffer = &view.target()->framebuffer();
    target.width = width;
    target.height = height;
    target.clearColor = kClearColor;

    // Stage 3: capture + depth-sorted composite over the opaque result.
    const auto begun = pipeline.begin(camera, target);
    if (begun.failed()) {
        return begun;
    }
    for (const render::MeshInstance& instance :
         rig.transparentScene().meshes) {
        auto geometry = resolveMeshGeometry(rig.registry(), instance.mesh,
                                            "oit_scene");
        if (geometry.failed()) {
            return data::makeError<void>(geometry.error().code,
                                         geometry.error().message);
        }
        render::MeshGeometry& geometryRef = **geometry;
        const auto captured = pipeline.drawTransparent(
            geometryRef, instance.material->baseColor(), instance.model,
            camera);
        if (captured.failed()) {
            return captured;
        }
    }
    return pipeline.end(camera, target);
}

} // namespace re::app::oit_scene
