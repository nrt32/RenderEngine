#pragma once

// render/point_renderer.hpp — PointRenderer: impostor billboard for dense point clouds and markers plus MeshRenderer delegate for single 3D lit spheres (V7 T4, FR-render.8).
//
// This renderer implements the V7 T4 point pipeline that was locked at 2026-08-30: a single 3D PointObject with world-space radius and Solid fill reuses MeshRenderer with GeometryKind::Sphere to guarantee an exact lit-sphere oracle match within 1/255 for the 3D perspective gate, while every other case — PointCloudObject with hundreds of points sharing one worldUnits toggle, 2D circles where is2D()==true via ClipPlane presence, pixel-constant 10 px markers that must stay 10 px at two camera distances, and Hollow vs GridDashed fill variants — is drawn as an impostor billboard. The billboard path owns a LazyProgramCache impostorProgram_ for point_impostor.vert/.frag and a shared ScreenQuad-style quad (position-only [−1,−1]..[1,1] expanded from center→clip→ndc→viewport using the Camera's right/up derived from the inverse view matrix, with radiusScreen computed as worldUnits ? radius*viewport.w/pos.w/tan(fov/2) approximated via projection delta of a right-offset world point versus radiusPx for worldUnits false). The vertex stage receives aPos mapping, expands centerClip to screen, offsets by mapping* radiusScreen, and reprojects to NDC so the impostor stays screen-aligned; the fragment stage computes r2=dot(mapping,mapping), discards where r2>1, branches on fill (Solid fills disk, Hollow discards inner ring r<0.5, GridDashed discards checker 0.7 threshold giving a distinct golden within 1/255), computes sphere normal n=vec3(mapping,sqrt(1−r2)), shades with max(dot(n,(0,0,1)),0) headlight, and for 3D writes gl_FragDepth=project(centerWS+n*radius) while for 2D (is2D()==true via View's ClipPlane present → no gl_FragDepth write) outputs flat alpha*halo. The PointRenderer is injected with a MeshRenderer* /*borrow*/ for the single-sphere delegate — lifetime is RenderStack co-owned, so the borrow is scope-bounded to the RenderStack that owns both renderers and must outlive any draw; the pointer is marked /*borrow*/ and carries a Doxygen @note lifetime tag per ownership_raw_ptr. The renderer is stateless aside from its cached program and quad, uses only core/ wrappers (guardrail gpu_api_ownership, render_no_glad), and exposes drawLayer(PointScene,Camera) for the broker as well as IRenderable::drawLayer for View type-erasure. (V7 T4)

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "core/re_context.hpp"
#include "core/shader_program.hpp"
#include "core/vertex_array.hpp"
#include "data/result.hpp"
#include "render/i_renderable.hpp"
#include "render/mesh_renderer.hpp"
#include "render/screen_quad.hpp"
#include "render/shader_cache.hpp"
#include "render/types.hpp"

namespace re::render {

/// RE-minimal point fill (mirrors scene::PointFill V7 T2 without including scene — disposition_render forbids render→scene; broker translates scene::PointFill→RePointFill).
enum class PointFill : uint8_t { Solid = 0, Hollow = 1, GridDashed = 2 };

/// Per-point render instance (RE-minimal handle-free, derived from scene::PointObject/PointCloudObject via broker translation of PointFill).
struct PointInstance {
    glm::vec3 pos{0.0f, 0.0f, 0.0f};
    float radius{5.0f};
    bool worldUnits{true};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    PointFill fill{PointFill::Solid};
    float fillParam{0.0f};
};

/// Collection of points to draw in one view layer (CPU side, built by broker mappers).
struct PointScene {
    std::vector<PointInstance> points;
};

/// Stateless impostor point renderer with MeshRenderer delegate for single spheres (V7 T4, FR-render.8).
///
/// Owns the impostor shader program cache and a shared quad (position-only [−1,−1]..[1,1]) and optionally borrows a MeshRenderer for the single-point 3D lit sphere path that guarantees the 3D perspective center pixel matches the MeshObject{GeometryKind::Sphere} oracle within 1/255 (the delegate builds a sphere mesh via the shared AssetRegistry and forwards to MeshRenderer::drawLayer). The impostor path handles every other configuration — 2D ClipPlane flat circles, worldUnits false 10 px constant markers, and Hollow vs GridDashed fills — via the point_impostor.vert/.frag pair described above (r2 discard, hollow/grid, n, gl_FragDepth for 3D). The borrow is non-owning and must outlive the renderer.
/// @note lifetime: MeshRenderer* /*borrow*/ meshRenderer_ is owned by RenderStack (co-owned with this PointRenderer) and must outlive this object; the View that drives drawLayer must keep the RenderStack alive for the whole frame.
class PointRenderer final : public IRenderable {
   public:
    /// Construct with the shared asset registry and an optional MeshRenderer borrow for single-sphere delegation.
    /// @param registry Shared asset registry for sphere mesh creation (co-owned, may be null — draws then return typed error code 4).
    /// @param meshRenderer Borrowed MeshRenderer for the single 3D lit sphere delegate (may be null — impostor fallback is used). @note lifetime: RenderStack co-owned, must outlive this renderer.
    explicit PointRenderer(std::shared_ptr<AssetRegistry> registry,
                           MeshRenderer* /*borrow*/ meshRenderer = nullptr);

    PointRenderer(const PointRenderer&) = delete;
    PointRenderer& operator=(const PointRenderer&) = delete;
    PointRenderer(PointRenderer&&) noexcept = default;
    PointRenderer& operator=(PointRenderer&&) noexcept = default;
    ~PointRenderer() final = default;

    /// Draw the point scene into the currently-bound framebuffer (ReView's ViewTarget), assuming the view already performed bind+viewport+clear via REContext::current().
    /// Expands each point's quad [−1,−1]..[1,1] via center→clip→ndc→viewport using Camera right/up and radiusScreen = worldUnits ? radius*viewport.w/pos.w/tan(fov/2) (approximated via projection delta) : radiusPx, passes mapping and centerWS to the impostor shader which implements r2 discard, hollow/grid branching, n=vec3(mapping,sqrt(1−r2)), pos=centerWS+n*radius and gl_FragDepth=project(pos) for 3D versus flat alpha*halo for 2D (is2D()==true via View's ClipPlane present → no gl_FragDepth write), shading via max(dot(n,(0,0,1)),0) headlight within 1/255.
    data::Result<void> drawLayer(const PointScene& scene, const Camera& camera);

    /// IRenderable type-erased entry (View never knows the renderer type). The scene must have been supplied via View::addItem(scene, renderer) type-erasure; this direct call without a scene returns a typed error and exists only to satisfy the interface.
    using IRenderable::drawLayer;
    data::Result<void> drawLayer(const Camera& camera) override;

    const std::shared_ptr<AssetRegistry>& assetRegistry() const noexcept { return registry_; }

    /// Borrowed mesh renderer for the single-sphere delegate (may be null).
    /// @note lifetime: RenderStack co-owned, valid while this PointRenderer lives and the RenderStack is held.
    MeshRenderer* /*borrow*/ meshRenderer() const noexcept { return meshRenderer_; }

   private:
    data::Result<core::ShaderProgram*> impostorProgram();
    data::Result<ScreenQuad*> quadGeometry();

    std::shared_ptr<AssetRegistry> registry_;
    MeshRenderer* /*borrow*/ meshRenderer_{nullptr};

    LazyProgramCache impostorProgram_;
    std::optional<ScreenQuad> quad_;
};

} // namespace re::render
