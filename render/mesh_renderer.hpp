#pragma once

// render/mesh_renderer.hpp — MeshRenderer: opaque forward pass + auto-engaged
// OIT (SPEC §3, FR-render.1/3).
//
// Stateless rendering: MeshRenderer::drawLayer(scene, camera) receives (T3b
// render() deleted — single OIT via broker/view_compositor.cpp:94);
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
#include <memory>
#include <optional>
#include <vector>

#include "core/re_context.hpp"
#include "core/shader_program.hpp"
#include "data/result.hpp"
#include "render/asset_registry.hpp"
#include "render/imaterial.hpp"
#include "render/itransparency_pipeline.hpp"
#include "render/mesh_geometry.hpp"
#include "render/shader_cache.hpp"
#include "render/types.hpp"

namespace re::render {

/// A single mesh in a scene: an AssetHandle into the shared AssetRegistry
/// (SPEC §9 V2.5), its material, and its model transform. Scenes carry the
/// handle — the currency views exchange — never a raw CPU pointer.
struct MeshInstance {
    AssetHandle mesh; ///< Handle of the GPU geometry in the shared registry.
    /// Shared reference to the (immutable-during-draw) material. Null is the
    /// documented "invalid instance" value — render() returns a typed error,
    /// never dereferences null (SPEC §5).
    /// @note lifetime: co-owned with the material's owner (sample/store);
    /// the instance keeps the material alive for as long as it references it.
    std::shared_ptr<IMaterial> material = nullptr;
    glm::mat4 model{1.0f};
};

/// A scene of mesh instances to render (CPU-side; app/ builds these).
struct MeshScene {
    std::vector<MeshInstance> meshes;
};

/// Stateless opaque-forward-pass mesh renderer (SPEC §3, T14 collapse — IRenderer
/// dispatch deleted, broker drawLayer path is single source of truth).
///
/// Owns only GL resources: the cached opaque shader program. Mesh geometry
/// lives in the shared AssetRegistry (SPEC §9 V2.5): the renderer resolves
/// each instance's AssetHandle through the injected registry, so a data::Mesh
/// is uploaded to the GPU once — even across MeshRenderer + SliceRenderer.
/// T14 removed the IRenderer Scene variant dispatch (the second transparent-mesh
/// behavior that silently dropped transparent instances when no OIT pipeline was
/// wired); the only rendering entry for composited views is drawLayer which
/// draws every resolvable instance with blending off, and the ViewCompositor
/// orchestrates OIT out-of-band when a pipeline is wired — no silent drop.
class MeshRenderer {
   public:
    /// Construct with the shared asset registry (SHARED ownership, T13: the
    /// renderer co-owns the registry with every other mesh-family renderer
    /// and mapper — declaration order can never dangle it) and the injected
    /// transparency pipeline (shared ownership; null = no OIT available). The
    /// pipeline is auto-engaged only when a scene contains a transparent
    /// material (FR-render.3). A null registry is accepted at construction so
    /// member-init order never matters, but every render/drawLayer validates
    /// it and returns a typed error (code 4) instead of dereferencing.
    explicit MeshRenderer(std::shared_ptr<AssetRegistry> registry,
                          std::shared_ptr<ITransparencyPipeline> transparency =
                              nullptr);

    /// Draw one layer into the currently-bound framebuffer (ReView's ViewTarget),
    /// assuming ReView already performed bind+viewport+clear via REContext::current()
    /// (T2 global per-GL-context, thread_local GLFWwindow* → REContextState, 2 layers
    /// sharing viewport issue 1 glViewport — per-frame local ctx deleted). Does not
    /// clear — second layer must not clear away the first. Returns typed error for
    /// stale handle or draw failure.
    data::Result<void> drawLayer(const MeshScene& scene, const Camera& camera);

    /// The injected transparency pipeline (may be null). The returned
    /// shared_ptr is a non-owning OBSERVER handle in spirit — it shares the
    /// same ownership the renderer has (no exclusive claim).
    const std::shared_ptr<ITransparencyPipeline>& transparencyPipeline()
        const noexcept {
        return transparency_;
    }

    /// The shared asset registry instances' handles resolve through (non-null
    /// after a valid construction; null only if constructed with nullptr —
    /// renders then fail with typed error code 4).
    const std::shared_ptr<AssetRegistry>& assetRegistry() const noexcept {
        return registry_;
    }

   private:
    /// Build (and cache) the opaque shader program, returning a pointer to the
    /// cached program (non-null on success).
    data::Result<core::ShaderProgram*> opaqueProgram();

    /// The shared instance-draw loop: installs `program` and draws every
    /// resolvable mesh of `scene` with blending off (single transparent behavior
    /// for the broker path; no silent drop). Transparency handling when an OIT
    /// pipeline is wired is orchestrated by the ViewCompositor out-of-band, not
    /// re-decided per layer. Returns a typed error if a handle fails to resolve
    /// or a draw cannot be issued.
    data::Result<void> drawInstances(const MeshScene& scene,
                                     const Camera& camera,
                                     core::ShaderProgram* program);

    std::shared_ptr<AssetRegistry> registry_;
    std::shared_ptr<ITransparencyPipeline> transparency_;

    LazyProgramCache opaqueProgram_;
};

} // namespace re::render
