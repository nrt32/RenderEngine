#pragma once

// broker/plane_mapper.hpp — PlaneMapper for textured planes (V3.4b T12,
// FR-render.5): pure translation of scene::PlaneObject to
// render::PlaneInstance via IMapper<scene::PlaneObject, render::PlaneInstance>.
//
// One file per mapper (guardrail broker_per_type). Pure translation (ISP: no
// cache — the only GPU work on this path, the data::Image → core::Texture2D
// upload and its dedup, lives inside render::PlaneRenderer::textureFor keyed
// by image pointer, so a generation cache here would add a second bookkeeping
// layer without a second reason to change):
//
//   scene::PlaneObject{const data::Image* (asset ref), transform,
//                      presentation}
//     → render::PlaneInstance{const PlaneGeometry*, const data::Image*, model}
//
// This is the ONLY route app/ has onto the textured-plane path: the app sends
// the scene value object {asset ref, transform, presentation} and receives the
// RE-side instance. app/ never names render::PlaneGeometry — the quad geometry
// is owned by THIS mapper (one shared unit quad for every mapped instance;
// render::PlaneRenderer maps it onto each instance's model matrix at draw
// time), so no CPU quad vertex generation can bypass PlaneRenderer (T12 D:
// PlaneGeometry::unitQuadXY stays a render/-internal detail reached through
// broker). Ownership (T13): the mapped instance SHARES (shared_ptr<const T>)
// both referenced objects — no borrows, nothing to outlive:
//   - geometry: the mapper's program-duration static unit quad (see
//     plane_mapper.cpp);
//   - image: the scene object's shared image asset reference.
//
// `presentation` (scene::MeshMaterialDesc) deliberately has no RE counterpart:
// the textured-plane path is an UNLIT texture display by design (FR-render.5's
// quad reproduces source texels exactly; a Phong-lit plane would not), so the
// renderer exposes no material slot for planes. The desc stays on the scene
// object for future lit-plane work; carrying it into render/ today would be a
// dead field (RE-minimal, SPEC §12.4).
//
// Typed errors (SPEC §5, no exceptions): code 1 null image pointer.
// No raw gl* (guardrail gpu_api_ownership — render/ owns GL via core/).

#include "broker/i_mapper.hpp"
#include "render/plane_renderer.hpp" // render::PlaneInstance / PlaneGeometry
#include "scene/object.hpp"          // scene::PlaneObject
#include "scene/translate_context.hpp"

namespace re::broker {

/// Plane mapper — pure translation scene::PlaneObject -> render::PlaneInstance.
class PlaneMapper : public IMapper<scene::PlaneObject,
                                   render::PlaneInstance> {
   public:
    using AppType = scene::PlaneObject;
    using ReType = render::PlaneInstance;

    /// Pure translation: carries the image asset ref + transform across and
    /// binds the mapper-owned shared unit quad as the instance geometry.
    /// Typed errors: code 1 null image pointer.
    data::Result<render::PlaneInstance> map(
        const scene::PlaneObject& app,
        const scene::TranslateContext& ctx) const override;
};

} // namespace re::broker
