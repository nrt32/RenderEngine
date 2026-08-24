#pragma once

// render/contour_renderer.hpp — ContourRenderer: geometry-shader plane∩mesh
// outline (FR-app.3, V3.8b T11).
//
// The outline-only peer of SliceRenderer: where the slice clip pass renders
// the KEPT side of a plane-clipped mesh and captures the on-plane
// cross-section polygon for FR-render.4, ContourRenderer renders ONLY the
// plane-intersection OUTLINE — computed entirely ON THE GPU by
// render/shaders/contour.geom.glsl, which classifies each triangle against
// the world-space clip plane with the same signed-distance pattern as
// slice_clip.geom.glsl and emits one screen-space thick-line quad per
// crossing segment (core-profile GL caps glLineWidth at 1.0; the FR-app.3
// acceptance band is ±2 px around the analytic curve, so the geometry shader
// emits the standard GPU thick-line expansion — see docs/render.md).
//
// Re* is RE-minimal (T9 V3.8): a contour carries only its AssetHandle (the
// GPU-geometry currency views exchange, SPEC §9 V2.5), the ClipPlane, a flat
// RGBA color, the model matrix, and the stroke half-width in pixels — never a
// verbatim app-side descriptor. The scene→render translation lives in the
// broker library's ContourMapper, a specialization of the generic mapper
// interface over (scene::ContourObject, render::ContourObject) — SPEC §11.
//
// Stateless rendering: render()/drawLayer() receive all of their data per
// call; the renderer owns only GL resources (its cached shader program). GPU
// geometry is owned by the shared AssetRegistry and resolved per draw, shared
// with MeshRenderer/SliceRenderer. No OIT (contours are opaque overlay
// strokes), no readback in production paths.
//
// render/ is GL-call-free: it draws through the core::Draw API and core/ RAII
// objects (guardrail gpu_api_ownership).

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <optional>
#include <vector>

#include "core/draw.hpp"
#include "core/framebuffer.hpp"
#include "core/shader_program.hpp"
#include "data/result.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_geometry.hpp"
#include "render/types.hpp" // Camera / RenderTarget / ClipPlane

namespace re::render {

/// One GPU contour layer: the plane∩mesh outline of one mesh asset.
struct ContourObject {
    /// Handle of the contoured mesh's GPU geometry in the shared registry —
    /// RE-minimal by design: this render-side object references geometry
    /// through a handle and never stores mesh bytes, positions, or app-side
    /// description values.
    AssetHandle mesh{};
    /// World-space clip plane whose intersection with the mesh is outlined.
    ClipPlane plane{};
    /// Straight RGBA stroke color (opaque red default = the FR-app.3 MPR
    /// contour color, exact bytes 255,0,0,255).
    glm::vec4 color{1.0f, 0.0f, 0.0f, 1.0f};
    /// Model transform of the contoured mesh (world → mesh mapping).
    glm::mat4 model{1.0f};
    /// Half stroke width in PIXELS. Pixels whose center lies within this
    /// distance of the analytic projected segment are covered; 2.0 fills the
    /// FR-app.3 ±2 px acceptance band exactly.
    float halfWidthPx{2.0f};
};

/// A scene of contour layers to draw (CPU-side; broker/app build these).
struct ContourScene {
    std::vector<ContourObject> contours;
};

/// Stateless geometry-shader plane-outline renderer (SPEC §3, FR-app.3).
///
/// Owns only GL resources: the cached contour shader program (vertex +
/// geometry + fragment). Mesh geometry lives in the shared AssetRegistry
/// (SPEC §9 V2.5): the renderer resolves each object's AssetHandle through
/// the injected registry, sharing one GPU object per CPU mesh with
/// MeshRenderer and SliceRenderer.
class ContourRenderer {
   public:
    /// Construct with the shared asset registry (SHARED ownership, T13 — see
    /// MeshRenderer). A null registry is accepted at construction so member
    /// init order never matters; every draw validates it and returns a typed
    /// error (code 4) instead of dereferencing.
    explicit ContourRenderer(std::shared_ptr<AssetRegistry> registry);

    ContourRenderer(const ContourRenderer&) = delete;
    ContourRenderer& operator=(const ContourRenderer&) = delete;

    /// Render `scene`'s contour outlines into `target` from `camera`: binds
    /// the target framebuffer, sets the viewport via the target size, clears
    /// to the target clear color, disables depth test and blending (exact
    /// color), then draws every object's outline. On success the target
    /// framebuffer is left bound (so tests can read it back). Returns a typed
    /// error if the shader fails to build, an object's handle fails to
    /// resolve (stale/dangling/null), or a draw cannot be issued.
    data::Result<void> render(const ContourScene& scene, const Camera& camera,
                              const RenderTarget& target);

    /// Draw ONE contour object into the currently-bound framebuffer (ReView's
    /// ViewTarget), assuming ReView already performed bind+viewport+clear via
    /// the same DrawContext (layer semantics of View::render — no clear here,
    /// so a second layer does not erase the first). The viewport pixel size
    /// for the thick-line expansion is read from `ctx` (the context View
    /// already configured); a cold context (no setViewport yet) is a typed
    /// error. Blending must already be disabled for exact stroke colors (View
    /// disables it before its layers).
    data::Result<void> drawLayer(const ContourObject& object,
                                 const Camera& camera, core::DrawContext& ctx);

    /// Layer variant drawing every object of `scene` in order.
    data::Result<void> drawLayer(const ContourScene& scene,
                                 const Camera& camera, core::DrawContext& ctx);

    /// The shared asset registry objects' handles resolve through (non-null
    /// after a valid construction; null only if constructed with nullptr —
    /// draws then fail with typed error code 4).
    const std::shared_ptr<AssetRegistry>& assetRegistry() const noexcept {
        return registry_;
    }

   private:
    /// Build (and cache) the contour shader program, returning a pointer to
    /// the cached program (non-null on success).
    data::Result<core::ShaderProgram*> program();

    /// Resolve `handle` to its GPU geometry through the shared asset registry
    /// (SPEC §9 V2.5; shared with MeshRenderer/SliceRenderer). Returns a
    /// typed error for a stale/dangling handle.
    /// @note lifetime: non-owning view of registry-owned storage (the shared
    /// slot's unique_ptr) — valid until the handle's slot is unregistered.
    data::Result<MeshGeometry*> geometryFor(const AssetHandle& handle);

    /// Issue the outline draw for one object with `program` already in use
    /// and per-frame uniforms (view/proj/plane) already set.
    data::Result<void> drawOne(const ContourObject& object,
                               core::ShaderProgram* program);

    std::shared_ptr<AssetRegistry> registry_;

    std::optional<core::ShaderProgram> program_;
};

} // namespace re::render
