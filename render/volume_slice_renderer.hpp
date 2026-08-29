#pragma once

// render/volume_slice_renderer.hpp — VolumeSliceRenderer: GPU volume-plane
// extraction (extends FR-render.5/FR-app.2; plane-capability task).
//
// A "plane" in this engine semantically means a slice extracted from volume
// data. This renderer produces that slice ENTIRELY ON THE GPU: it draws one
// full-screen quad whose fragment shader reconstructs each pixel's camera
// ray, intersects the ray with the instance's world-space `ClipPlane`,
// converts the hit point into the dataset's model space (the unit cube
// [0,1]^3), samples the cached R32F `core::Texture3D` with hardware
// trilinear filtering at texel coordinate (idx+0.5)/dim — the exact mapping
// the VolumeRenderer ray-cast uses, so GL_LINEAR reproduces the CPU sampler
// `data::VolumeDataset::sampleTrilinear` — and writes the transfer-function
// color as straight RGBA. Rays that miss the volume slab write transparent
// black, so the extracted slice appears exactly where the plane crosses the
// dataset; oblique planes need no special-casing (the intersection is fully
// general). Because a slice index enters only through uniforms (the plane's
// point and the instance model matrix), scrolling to another slice is a
// uniform update — there is no CPU voxel loop and no intermediate image
// anywhere on this path.
//
// Display-frame convention for axis-aligned MPR-style views: with the model
// matrix mapping voxel-center index i of a free axis to display coordinate
// i + 0.5 (see app::sliceVolumeModel) and an orthographic camera over that
// display rectangle, pixel centers land exactly on voxel centers, so the
// extracted image reproduces the CPU slice oracle byte-for-byte within
// 1/255 at every probe.
//
// Stateless rendering: drawLayer() receives all of its data per (T3b render() deleted)
// call; the renderer owns only GL resources (its cached shader program and
// one shared full-screen quad VAO/VBO). GPU 3D textures live in the SHARED
// asset store (`render::AssetRegistry`, unified multi-kind store): every
// dataset is content-hash-deduped into exactly one Texture3D per registry,
// shared with VolumeRenderer instances — an extracted slice and a ray-cast
// of the same dataset upload it once.
//
// T14 collapse: the former `render::Scene` IRenderer dispatch variant
// (`variant<const MeshScene*,...>` + `IRenderer render(Scene)`) was deleted;
// the vestigial second transparent-mesh behavior (silent drop when no OIT
// pipeline was wired) is now unrepresentable. All rendering goes through the
// broker path: View's `REContext::current().beginPass` prologue followed by
// concrete `drawLayer(scene, camera)` — the same single behavior as
// Mesh/Plane/Volume. Direct tests call the concrete typed overload.
//
// render/ is GL-call-free: it draws through the core::Draw API and core/
// RAII objects (guardrail gpu_api_ownership).

#include <cstddef>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
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
#include "render/types.hpp" // Camera / RenderTarget / ClipPlane
#include "volume/transfer_function.hpp"

namespace re::render {

/// One GPU-extracted slice: the volume handle (owner-driven) + TF + model + plane.
struct VolumeSliceInstance {
    /// Owner-driven handle to the GPU 3D texture (hashed at register time).
    /// Must be non-null for draw (typed error if null).
    VolumeTextureHandle handle{};
    /// Shared reference to the immutable volume dataset for CPU math/size.
    /// Retained for dimensions but GPU identity is the handle (T7, no per-frame hash).
    std::shared_ptr<const data::VolumeDataset> dataset = nullptr;

    VolumeSliceInstance() = default;
    VolumeSliceInstance(VolumeTextureHandle h,
                        std::shared_ptr<const data::VolumeDataset> ds,
                        volume::TransferFunction tf, glm::mat4 m, ClipPlane p)
        : handle(h), dataset(std::move(ds)), transferFunction(std::move(tf)), model(m), plane(p) {}
    /// Transfer function carried BY VALUE on purpose: a TF is a small,
    /// immutable control-point ramp, and copying it into the per-frame
    /// instance removes any pointer that could dangle or be nulled mid-frame.
    /// Default ramp = grayscale black->white (the identity display ramp).
    volume::TransferFunction transferFunction{
        {{0.0f, {0, 0, 0, 0}}, {1.0f, {1, 1, 1, 1}}}};
    /// Model matrix: maps the dataset's model-space unit cube [0,1]^3 into
    /// world space (identity leaves the cube at [0,1]^3). Voxel-center index
    /// i sits at model coordinate i/(dim-1); the shader converts back via
    /// idx = modelPos*(dim-1).
    glm::mat4 model{1.0f};
    /// The world-space extraction plane: fragments are shaded where the
    /// pixel ray crosses THIS plane and the crossing lies inside the model
    /// cube. For axis-aligned slices through voxel-layer centers the point
    /// coordinate is index + 0.5 on the held axis.
    ClipPlane plane{};
};

/// A scene of extracted-slice instances to render (CPU-side; app/broker build
/// these).
struct VolumeSliceScene {
    std::vector<VolumeSliceInstance> slices;
};

/// Stateless GPU volume-plane extraction renderer.
///
/// Owns only GL resources: the cached extraction shader program and one
/// shared full-screen quad (every slice covers the whole viewport; coverage
/// outside the plane/volume intersection writes transparent black). GPU 3D
/// textures resolve through the shared `AssetRegistry` (SPEC §7 T14, T7
/// owner-driven): each slice carries a `VolumeTextureHandle` minted at
/// register time (hashed at load/register time, never per frame per
/// data/content_hash.hpp:31); the renderer resolves via O(1) handle
/// (`resolveVolume`, content-hash IS identity, no per-frame FNV-1a, no lazy
/// lookupVolume path), so the extraction path never duplicates a dataset
/// upload and no per-renderer texture map exists.
class VolumeSliceRenderer {
   public:
    /// Construct with the shared asset store (SHARED ownership: the renderer
    /// co-owns the registry with every other renderer — declaration order can
    /// never dangle it). Defaults to the process-wide registry
    /// (`AssetRegistry::shared()`). A null pointer is accepted at
    /// construction so member-init order never matters, but every
    /// render/drawLayer validates it and returns a typed error (code 4)
    /// instead of dereferencing.
    explicit VolumeSliceRenderer(
        std::shared_ptr<AssetRegistry> assets = AssetRegistry::shared());

    VolumeSliceRenderer(const VolumeSliceRenderer&) = delete;
    VolumeSliceRenderer& operator=(const VolumeSliceRenderer&) = delete;

    /// Draw one layer into the currently-bound framebuffer (ReView's
    /// ViewTarget), assuming ReView already performed bind+viewport+clear via
    /// REContext::current() (T2 global per-GL-context, 2 layers sharing viewport
    /// issue 1 glViewport). Does not clear — a second layer (e.g. a contour
    /// overlay) must not erase the first.
    data::Result<void> drawLayer(const VolumeSliceScene& scene,
                                 const Camera& camera);

    /// The shared asset store textures resolve through (non-null after a
    /// valid construction; null only if constructed with nullptr — renders
    /// then fail with typed error code 4).
    const std::shared_ptr<AssetRegistry>& assets() const noexcept {
        return assets_;
    }

   private:
    /// Build (and cache) the extraction shader program, returning a pointer
    /// to the cached program (non-null on success).
    data::Result<core::ShaderProgram*> sliceProgram();

    /// Ensure the shared full-screen quad geometry is uploaded, returning a
    /// pointer to the cached vertex array (non-null on success).
    /// @note lifetime: non-owning view of renderer-owned storage (the
    /// screenQuad_ `optional<>` member) — valid while this renderer is.
    data::Result<core::VertexArray*> screenQuad();

    /// Upload the transfer function `tf` to `program` as the TF control-point
    /// uniforms (uTfCount/uTfValues/uTfColors).
    void uploadTransferFunction(const volume::TransferFunction& tf,
                                core::ShaderProgram* program) const;

    /// Shared per-instance draw sequence used by both entry points (the
    /// caller has already validated the store and prepared the program):
    /// binds textures/uniforms for `instance` and issues one full-screen
    /// quad draw. `camera` feeds the ray-reconstruction matrix.
    data::Result<void> drawOne(const VolumeSliceInstance& instance,
                               const Camera& camera,
                               core::ShaderProgram* program);

    std::shared_ptr<AssetRegistry> assets_;
    LazyProgramCache program_;
    std::optional<ScreenQuad> screenQuad_;
    mutable std::unordered_map<const data::VolumeDataset*, VolumeTextureHandle>
        legacyHandleCache_;
};

} // namespace re::render
