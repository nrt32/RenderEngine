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
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/re_context.hpp"
#include "core/element_buffer.hpp"
#include "core/framebuffer.hpp"
#include "core/shader_program.hpp"
#include "core/texture3d.hpp"
#include "core/vertex_array.hpp"
#include "core/vertex_buffer.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "render/asset_registry.hpp"
#include "render/render_constants.hpp"
#include "render/screen_quad.hpp"
#include "render/shader_cache.hpp"
#include "render/types.hpp" // render::Camera / render::RenderTarget
#include "volume/transfer_function.hpp"

namespace re::render {

/// A single volume in a scene: the dataset handle (owner-driven) + TF + model.
///
/// The dataset's voxel grid occupies the unit cube [0,1]^3 in *model space*
/// (the renderer normalizes the grid's index space [0, dim-1] to [0,1] per
/// axis), so model space maps directly to texture-coordinate space [0,1]^3.
/// The model matrix places and orients the volume in world space; the world
/// AABB used by the shader's slab intersection is the axis-aligned bounding
/// box of the 8 transformed corners of [0,1]^3 (exact for axis-aligned
/// scaling/translation, which is the v1 case).
///
/// Owner-driven identity (T7): the instance carries a `VolumeTextureHandle`
/// minted by `AssetRegistry::registerVolume` (hashed at load/register time,
/// never per frame per data/content_hash.hpp:31). The handle IS the identity
/// (content-hash dedup, not pointer). The `dataset` shared_ptr is retained
/// for CPU-side size/voxel access (analytic expectations) but the GPU texture
/// resolves exclusively via `handle` (O(1) handle resolve, no per-frame
/// FNV-1a). New code should set `handle` via explicit registration; the
/// legacy `dataset`-only path is removed (no lazy-hash lookup).
struct VolumeInstance {
    /// Owner-driven handle to the GPU 3D texture (hashed at register time).
    /// Must be non-null for draw (typed error if null).
    VolumeTextureHandle handle{};
    /// Shared reference to the immutable volume dataset for CPU math/size.
    /// Retained for analytic size (instance model) but NOT for GPU identity.
    std::shared_ptr<const data::VolumeDataset> dataset = nullptr;
    /// Transfer function carried BY VALUE on purpose: a TF is a small,
    /// immutable control-point ramp, and copying it into the per-frame
    /// instance removes the last raw borrow from this path — there is no
    /// pointer that could dangle or be nulled mid-frame. Default ramp =
    /// grayscale black→white (the identity display ramp).
    volume::TransferFunction transferFunction{
        {{0.0f, {0, 0, 0, 0}}, {1.0f, {1, 1, 1, 1}}}};
    glm::mat4 model{1.0f};

    VolumeInstance() = default;
    // Legacy 3-arg ctor for pre-T7 direct-renderer tests (dataset-only): leaves handle null so the renderer's fallback legacyHandleCache registers the dataset once at first use via AssetRegistry::registerVolume (hashed at load/register time per data/content_hash.hpp:31, never per frame) and then hits O(1) cache without per-frame FNV-1a; this shim keeps old fixtures green while new broker-mediated code and the T7 gate use explicit VolumeTextureHandle via register→resolve, where content-hash IS identity and pinned refs==0 slots are gone.
    VolumeInstance(std::shared_ptr<const data::VolumeDataset> ds,
                   volume::TransferFunction tf, glm::mat4 m)
        : handle{}, dataset(std::move(ds)), transferFunction(std::move(tf)), model(m) {}
    // Full T7 owner-driven ctor with explicit handle (SPEC §7, data/content_hash.hpp:31 hashed at load/register time, never per frame): the instance carries a VolumeTextureHandle minted by AssetRegistry::registerVolume; the renderer resolves O(1) via handle's contentHash (content-hash IS identity, no byObject shim, no pinned slots). New broker-mediated code and the T7 gate use this path; legacy 3-arg ctor remains only for pre-T7 direct tests via fallback cache.
    VolumeInstance(VolumeTextureHandle h,
                   std::shared_ptr<const data::VolumeDataset> ds,
                   volume::TransferFunction tf, glm::mat4 m)
        : handle(h), dataset(std::move(ds)), transferFunction(std::move(tf)), model(m) {}
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

/// Stateless ray-cast volume renderer (SPEC §3, FR-render.6, T14 collapse — IRenderer
/// dispatch deleted, broker drawLayer path is single source of truth).
///
/// Owns only GL resources: the cached ray-cast shader program and one shared
/// full-screen quad (every volume is ray-cast over the whole viewport). GPU
/// 3D textures live in the shared `AssetRegistry` (SPEC §7 T14, T7
/// owner-driven): each instance carries a `VolumeTextureHandle` minted at
/// register time (hashed at load/register time, never per frame per
/// data/content_hash.hpp:31); the renderer resolves via O(1) handle
/// (`resolveVolume`, content-hash IS identity, no per-frame FNV-1a, no
/// lazy lookupVolume path), so each distinct dataset is uploaded once per
/// store — even across two renderer instances — and no per-renderer texture
/// map exists. The transfer function is uploaded per instance from its
/// control points. T14 removed the IRenderer Scene variant dispatch; all
/// rendering goes through drawLayer after View's beginPass.
class VolumeRenderer {
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

    /// Draw one layer into the currently-bound framebuffer (ReView's ViewTarget),
    /// assuming ReView already performed bind+viewport+clear via REContext::current()
    /// (T2 global per-GL-context, 2 layers sharing viewport issue 1 glViewport).
    /// Does not clear — second layer must not clear away the first.
    data::Result<void> drawLayer(const VolumeScene& scene, const Camera& camera);

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
    /// screenQuad_ `optional<>` member) — valid while this renderer is.
    data::Result<core::VertexArray*> screenQuad();

    /// The ONE shared instance-draw loop behind both entry points (render()
    /// after its pass prologue, drawLayer() as a View layer): installs
    /// `program`, binds each instance's store-owned 3D texture via direct
    /// handle resolve (no per-renderer map, O(1) shared store), uploads the
    /// slab/uniform block (single copy of that math), and issues one
    /// full-screen-quad draw per instance.
    data::Result<void> drawInstances(const VolumeScene& scene,
                                     const Camera& camera,
                                     core::ShaderProgram* program,
                                     core::VertexArray* quadVao);

    /// Upload the transfer function `tf` to `program` as the TF control-point
    /// uniforms (uTfCount/uTfValues/uTfColors).
    void uploadTransferFunction(const volume::TransferFunction& tf,
                                core::ShaderProgram* program) const;

    std::shared_ptr<AssetRegistry> assets_;
    LazyProgramCache rayCastProgram_;
    std::optional<ScreenQuad> screenQuad_;
    // Fallback cache for legacy dataset-only instances (pre-T7): maps raw
    // dataset pointer to its owner-driven handle (registered once, then O(1)).
    // This keeps old direct-renderer tests green without per-frame hashing
    // while new code should set handle explicitly via AssetRegistry::registerVolume.
    mutable std::unordered_map<const data::VolumeDataset*, VolumeTextureHandle>
        legacyHandleCache_;
};

} // namespace re::render
