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

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/framebuffer.hpp"
#include "core/shader_program.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "render/imaterial.hpp"
#include "render/itransparency_pipeline.hpp"
#include "render/mesh_geometry.hpp"

namespace re::render {

/// Camera: view + projection matrices plus the eye position (for lighting and
/// view-direction terms).
struct Camera {
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::vec3 position{0.0f, 0.0f, 0.0f};
};

/// Offscreen render target: a color-only framebuffer plus its pixel size and
/// clear color (SPEC §3; v1 FBOs are color-only, SPEC §6 / docs/core.md).
struct RenderTarget {
    core::Framebuffer* framebuffer = nullptr;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 0.0f};
};

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
class MeshRenderer {
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

    ITransparencyPipeline* transparency_;

    std::optional<core::ShaderProgram> opaqueProgram_;
    std::unordered_map<const data::Mesh*, MeshGeometry> geometries_;
};

} // namespace re::render
