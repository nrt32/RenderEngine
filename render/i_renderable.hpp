#pragma once

// render/i_renderable.hpp — IRenderable type-erased draw interface (SPEC §3.2 V3.4 T5).
//
// View (ReView) owns a heterogeneous list<IRenderable> (VolumeSlice+MeshSlice for
// 2D, Volume+Mesh for 3D). Each IRenderable is type-erased drawLayer(Camera,DrawContext&)
// — View never knows the concrete renderer. Renderers gain drawLayer(SceneT,Camera,DrawContext&)
// assuming ReView already bind+viewport+clear; single-item render() keeps clear for direct tests.
// Type-erasure is via virtual IRenderable, not std::function (avoids per-draw allocation + keeps
// AssetRegistry ownership clear).

#include "core/draw.hpp"
#include "data/result.hpp"
#include "render/types.hpp"

namespace re::render {

/// Type-erased renderable: one drawLayer call for ReView compositing.
///
/// Concrete renderers implement drawLayer(SceneT,Camera,DrawContext&) and expose a
/// factory or View::addItem<SceneT> helper that wraps the typed scene into this
/// interface. The View iterates its list and calls drawLayer(camera, ctx) per item
/// after it has bound its ViewTarget, set viewport via ctx, and cleared.
class IRenderable {
   public:
    virtual ~IRenderable() = default;

    /// Draw one layer into the currently-bound framebuffer (ReView's ViewTarget),
    /// assuming View already performed bind+viewport+clear via the same DrawContext.
    /// Must not clear — second layer must not clear away the first. Returns a
    /// typed error if the draw cannot be issued (e.g., stale handle).
    virtual data::Result<void> drawLayer(const Camera& camera, core::DrawContext& ctx) = 0;
};

} // namespace re::render
