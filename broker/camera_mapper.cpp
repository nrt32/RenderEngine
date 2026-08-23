// broker/camera_mapper.cpp — CameraMapper cached translation (no GL).
// T4 V3.3: validates 2D ortho vs 3D perspective (plane present → ortho).

#include "broker/camera_mapper.hpp"

#include "data/result.hpp"

namespace re::broker {

data::Result<render::Camera> CameraMapper::map(
    const scene::Camera& app, const scene::TranslateContext& ctx) const {
    // 2D (plane present) must be orthographic, 3D (no plane) must be perspective.
    const bool hasPlane = ctx.hasPlane();
    const bool isOrtho = app.isOrthographic();
    const bool isPersp = app.isPerspective();
    if (hasPlane && isPersp) {
        return data::makeError<render::Camera>(
            4, "2D view with plane requires orthographic camera (plane present -> ortho)");
    }
    if (!hasPlane && isOrtho) {
        return data::makeError<render::Camera>(
            4, "3D view without plane requires perspective camera (no plane -> perspective)");
    }
    render::Camera out;
    out.view = app.viewMatrix();
    out.proj = app.projMatrix();
    out.position = app.eye();
    return data::makeValue<render::Camera>(out);
}

data::Result<render::Camera> CameraMapper::mapCached(
    const scene::Camera& app, const scene::TranslateContext& ctx) {
    // Validate before cache probe — a cached valid result must never be
    // returned for a mismatched 2D/3D context (plane present → ortho).
    const bool hasPlane = ctx.hasPlane();
    const bool isOrtho = app.isOrthographic();
    const bool isPersp = app.isPerspective();
    if (hasPlane && isPersp) {
        return data::makeError<render::Camera>(
            4, "2D view with plane requires orthographic camera (plane present -> ortho)");
    }
    if (!hasPlane && isOrtho) {
        return data::makeError<render::Camera>(
            4, "3D view without plane requires perspective camera (no plane -> perspective)");
    }
    if (hasCache_ && lastViewGen_ == app.viewGen() && lastProjGen_ == app.projGen()) {
        return data::makeValue<render::Camera>(cached_);
    }
    auto r = map(app, ctx);
    if (r.ok()) {
        cached_ = *r;
        lastViewGen_ = app.viewGen();
        lastProjGen_ = app.projGen();
        hasCache_ = true;
    }
    return r;
}

void CameraMapper::invalidate(uint64_t /*id*/) {
    hasCache_ = false;
    lastViewGen_ = ~0ULL;
    lastProjGen_ = ~0ULL;
}

} // namespace re::broker
