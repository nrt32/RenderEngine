// broker/plane_mapper.cpp — PlaneMapper pure translation (no raw gl*).
//
// The shared unit quad: one program-duration PlaneGeometry built once from
// render::PlaneGeometry::unitQuadXY() and lent to every mapped instance. A
// function-local static (not a mapper member) keeps the borrow valid for the
// whole run regardless of mapper copy/move/destroy order, so a mapped
// render::PlaneInstance can never dangle through broker lifetime choices; the
// geometry is immutable after construction (all corners/UVs/normal analytic —
// see the unitQuadXY contract in render/plane_renderer.hpp), so sharing it
// across instances and mappers is race-free in the engine's single-threaded
// draw model.

#include "broker/plane_mapper.hpp"

namespace re::broker {
namespace {

/// The program-duration shared unit quad every mapped instance points at.
const render::PlaneGeometry& sharedUnitQuad() {
    static const render::PlaneGeometry kUnit =
        render::PlaneGeometry::unitQuadXY();
    return kUnit;
}

} // namespace

data::Result<render::PlaneInstance> PlaneMapper::map(
    const scene::PlaneObject& app,
    const scene::TranslateContext& /*ctx*/) const {
    if (app.image == nullptr) {
        return data::makeError<render::PlaneInstance>(
            1, "PlaneMapper: null image pointer");
    }
    render::PlaneInstance out;
    out.geometry = &sharedUnitQuad();
    out.image = app.image;
    out.model = app.transform;
    return data::makeValue<render::PlaneInstance>(out);
}

} // namespace re::broker
