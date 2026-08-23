#pragma once

// broker/camera_mapper.hpp — CameraMapper: ICachedMapper<scene::Camera, render::Camera> (T3 V3.2b, T4 V3.3).
//
// One file per mapper (guardrail broker_per_type). Cached (per-field viewGen/projGen)
// so a Camera::rotate(1°) dirties only viewGen, not projGen (SPEC §10.4, T4). Pure
// translation is viewMatrix/projMatrix/position forwarding — no GL, no core/ GL
// calls (gpu_api_ownership: broker stays outside core/). T4 validates 2D ortho vs
// 3D perspective (plane present → ortho) via TranslateContext::hasPlane().

#include "broker/i_mapper.hpp"
#include "render/types.hpp"
#include "scene/camera.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Camera mapper — cached translation scene::Camera -> render::Camera.
///
/// Cache key is (viewGen, projGen) per SPEC §10.4 per-field split; short-circuits
/// when both match last cached values. Validates that a 2D view (plane present)
/// carries an orthographic camera and a 3D view (no plane) carries a perspective
/// camera (T4 V3.3, SPEC §3.1); mismatch returns typed error code 4.
class CameraMapper : public ICachedMapper<scene::Camera, render::Camera> {
   public:
    using AppType = scene::Camera;
    using ReType = render::Camera;
    CameraMapper() = default;

    /// Pure translation (no cache probe): app.viewMatrix/projMatrix/eye -> Re camera.
    data::Result<render::Camera> map(const scene::Camera& app,
                                     const scene::TranslateContext& ctx) const override;

    /// Cached translation: returns cached Re camera when viewGen+projGen unchanged.
    data::Result<render::Camera> mapCached(const scene::Camera& app,
                                           const scene::TranslateContext& ctx) override;

    /// Invalidate cached camera (e.g. viewId removed). Clears cache.
    void invalidate(uint64_t /*id*/) override;

   private:
    uint64_t lastViewGen_{~0ULL};
    uint64_t lastProjGen_{~0ULL};
    render::Camera cached_{};
    bool hasCache_{false};
};

} // namespace re::broker
