#pragma once

// render/slice_renderer.hpp — SliceRenderer: geometry-shader plane clip of a
// mesh (SPEC §3, FR-render.4).
//
// A mesh-family technique: the geometry shader clips each triangle against a
// plane in world space and emits the clipped mesh (the kept side) for
// rendering, plus the on-plane cross-section polygon. It reuses the shared
// MeshGeometry (mesh geometry handling) and the IMaterial interface, exactly
// like MeshRenderer, and does NOT use OIT in v1 (SPEC §3 "Slicing is geometry,
// not compositing").
//
// Stateless rendering: render()/captureCrossSection() receive all of their data
// per call; the renderer owns only GL resources (its cached clip shader
// program and its transform-feedback capture program + object). GPU geometry
// is owned by the shared AssetRegistry (SPEC §9 V2.5): scenes carry
// AssetHandles and the renderer resolves them through the injected registry,
// sharing one GPU object per CPU mesh with MeshRenderer and every view.
//
// render/ is GL-call-free: it draws through the core::Draw API and core/ RAII
// objects, and captures cross-section vertices through core::TransformFeedback
// (guardrail gpu_api_ownership). captureCrossSection() is a test-consumed
// readback path (guardrail no_production_readback): the render path never reads
// back from the GPU.

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <optional>
#include <vector>

#include "core/re_context.hpp"
#include "core/framebuffer.hpp"
#include "core/shader_program.hpp"
#include "core/transform_feedback.hpp"
#include "core/vertex_buffer.hpp"
#include "data/result.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_geometry.hpp"
#include "render/mesh_renderer.hpp"
#include "render/shader_cache.hpp"
#include "render/types.hpp" // IRenderer / render::Scene

namespace re::render {

/// A scene of mesh instances to clip and slice (reuses MeshInstance from
/// MeshRenderer; CPU-side, app/ builds these).
///
/// `plane` is the clip plane used by the IRenderer dispatch path
/// (SliceRenderer::render(scene, camera, target), SPEC §9 V2.3): the dispatch
/// contract carries no separate plane parameter, so the scene itself carries
/// the plane it should be sliced against. The concrete 4-argument render
/// (which receives the plane explicitly) is unaffected by this member.
struct SliceScene {
    std::vector<MeshInstance> meshes;
    ClipPlane plane; ///< Clip plane for the IRenderer dispatch path.
};

/// Stateless geometry-shader plane-clip renderer (SPEC §3).
///
/// Owns only GL resources: the cached clip shader program (vertex + geometry +
/// fragment), the cached transform-feedback capture program, and a transform
/// feedback object. Mesh geometry lives in the shared AssetRegistry (SPEC §9
/// V2.5): the renderer resolves each instance's AssetHandle through the
/// injected registry, so a data::Mesh is uploaded to the GPU once — even
/// across MeshRenderer + SliceRenderer.
class SliceRenderer : public IRenderer {
   public:
    /// Construct with the shared asset registry (SHARED ownership, T13 — see
    /// MeshRenderer). A null registry is accepted at construction so member
    /// init order never matters; every draw validates it and returns a typed
    /// error (code 4) instead of dereferencing. Slicing does not use OIT in
    /// v1.
    explicit SliceRenderer(std::shared_ptr<AssetRegistry> registry);

    SliceRenderer(const SliceRenderer&) = delete;
    SliceRenderer& operator=(const SliceRenderer&) = delete;

    /// Render the meshes of `scene` clipped against `plane` into `target` from
    /// `camera`. Each mesh is clipped to the kept side
    /// (`dot(normal, p - point) >= 0`); the emitted clipped-mesh triangles are
    /// shaded with each instance's material (deterministic v1 flat lighting,
    /// see docs/render.md). On success the target framebuffer is left bound (so
    /// tests can read it back). Returns a typed error if the clip shader fails
    /// to build, an instance's handle fails to resolve (stale/dangling), or a
    /// draw cannot be issued.
    data::Result<void> render(const SliceScene& scene, const Camera& camera,
                              const ClipPlane& plane,
                              const RenderTarget& target);

    /// IRenderer dispatch (SPEC §9 V2.3): renders when `scene` holds a
    /// SliceScene, clipping it against the plane carried by the scene itself
    /// (`scene.plane`); returns a typed error when it holds a different
    /// technique (SPEC §5, no exceptions).
    data::Result<void> render(const Scene& scene, const Camera& camera,
                               const RenderTarget& target) override;

    /// Draw one layer into the currently-bound framebuffer (ReView's ViewTarget),
    /// assuming ReView already performed bind+viewport+clear via REContext::current()
    /// (T2 global per-GL-context, 2 layers sharing viewport issue 1 glViewport).
    /// Does not clear — second layer must not clear away the first. Clips against
    /// the plane carried by the scene (slice.plane) for the IRenderer path; the
    /// explicit-plane overload uses the passed plane.
    data::Result<void> drawLayer(const SliceScene& scene, const Camera& camera);
    /// Explicit-plane layer variant (used when View's ClipPlane supplies the
    /// plane, not the scene).
    data::Result<void> drawLayer(const SliceScene& scene, const Camera& camera,
                                 const ClipPlane& plane);

    /// Capture the on-plane cross-section vertices emitted by the geometry
    /// shader for `scene` clipped against `plane` into `out` (world-space
    /// positions). This is a test-consumed readback path (guardrail
    /// no_production_readback) used by the FR-render.4 gate to assert every
    /// emitted cross-section vertex lies on the clip plane. Returns a typed
    /// error if the capture program fails to build, an instance's handle fails
    /// to resolve, or the capture cannot be issued.
    data::Result<void> captureCrossSection(const SliceScene& scene,
                                           const ClipPlane& plane,
                                           std::vector<glm::vec3>& out);

    /// The shared asset registry instances' handles resolve through (non-null
    /// after a valid construction; null only if constructed with nullptr —
    /// draws then fail with typed error code 4).
    const std::shared_ptr<AssetRegistry>& assetRegistry() const noexcept {
        return registry_;
    }

   private:
    /// Build (and cache) the clip shader program, returning a pointer to the
    /// cached program (non-null on success).
    data::Result<core::ShaderProgram*> clipProgram();

    /// Build (and cache) the transform-feedback capture program, returning a
    /// pointer to the cached program (non-null on success).
    data::Result<core::ShaderProgram*> captureProgram();

    /// Ensure the shared transform-feedback object and capture buffer exist,
    /// returning a pointer to the transform-feedback object.
    data::Result<core::TransformFeedback*> captureFeedback();

    /// The ONE shared clip loop behind both entry points (render() after its
    /// pass prologue, drawLayer() as a View layer): installs `program` and
    /// draws every resolvable instance of `scene` clipped against `plane`
    /// (single copy of the uniform + draw sequence). Slicing deliberately has
    /// NO transparency path in v1 — see the note in the .cpp loop.
    data::Result<void> clipInstances(const SliceScene& scene,
                                     const Camera& camera,
                                     const ClipPlane& plane,
                                     core::ShaderProgram* program);

    std::shared_ptr<AssetRegistry> registry_;

    LazyProgramCache clipProgram_;
    LazyProgramCache captureProgram_;
    std::optional<core::TransformFeedback> captureFeedback_;
    std::optional<core::VertexBuffer> captureBuffer_;
};

} // namespace re::render
