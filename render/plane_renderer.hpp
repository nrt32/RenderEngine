#pragma once

// render/plane_renderer.hpp — PlaneRenderer: textured quads/planes (SPEC §3,
// FR-render.5).
//
// Sole owner of every textured-plane draw (V3.4b T12 audit): all textured-plane
// displays — the plane sample and the MPR slice views — reach the GPU ONLY
// through this renderer (drawLayer(PlaneScene, Camera, DrawContext&) inside a
// ReView's IRenderable list, or render() for direct single-target tests). The
// app side sends only scene::PlaneObject{image asset ref, transform,
// presentation}; broker/ mediates that value into the render::PlaneInstance
// consumed here. app/ never names PlaneGeometry and never parses quad
// corners/UVs into vertex buffers — the unit-quad VAO is built and owned by
// quadGeometry() below, PlaneGeometry::unitQuadXY() stays a render/-internal
// detail (reached through broker, not re-parsed by callers), and the
// data::Image → core::Texture2D upload lives in the SHARED asset store
// (`render::AssetRegistry`, SPEC §7 T14 — the RGBA8 conversion and row flip
// are part of that GPU-upload contract, not an app-side quad path).
//
// Stateless rendering: PlaneRenderer::render(scene, camera, target) receives
// all of its data per call; the renderer owns only GL resources (its cached
// textured-plane shader program and one shared unit-quad VAO/VBO). GPU
// textures live in the shared asset store: every image is content-hash-deduped
// into exactly one Texture2D per registry, so identical pixel content shares
// one GL texture id across instances and views, and no per-renderer
// pointer-keyed cache exists. This is the renderer MPRView drives for its
// Transverse/Coronal/Sagittal slice views (T14).
//
// Plane geometry is defined per-instance by PlaneGeometry (four world-space
// corners + per-corner UVs + a precomputed normal) and transformed by the
// instance's model matrix, so the renderer handles planes of any orientation.
// UV mapping convention (analytic, verified by the T8 gate): the plane's UV
// space maps the source image exactly once across the quad, with (u,v) = (0,0)
// at corner0 and (1,1) at corner2; the image's top-left pixel appears at the
// quad's top-left when viewed from the side the normal points toward (the
// store's row-flipping RGBA8 conversion). The model matrix maps the geometry's
// corners into world space (applied after the affine corner-box map the
// renderer builds).
//
// render/ is GL-call-free: it draws through the core::Draw API and core/ RAII
// objects (guardrail gpu_api_ownership).

#include <array>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/re_context.hpp"
#include "core/element_buffer.hpp"
#include "core/framebuffer.hpp"
#include "core/shader_program.hpp"
#include "core/texture2d.hpp"
#include "core/vertex_array.hpp"
#include "core/vertex_buffer.hpp"
#include "data/image.hpp"
#include "data/result.hpp"
#include "render/asset_registry.hpp"
#include "render/types.hpp" // render::Camera / render::RenderTarget

namespace re::render {

/// A planar quad in world space (before the instance's model transform):
/// four corners, per-corner texture coordinates, and the plane's unit normal.
///
/// Corner/UV binding (the plane's UV mapping, FR-render.5): corner `i` carries
/// UV `uv[i]`, so (u,v) = (0,0) at corner0 and (1,1) at corner2. The normal is
/// precomputed (analytic cross product of the corner0->corner1 and
/// corner0->corner3 edges, normalized); the rendered surface is the two
/// triangles (corner0, corner1, corner2) and (corner0, corner2, corner3).
struct PlaneGeometry {
    /// The four corners in order corner0..corner3.
    std::array<glm::vec3, 4> corners;

    /// Per-corner UVs, indexed like corners.
    std::array<glm::vec2, 4> uv;

    /// Unit normal of the plane (analytic: normalized cross product of the
    /// edges corner0->corner1 and corner0->corner3).
    glm::vec3 normal{0.0f, 0.0f, 1.0f};

    /// Build the unit quad: the XY plane square [-1,1]^2 at z = 0, normal
    /// (0,0,1), with the standard UV mapping corner0=(-1,-1)->(0,0),
    /// corner1=(1,-1)->(1,0), corner2=(1,1)->(1,1), corner3=(-1,1)->(0,1).
    static PlaneGeometry unitQuadXY();
};

/// A single plane in a scene: its geometry, the image handle (owner-driven)
/// + shared image ref, and model transform. The model matrix places the quad
/// corners in world space and orients the plane (applied to vertices and
/// normals).
///
/// Produced by broker::PlaneObjectMapper from scene::PlaneObject (V3.4b T12) — app/
/// code does not assemble instances by hand.
///
/// Ownership (T13): geometry is SHARED (`shared_ptr<const PlaneGeometry>`);
/// image identity is owner-driven via `ImageTextureHandle` (T7, hashed at
/// register time, never per frame) — the `image` shared_ptr is retained for
/// CPU convenience but GPU identity is the handle (O(1) handle resolve).
struct PlaneInstance {
    /// Shared reference to the plane geometry (PlaneMapper's program-duration
    /// shared unit quad). Null is invalid by contract: render/drawLayer
    /// reject it with a typed error instead of dereferencing, so a
    /// half-constructed instance can never reach GL.
    std::shared_ptr<const PlaneGeometry> geometry = nullptr;
    /// Owner-driven handle to the GPU 2D texture (hashed at register time).
    /// Must be non-null for draw (typed error if null).
    ImageTextureHandle handle{};
    /// Shared reference to the source image for CPU convenience (not GPU identity).
    std::shared_ptr<const data::Image> image = nullptr;
    glm::mat4 model{1.0f};

    PlaneInstance() = default;
    // Legacy 3-arg ctor for pre-T7 direct-renderer tests: leaves handle null so the renderer's fallback legacyHandleCache registers the image once at first use and then hits O(1) without per-frame hashing (T7 owner-driven path prefers explicit handle via AssetRegistry::registerImage; this shim keeps old fixtures green while new broker-mediated code and the T7 gate use explicit handles, hashed at load/register time per data/content_hash.hpp:31, never per frame).
    PlaneInstance(std::shared_ptr<const PlaneGeometry> g,
                  std::shared_ptr<const data::Image> img, glm::mat4 m)
        : geometry(std::move(g)), handle{}, image(std::move(img)), model(m) {}
    PlaneInstance(std::shared_ptr<const PlaneGeometry> g, ImageTextureHandle h,
                  std::shared_ptr<const data::Image> img, glm::mat4 m)
        : geometry(std::move(g)), handle(h), image(std::move(img)), model(m) {}
};

/// A scene of plane instances to render (CPU-side; built by mapping
/// scene::PlaneObject values through broker::PlaneMapper).
struct PlaneScene {
    std::vector<PlaneInstance> planes;
};

/// Stateless textured-plane renderer (SPEC §3; feeds MPR, T14).
///
/// Owns only GL resources: the cached textured-plane shader program and one
/// shared unit-quad vertex array (all planes are unit quads transformed by
/// their model matrix). GPU textures live in the shared `AssetRegistry`
/// (SPEC §7 T14, T7 owner-driven): each instance carries an
/// `ImageTextureHandle` minted at register time (hashed at load/register time,
/// never per frame per data/content_hash.hpp:31); the renderer resolves via
/// O(1) handle (`resolveImage`, content-hash IS identity, no per-frame
/// FNV-1a, no lazy lookupImage path), so each distinct image is uploaded once
/// per store and no per-renderer texture map exists. Textures are sampled with
/// GL_LINEAR and CLAMP_TO_EDGE (core::Texture2D defaults), so a quad mapped
/// 1:1 onto the viewport reproduces the source texels exactly (FR-render.5).
class PlaneRenderer : public IRenderer {
   public:
    /// Construct with the shared asset store (SHARED ownership: the renderer
    /// co-owns the registry with every other renderer — declaration order can
    /// never dangle it). Defaults to the process-wide registry
    /// (`AssetRegistry::shared()`), so two default-constructed renderers share
    /// one GPU texture per content. A null pointer is accepted at construction
    /// so member-init order never matters, but every render/drawLayer validates
    /// it and returns a typed error (code 4) instead of dereferencing.
    explicit PlaneRenderer(
        std::shared_ptr<AssetRegistry> assets = AssetRegistry::shared());

    /// Render `scene` into `target` from `camera`. On success the target
    /// framebuffer is left bound (so tests can read it back). Returns a typed
    /// error if the shader fails to build, a texture upload fails, or a draw
    /// cannot be issued.
    data::Result<void> render(const PlaneScene& scene, const Camera& camera,
                              const RenderTarget& target);

    /// Type-erased dispatch entry (the IRenderer contract): renders when
    /// `scene` holds a PlaneScene; a scene of any OTHER technique is rejected
    /// with a typed error rather than thrown or silently ignored, so a wrong
    /// renderer/scene pairing surfaces at the call site.
    data::Result<void> render(const Scene& scene, const Camera& camera,
                               const RenderTarget& target) override;

    /// Draw one layer into the currently-bound framebuffer (ReView's ViewTarget),
    /// assuming ReView already performed bind+viewport+clear via REContext::current()
    /// (T2 global per-GL-context, 2 layers sharing viewport issue 1 glViewport).
    /// Does not clear — second layer must not clear away the first.
    data::Result<void> drawLayer(const PlaneScene& scene, const Camera& camera);

    /// The shared asset store textures resolve through (non-null after a valid
    /// construction; null only if constructed with nullptr — renders then fail
    /// with typed error code 4).
    const std::shared_ptr<AssetRegistry>& assets() const noexcept {
        return assets_;
    }

   private:
    /// Build (and cache) the textured-plane shader program, returning a
    /// pointer to the cached program (non-null on success).
    data::Result<core::ShaderProgram*> planeProgram();

    /// Ensure the shared unit-quad geometry is uploaded, returning a pointer
    /// to the cached vertex array (non-null on success).
    /// @note lifetime: non-owning view of renderer-owned storage (the
    /// quadVao_ `optional<>` member) — valid while this renderer is.
    data::Result<core::VertexArray*> quadGeometry();

    /// The ONE shared instance-draw loop behind both entry points
    /// (render() after its pass prologue, drawLayer() as a View layer):
    /// installs `program`, maps the shared unit quad onto each instance's
    /// corner box + model transform (single copy of the basis-matrix math),
    /// binds textures through the store, and issues one indexed draw per
    /// instance using the shared kQuadTriangleIndices pattern.
    data::Result<void> drawInstances(const PlaneScene& scene,
                                     const Camera& camera,
                                     core::ShaderProgram* program,
                                     core::VertexArray* quadVao);

    /// Resolve `handle`'s content in the shared asset store (O(1) handle
    /// resolve, T7 owner-driven, no per-frame hash), returning a pointer to
    /// the store-owned texture (non-null on success).
    /// @note lifetime: non-owning view of store-owned storage (the slot's
    /// unique_ptr) — valid until the slot's last reference is released or the
    /// store dies; renderers never retain it across frames.
    data::Result<core::Texture2D*> textureFor(
        const ImageTextureHandle& handle);

    std::shared_ptr<AssetRegistry> assets_;
    std::optional<core::ShaderProgram> planeProgram_;
    std::optional<core::VertexArray> quadVao_;
    std::optional<core::VertexBuffer> quadVbo_;
    std::optional<core::ElementBuffer> quadEbo_; // index buffer referenced by quadVao_
    mutable std::unordered_map<const data::Image*, ImageTextureHandle>
        legacyHandleCache_;
};

} // namespace re::render
