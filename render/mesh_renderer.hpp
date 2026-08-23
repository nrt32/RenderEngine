#pragma once

// render/mesh_renderer.hpp — MeshRenderer: opaque forward pass + auto-engaged
// OIT (SPEC §3, FR-render.1/3).
//
// Stateless rendering: MeshRenderer::render(scene, camera, target) receives
// all of its data per call; the renderer owns only GL resources (its cached
// opaque shader program). GPU geometry is owned by the shared AssetRegistry
// (SPEC §9 V2.5): scenes carry AssetHandles instead of raw `const data::Mesh*`
// pointers, and the renderer resolves them through the injected registry — so
// one GPU object exists per individual CPU mesh GLOBALLY, shared with every
// other mesh-family renderer (SliceRenderer) and every view. One mesh can be
// drawn by several views and several renderers without duplication.
//
// Mesh geometry handling is shared through MeshGeometry, reused by the later
// mesh-family renderers (SliceRenderer, T11). Materials are consumed through
// the IMaterial interface; transparency is a material property, and the
// injected ITransparencyPipeline is auto-engaged only when some mesh's
// material is transparent (FR-render.3). An opaque-only scene never engages
// the pipeline.
//
// render/ is GL-call-free: it draws through the core::Draw API and core/ RAII
// objects (guardrail gpu_api_ownership).

#include <glm/mat4x4.hpp>
#include <optional>
#include <vector>

#include "core/draw.hpp"
#include "core/shader_program.hpp"
#include "data/result.hpp"
#include "render/asset_registry.hpp"
#include "render/imaterial.hpp"
#include "render/itransparency_pipeline.hpp"
#include "render/mesh_geometry.hpp"
#include "render/types.hpp"

namespace re::render {

/// A single mesh in a scene: an AssetHandle into the shared AssetRegistry
/// (SPEC §9 V2.5), its material, and its model transform. Scenes carry the
/// handle — the currency views exchange — never a raw CPU pointer.
struct MeshInstance {
    AssetHandle mesh; ///< Handle of the GPU geometry in the shared registry.
    const IMaterial* material = nullptr;
    glm::mat4 model{1.0f};
};

/// A scene of mesh instances to render (CPU-side; app/ builds these).
struct MeshScene {
    std::vector<MeshInstance> meshes;
};

/// Stateless opaque-forward-pass mesh renderer (SPEC §3).
///
/// Owns only GL resources: the cached opaque shader program. Mesh geometry
/// lives in the shared AssetRegistry (SPEC §9 V2.5): the renderer resolves
/// each instance's AssetHandle through the injected registry, so a data::Mesh
/// is uploaded to the GPU once — even across MeshRenderer + SliceRenderer.
class MeshRenderer : public IRenderer {
   public:
    /// Construct with the shared asset registry (`registry` must be non-null
    /// and outlive the renderer; scenes' AssetHandles resolve through it, SPEC
    /// §9 V2.5) and the injected transparency pipeline (`transparency` may be
    /// null when no OIT is available). The pipeline is auto-engaged only when
    /// a scene contains a transparent material (FR-render.3).
    explicit MeshRenderer(AssetRegistry* registry,
                          ITransparencyPipeline* transparency = nullptr);

    /// Render `scene` into `target` from `camera`. On success the target
    /// framebuffer is left bound (so tests can read it back). Returns a typed
    /// error if the opaque shader fails to build, an instance's handle fails
    /// to resolve (stale/dangling), or a draw cannot be issued.
    data::Result<void> render(const MeshScene& scene, const Camera& camera,
                              const RenderTarget& target);

    /// IRenderer dispatch (SPEC §9 V2.3): renders when `scene` holds a
    /// MeshScene; returns a typed error when it holds a different technique
    /// (SPEC §5, no exceptions).
    data::Result<void> render(const Scene& scene, const Camera& camera,
                               const RenderTarget& target) override;

    /// Draw one layer into the currently-bound framebuffer (ReView's ViewTarget),
    /// assuming ReView already performed bind+viewport+clear via the same
    /// DrawContext. Does not clear — second layer must not clear away the first.
    /// Returns typed error for stale handle or draw failure.
    data::Result<void> drawLayer(const MeshScene& scene, const Camera& camera,
                                 core::DrawContext& ctx);

    /// The injected transparency pipeline (may be null).
    ITransparencyPipeline* transparencyPipeline() const noexcept {
        return transparency_;
    }

    /// The shared asset registry instances' handles resolve through (non-null
    /// after construction).
    AssetRegistry* assetRegistry() const noexcept {
        return registry_;
    }

   private:
    /// Build (and cache) the opaque shader program, returning a pointer to the
    /// cached program (non-null on success).
    data::Result<core::ShaderProgram*> opaqueProgram();

    /// Resolve `handle` to its GPU geometry through the shared asset registry
    /// (SPEC §9 V2.5). Returns a typed error for a stale/dangling handle.
    data::Result<MeshGeometry*> geometryFor(const AssetHandle& handle);

    /// Draw every opaque mesh instance directly to `target`.
    data::Result<void> drawOpaque(const MeshScene& scene, const Camera& camera,
                                  const RenderTarget& target);

    /// Capture every transparent mesh instance through the engaged
    /// transparency pipeline (FR-render.3). Returns a typed error if a geometry
    /// resolution or pipeline capture cannot be issued (SPEC §5).
    data::Result<void> drawTransparent(const MeshScene& scene,
                                       const Camera& camera);

    AssetRegistry* registry_;
    ITransparencyPipeline* transparency_;

    std::optional<core::ShaderProgram> opaqueProgram_;
};

} // namespace re::render
