// broker/plane_object_mapper.cpp — PlaneObjectMapper pure translation (no raw gl*).
//
// The shared unit quad: one program-duration PlaneGeometry built once from
// render::PlaneGeometry::unitQuadXY() and SHARED (T13 shared_ptr, not lent by
// raw pointer) into every mapped instance. A function-local static holds the
// owning reference for the whole run regardless of mapper copy/move/destroy
// order; each mapped render::PlaneInstance co-owns a reference, so an
// instance can never outlive its geometry. The geometry is immutable after
// construction (all corners/UVs/normal analytic — see the unitQuadXY contract
// in render/plane_renderer.hpp), so sharing it across instances and mappers
// is race-free in the engine's single-threaded draw model.

#include "broker/plane_object_mapper.hpp"

#include <memory>

namespace re::broker {
namespace {

/// The program-duration shared unit quad every mapped instance references.
const std::shared_ptr<const render::PlaneGeometry>& sharedUnitQuad() {
    static const auto kUnit =
        std::make_shared<const render::PlaneGeometry>(
            render::PlaneGeometry::unitQuadXY());
    return kUnit;
}

} // namespace

data::Result<render::PlaneInstance> PlaneObjectMapper::map(
    const scene::PlaneObject& app,
    const scene::TranslateContext& /*ctx*/) const {
    if (!app.image) {
        return data::makeError<render::PlaneInstance>(
            1, "PlaneObjectMapper: null image asset reference");
    }
    render::PlaneInstance out;
    out.geometry = sharedUnitQuad(); // shared ownership — instance co-owns
    out.image = app.image;           // shared ownership — instance co-owns
    out.model = app.transform;
    return data::makeValue<render::PlaneInstance>(out);
}

} // namespace re::broker
