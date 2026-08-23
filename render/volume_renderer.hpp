#pragma once

// render/volume_renderer.hpp — VolumeRenderer: ray-cast GL draw pass (SPEC §3,
// FR-render.6).
//
// Stateless rendering: VolumeRenderer::render(scene, camera, target) receives
// all of its data per call; the renderer owns only GL resources (its cached
// ray-cast shader program and one shared full-screen quad VAO/VBO). GPU 3D
// textures live in the SHARED asset store (`render::AssetRegistry`, SPEC §7
// T14): every dataset is content-hash-deduped into exactly one Texture3D per
// registry, so two VolumeRenderer instances rendering the same dataset share
// one GL texture id, identical-content distinct allocations dedup, and an
// owner can invalidate an entry explicitly (unregisterVolume) without any
// per-renderer pointer-keyed cache. A dataset is uploaded to the GPU once and
// reused across instances, renderers, and views.
//
// The renderer consumes the pure volume/ math (SPEC §3: "VolumeRenderer
// (ray-cast GL draw; volume/ provides the pure math)"):
//   - the fragment shader performs the closed-form slab AABB intersection and
//     center-stepped sampling loop (mirroring volume::intersectRayAabb /
//     computeRaySampleSteps, FR-vol.3) and front-to-back premultiplied-alpha
//     compositing (mirroring volume::compositeFrontToBack, FR-vol.2);
//   - the transfer function is uploaded from a volume::TransferFunction's
//     control points (FR-vol.1) and evaluated as a piecewise-linear ramp.
// The T9 gate (FR-render.6) verifies the GPU output matches the analytic CPU
// ray-cast computed from these same volume/ primitives within 1/255.
//
// render/ is GL-call-free: it draws through the core::Draw API and core/ RAII
// objects (guardrail gpu_api_ownership).

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "core/draw.hpp"
#include "core/element_buffer.hpp"
#include "core/framebuffer.hpp"
#include "core/shader_program.hpp"
#include "core/texture3d.hpp"
#include "core/vertex_array.hpp"
#include "core/vertex_buffer.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "render/asset_registry.hpp"
#include "render/types.hpp" // render::Camera / render::RenderTarget
#include "volume/transfer_function.hpp"

namespace re::render {

/// A single volume in a scene: the dataset, the transfer function mapping its
/// scalar values to RGBA, and the model matrix.
///
/// The dataset's voxel grid occupies the unit cube [0,1]^3 in *model space*
/// (the renderer normalizes the grid's index space [0, dim-1] to [0,1] per
/// axis), so model space maps directly to texture-coordinate space [0,1]^3.
/// The model matrix places and orients the volume in world space; the world
/// AABB used by the shader's slab intersection is the axis-aligned bounding
/// box of the 8 transformed corners of [0,1]^3 (exact for axis-aligned
/// scaling/translation, which is the v1 case).
struct VolumeInstance {
    /// Shared reference to the immutable volume dataset (T13: co-owned with
    /// the scene object / store — no raw borrow to track).
    std::shared_ptr<const data::VolumeDataset> dataset = nullptr;
    /// Transfer function carried BY VALUE (T13: a TF is a small immutable
    /// control-point ramp — copying it into the per-frame instance removes
    /// the last borrow from this path; null-pointer hazards are impossible).
    /// Default ramp = grayscale black→white (the identity display ramp).
    volume::TransferFunction transferFunction{
        {{0.0f, {0, 0, 0, 0}}, {1.0f, {1, 1, 1, 1}}}};
    glm::mat4 model{1.0f};
};

/// A scene of volume instances to render (CPU-side; app/ builds these).
struct VolumeScene {
    std::vector<VolumeInstance> volumes;
};

/// Default ray-cast sampling step length in world units (t-units along the
/// ray): the shader samples `floor(span / stepLength)` steps at their centers,
/// mirroring volume::computeRaySampleSteps (FR-vol.3). A public constant so the
/// analytic CPU expectation in the gate uses the identical spacing.
inline constexpr float kDefaultStepLength = 0.25f;

/// Stateless ray-cast volume renderer (SPEC §3, FR-render.6).
///
/// Owns only GL resources: the cached ray-cast shader program and one shared
/// full-screen quad (every volume is ray-cast over the whole viewport). GPU
/// 3D textures live in the shared `AssetRegistry` (SPEC §7 T14): the renderer
/// lazily resolves each instance's dataset by content hash, so each distinct
/// dataset is uploaded to the GPU once per store — even across two renderer
/// instances — and no per-renderer texture map exists. The transfer function
/// is uploaded per instance from its control points.
class VolumeRenderer : public IRenderer {
   public:
    /// Construct with the shared asset store (SHARED ownership: the renderer
    /// co-owns the registry with every other renderer — declaration order can
    /// never dangle it). Defaults to the process-wide registry
    /// (`AssetRegistry::shared()`), so two default-constructed renderers share
    /// one GPU texture per content (the T14 invariant). A null pointer is
    /// accepted at construction so member-init order never matters, but every
    /// render/drawLayer validates it and returns a typed error (code 4)
    /// instead of dereferencing.
    explicit VolumeRenderer(
        std::shared_ptr<AssetRegistry> assets = AssetRegistry::shared());

    /// Render `scene` into `target` from `camera`. On success the target
    /// framebuffer is left bound (so tests can read it back). Returns a typed
    /// error if the shader fails to build, a texture upload fails, or a draw
    /// cannot be issued.
    data::Result<void> render(const VolumeScene& scene, const Camera& camera,
                              const RenderTarget& target);

    /// IRenderer dispatch (SPEC §9 V2.3): renders when `scene` holds a
    /// VolumeScene; returns a typed error when it holds a different technique
    /// (SPEC §5, no exceptions).
    data::Result<void> render(const Scene& scene, const Camera& camera,
                               const RenderTarget& target) override;

    /// Draw one layer into the currently-bound framebuffer (ReView's ViewTarget),
    /// assuming ReView already performed bind+viewport+clear via the same
    /// DrawContext. Does not clear — second layer must not clear away the first.
    data::Result<void> drawLayer(const VolumeScene& scene, const Camera& camera,
                                 core::DrawContext& ctx);

    /// The world-space AABB of `instance`: the min/max of its model-space unit
    /// cube [0,1]^3 transformed by the instance's model matrix (exact for
    /// axis-aligned transforms). Public so tests can verify it analytically.
    static std::pair<glm::vec3, glm::vec3> worldAabb(
        const VolumeInstance& instance);

    /// The shared asset store textures resolve through (non-null after a valid
    /// construction; null only if constructed with nullptr — renders then fail
    /// with typed error code 4).
    const std::shared_ptr<AssetRegistry>& assets() const noexcept {
        return assets_;
    }

   private:
    /// Build (and cache) the ray-cast shader program, returning a pointer to
    /// the cached program (non-null on success).
    data::Result<core::ShaderProgram*> rayCastProgram();

    /// Ensure the shared full-screen quad geometry is uploaded, returning a
    /// pointer to the cached vertex array (non-null on success).
    /// @note lifetime: non-owning view of renderer-owned storage (the
    /// screenQuadVao_ `optional<>` member) — valid while this renderer is.
    data::Result<core::VertexArray*> screenQuad();

    /// Resolve `dataset`'s content in the shared asset store (lazy
    /// find-or-upload by content hash, no reference-count change — T14),
    /// returning a pointer to the store-owned texture (non-null on success).
    /// @note lifetime: non-owning view of store-owned storage (the slot's
    /// unique_ptr) — valid until the slot's last reference is released or the
    /// store dies; renderers never retain it across frames.
    data::Result<core::Texture3D*> textureFor(
        const std::shared_ptr<const data::VolumeDataset>& dataset);

    /// Upload the transfer function `tf` to the currently-in-use program as
    /// the TF control-point uniforms (uTfCount/uTfValues/uTfColors).
    void uploadTransferFunction(const volume::TransferFunction& tf) const;

    std::shared_ptr<AssetRegistry> assets_;
    std::optional<core::ShaderProgram> rayCastProgram_;
    std::optional<core::VertexArray> screenQuadVao_;
    std::optional<core::VertexBuffer> screenQuadVbo_;
    std::optional<core::ElementBuffer>
        screenQuadEbo_; // index buffer referenced by screenQuadVao_
    std::size_t screenQuadIndexCount_{6u}; // two triangles
};

} // namespace re::render
