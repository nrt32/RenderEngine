#pragma once

// render/mesh_renderer.hpp — MeshRenderer: opaque forward pass + auto-engaged
// OIT (SPEC §3, FR-render.1/3).
//
// Stateless rendering: MeshRenderer::render(scene, camera, target) receives
// all of its data per call; the renderer owns only GL resources (its cached
// opaque shader program and the GPU geometries of the meshes it has drawn).
// One mesh can be drawn by several views without duplication.
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
#include <unordered_map>
#include <vector>

#include "core/shader_program.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "render/imaterial.hpp"
#include "render/itransparency_pipeline.hpp"
#include "render/mesh_geometry.hpp"
#include "render/types.hpp"

namespace re::render {

/// A single mesh in a scene: a data::Mesh, its material, and its model
/// transform.
struct MeshInstance {
    const data::Mesh* mesh = nullptr;
    const IMaterial* material = nullptr;
    glm::mat4 model{1.0f};
};

/// A scene of mesh instances to render (CPU-side; app/ builds these).
struct MeshScene {
    std::vector<MeshInstance> meshes;
};

/// Stateless opaque-forward-pass mesh renderer (SPEC §3).
///
/// Owns only GL resources: the cached opaque shader program and a geometry
/// cache keyed by mesh pointer, so a data::Mesh is uploaded to the GPU once.
class MeshRenderer : public IRenderer {
   public:
    /// Construct with the injected transparency pipeline (`transparency` may
    /// be null when no OIT is available). The pipeline is auto-engaged only
    /// when a scene contains a transparent material (FR-render.3).
    explicit MeshRenderer(ITransparencyPipeline* transparency = nullptr);

    /// Render `scene` into `target` from `camera`. On success the target
    /// framebuffer is left bound (so tests can read it back). Returns a typed
    /// error if the opaque shader fails to build or a draw cannot be issued.
    data::Result<void> render(const MeshScene& scene, const Camera& camera,
                              const RenderTarget& target);

    /// IRenderer dispatch (SPEC §9 V2.3): renders when `scene` holds a
    /// MeshScene; returns a typed error when it holds a different technique
    /// (SPEC §5, no exceptions).
    data::Result<void> render(const Scene& scene, const Camera& camera,
                              const RenderTarget& target) override;

    /// The injected transparency pipeline (may be null).
    ITransparencyPipeline* transparencyPipeline() const noexcept {
        return transparency_;
    }

   private:
    /// Build (and cache) the opaque shader program, returning a pointer to the
    /// cached program (non-null on success).
    data::Result<core::ShaderProgram*> opaqueProgram();

    /// Upload the GPU geometry for `mesh` if not already cached, returning a
    /// pointer to it.
    data::Result<MeshGeometry*> geometryFor(const data::Mesh& mesh);

    /// Draw every opaque mesh instance directly to `target`.
    data::Result<void> drawOpaque(const MeshScene& scene, const Camera& camera,
                                  const RenderTarget& target);

    /// Capture every transparent mesh instance through the engaged
    /// transparency pipeline (FR-render.3). Returns a typed error if a geometry
    /// upload or pipeline capture cannot be issued (SPEC §5).
    data::Result<void> drawTransparent(const MeshScene& scene,
                                       const Camera& camera);

    ITransparencyPipeline* transparency_;

    std::optional<core::ShaderProgram> opaqueProgram_;
    std::unordered_map<const data::Mesh*, MeshGeometry> geometries_;
};

} // namespace re::render
