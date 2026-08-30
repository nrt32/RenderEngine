#pragma once

// broker/point_object_mapper.hpp — PointObjectMapper: ICachedMapper<scene::PointObject, render::PointInstance> (V7 T7).
//
// This mapper is the per-type broker for single-point markers (scene::PointObject → render::PointInstance) that were introduced as part of the V7 GPU Points/Lines deliverable where Point 3D single reuses MeshRenderer with GeometryKind::Sphere for a lit sphere oracle within 1/255 while PointCloud and 2D circles go through PointRenderer's impostor billboard that expands an instanced quad [−1,−1]..[1,1] via center→clip→ndc→viewport with right/up from Camera and radiusScreen computed as worldUnits ? radius*viewport.w/pos.w/tan(fov/2) approximated via projection delta of a right-offset world point versus radiusPx for worldUnits false, then discards where r2=dot(mapping,mapping)>1 and shades hollow/grid via fill, writing gl_FragDepth from ray-sphere intersection for 3D but flat alpha halo for 2D where is2D()==true via ClipPlane presence. The mapper translates the scene value (position, radius, worldUnits, color, PointFill) into the RE-minimal render::PointInstance that carries only RE-direct fields (pos transformed by the object's model matrix, radius, worldUnits, color, fill, fillParam) and aliases through the broker registry via Broker::pairKey so the ViewSynchronizer can look up by SceneKind::Point without branching and the cache stays per-id generation via CachedMapperBase. No raw GL is used here (gpu_api_ownership — render helpers own GL via core/), and the header stays one type per file so the per-type rule at T15b remains green; ISP is satisfied via the cached interface. (V7 T7)

#include <memory>

#include "broker/cached_mapper_base.hpp"
#include "render/re_scene/point_object.hpp"
#include "scene/object.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Point object mapper — cached translation scene::PointObject -> render::RePointObject (PointInstance).
///
/// Maps position through the object's world transform (transform * vec4(position,1)), carries radius/worldUnits/color/fill verbatim, and caches per-id generation so a pure setTransform orbit reuses the cached instance without touching the GPU asset store. The mapper co-owns no registry (points are procedural, not assets) but still uses CachedMapperBase for per-field generation cache. No raw GL.
class PointObjectMapper : public CachedMapperBase<scene::PointObject, render::RePointObject> {
   public:
    using AppType = scene::PointObject;
    using ReType = render::RePointObject;

    PointObjectMapper() = default;

    /// Pure translation: position transformed by app.transform, radius/worldUnits/color/fill carried through.
    data::Result<render::RePointObject> map(const scene::PointObject& app,
                                            const scene::TranslateContext& ctx) const override;
};

} // namespace re::broker
