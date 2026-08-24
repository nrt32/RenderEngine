// broker/camera_mapper.cpp — CameraMapper implementation: map the app-side
// scene::Camera onto render::Camera (view/proj/eye). The mapper also VALIDATES
// the 2D/3D pairing: when the context carries a slice plane the projection
// must be orthographic; with no plane it must be perspective — a mismatch is
// a typed error, not silently wrong geometry. There is deliberately no
// memoization behind mapCached right now (see the header comment: the old
// single-slot gen-keyed entry collided across views).

#include "broker/camera_mapper.hpp"

#include "data/result.hpp"

namespace re::broker {
namespace {

/// The shared 2D/3D pairing validation (plane present -> ortho). Returns the
/// typed error for a mismatch, or success.
data::Result<void> validateProjectionPairing(const scene::Camera& app,
                                             const scene::TranslateContext& ctx) {
    const bool hasPlane = ctx.hasPlane();
    if (hasPlane && app.isPerspective()) {
        return data::makeError<void>(
            4, "2D view with plane requires orthographic camera (plane "
               "present -> ortho)");
    }
    if (!hasPlane && app.isOrthographic()) {
        return data::makeError<void>(
            4, "3D view without plane requires perspective camera (no plane "
               "-> perspective)");
    }
    return data::Result<void>(data::value);
}

} // namespace

data::Result<render::Camera> CameraMapper::map(
    const scene::Camera& app, const scene::TranslateContext& ctx) const {
    auto valid = validateProjectionPairing(app, ctx);
    if (valid.failed()) {
        return data::makeError<render::Camera>(valid.error().code,
                                               valid.error().message);
    }
    render::Camera out;
    out.view = app.viewMatrix();
    out.proj = app.projMatrix();
    out.position = app.eye();
    return data::makeValue<render::Camera>(out);
}

data::Result<render::Camera> CameraMapper::mapCached(
    const scene::Camera& app, const scene::TranslateContext& ctx) {
    // Validation first — a cache hit must never bypass the 2D/3D pairing
    // check (T4 gate). No memo today: see the header comment (the former
    // single-slot gen-keyed entry collided across views).
    auto valid = validateProjectionPairing(app, ctx);
    if (valid.failed()) {
        return data::makeError<render::Camera>(valid.error().code,
                                               valid.error().message);
    }
    render::Camera out;
    out.view = app.viewMatrix();
    out.proj = app.projMatrix();
    out.position = app.eye();
    return data::makeValue<render::Camera>(out);
}

void CameraMapper::invalidate(uint64_t /*id*/) {}

} // namespace re::broker
