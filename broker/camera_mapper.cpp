// broker/camera_mapper.cpp — CameraMapper cached translation (no GL).

#include "broker/camera_mapper.hpp"

namespace re::broker {

data::Result<render::Camera> CameraMapper::map(
    const scene::Camera& app, const scene::TranslateContext& /*ctx*/) const {
    render::Camera out;
    out.view = app.viewMatrix();
    out.proj = app.projMatrix();
    out.position = app.eye();
    return data::makeValue<render::Camera>(out);
}

data::Result<render::Camera> CameraMapper::mapCached(
    const scene::Camera& app, const scene::TranslateContext& ctx) {
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
