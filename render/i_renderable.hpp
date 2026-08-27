#pragma once

// render/i_renderable.hpp — IRenderable type-erased draw interface (SPEC §3.2 V3.4 T5).
//
// View (ReView) owns a heterogeneous list<IRenderable> (VolumeSlice+MeshSlice for
// 2D, Volume+Mesh for 3D). Each IRenderable is type-erased drawLayer(Camera)
// (formerly drawLayer(Camera,DrawContext&), T2 — per-frame local ctx deleted,
// REContext::current() is the global per-GL-context single writer) — View never
// knows the renderer. Renderers gain drawLayer(SceneT,Camera) assuming ReView
// already bind+viewport+clear via REContext::current(); single-item render()
// uses the same global current.
// Type-erasure is via virtual IRenderable, not std::function (avoids per-draw allocation + keeps
// AssetRegistry ownership clear).

#include "core/re_context.hpp"
#include "data/result.hpp"
#include "render/types.hpp"

namespace re::render {

/// Type-erased renderable: one drawLayer call for ReView compositing.
///
/// T2: global per-GL-context REContext (formerly DrawContext, T2 rename) —
///
/// View owns a heterogeneous list<IRenderable> and each IRenderable is
/// type-erased drawLayer(Camera) — View never knows the renderer. The per-GL-
/// context REContext::current() (thread_local GLFWwindow* → REContextState) owns
/// the mirror (viewport, clearColor, depthTest, blend, blendFunc, cull, FBO/VAO/
/// program/image units) so 2 layers sharing state within the same GL context
/// issue only 1 glViewport (cross-pass dedup spy, T2). Renderers gain
/// drawLayer(SceneT,Camera) assuming ReView already bind+viewport+clear via the
/// global REContext::current(); single-item render() uses the same global
/// current (per-frame local ctx instances deleted, T2). The (void)ctx params
/// that were previously ignored are now dropped — the global current is the
/// single writer (single responsibility via REContext). Type-erasure remains
/// virtual IRenderable, not std::function (avoids per-draw allocation).
class IRenderable {
   public:
    virtual ~IRenderable() = default;

    /// Draw one layer into the currently-bound framebuffer (ReView's ViewTarget),
    /// assuming View already performed bind+viewport+clear via REContext::current().
    /// Must not clear — second layer must not clear away the first. Returns a
    /// typed error if the draw cannot be issued (e.g., stale handle).
    virtual data::Result<void> drawLayer(const Camera& camera) = 0;

    /// Deprecated compat shim: old code passed a per-frame DrawContext/REContext
    /// (DrawContext is alias to REContext in T2). New code uses REContext::current()
    /// (T2 global per-GL-context). The passed context is ignored — the global
    /// current is the single writer. Kept so old View tests that still call
    /// drawLayer(camera, ctx) compile; one overload suffices because the alias
    /// makes the types identical.
    virtual data::Result<void> drawLayer(const Camera& camera, core::REContext& /*ctx*/) {
        return drawLayer(camera);
    }
};

} // namespace re::render
