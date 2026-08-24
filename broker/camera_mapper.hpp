#pragma once

// broker/camera_mapper.hpp — CameraMapper: ICachedMapper<scene::Camera, render::Camera> (T3 V3.2b, T4 V3.3).
//
// One file per mapper (guardrail broker_per_type). The per-field viewGen/
// projGen split means a Camera::rotate(1°) dirties only viewGen, not projGen
// (SPEC §10.4, T4) — the SYNCHRONIZER uses those generations to decide WHETHER
// this mapper runs at all. Pure translation is viewMatrix/projMatrix/position
// forwarding — no GL, no core/ GL calls (gpu_api_ownership: broker stays
// outside core/). T4 validates 2D ortho vs 3D perspective (plane present →
// ortho) via TranslateContext::hasPlane().
//
// Why mapCached currently does NOT memoize (T20): the former single-slot
// cache keyed on (viewGen, projGen) collided across VIEWS — independent
// scene::Camera instances each start their generations at zero, so a sync
// over several views served the first view's matrices to every later one
// (the MPR sample's 2D/3D views rendered through each other's cameras). A
// correct memo needs a per-camera identity (id-keyed multi-entry cache,
// tracked as the T21 persistence-honesty follow-up); until scene::Camera
// carries an id through the sync path, translation recomputes — two glm
// matrix constructions per dirty camera, correctness over a micro-optimization.

#include "broker/i_mapper.hpp"
#include "render/types.hpp"
#include "scene/camera.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Camera mapper — translation scene::Camera -> render::Camera.
///
/// Validates that a 2D view (plane present) carries an orthographic camera
/// and a 3D view (no plane) carries a perspective camera (T4 V3.3, SPEC
/// §3.1); mismatch returns typed error code 4 — on BOTH entry points, never
/// bypassed.
class CameraMapper : public ICachedMapper<scene::Camera, render::Camera> {
   public:
    using AppType = scene::Camera;
    using ReType = render::Camera;
    CameraMapper() = default;

    /// Pure translation: app.viewMatrix/projMatrix/eye -> Re camera.
    data::Result<render::Camera> map(const scene::Camera& app,
                                     const scene::TranslateContext& ctx) const override;

    /// Cached-translation contract point: validates then translates (see the
    /// header comment for why there is no memo behind it right now).
    data::Result<render::Camera> mapCached(const scene::Camera& app,
                                           const scene::TranslateContext& ctx) override;

    /// Invalidate any memoized state for `id` (no-op while unmemoized; kept
    /// so the ICachedMapper contract and callers stay stable).
    void invalidate(uint64_t /*id*/) override;
};

} // namespace re::broker
