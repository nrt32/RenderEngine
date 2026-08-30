#pragma once

// broker/point_cloud_mapper.hpp — PointCloudMapper: ICachedMapper<scene::PointCloudObject, render::PointScene> (V7 T7).
//
// This mapper is the per-type broker for batched point clouds (scene::PointCloudObject → render::PointScene) that were introduced alongside the single PointObject to efficiently render hundreds of markers sharing one worldUnits toggle but needing per-point radii, colors, and fill encodings (PointData{pos, radius, color, fillBits} where fillBits encodes PointFill 0=Solid 1=Hollow 2=GridDashed as a compact 32-bit tag so the broker can upload a single SSBO of PointInstance without per-point header overhead and the renderer's instanced quad expansion with r2 discard and hollow/grid branching stays identical to the single-point impostor). The translation iterates the cloud's vector<PointData>, transforms each pos by the object's world transform, decodes fillBits→PointFill, and preserves the shared worldUnits flag so a cloud can be uniformly world-scaled or pixel-constant; the RE side stays handle-free (no AssetRegistry) and the cache is per-id generation via CachedMapperBase. One file per mapper keeps broker_per_type green, ISP via ICachedMapper, no raw GL (gpu_api_ownership). (V7 T7)

#include <memory>

#include "broker/cached_mapper_base.hpp"
#include "render/point_renderer.hpp"
#include "scene/object.hpp"
#include "scene/translate_context.hpp"

namespace re::render {
/// RE-minimal batched point cloud alias — the renderer's PointScene collection of PointInstances that the PointRenderer draws as instanced impostor quads; reusing the renderer's collection keeps the broker-to-render hand-off uniform-ready without duplicating the per-point layout.
using RePointCloudObject = PointScene;
} // namespace re::render

namespace re::broker {

/// Point cloud mapper — cached translation scene::PointCloudObject -> render::RePointCloudObject (PointScene).
///
/// Batched translation: each PointData pos is transformed by app.transform, shared worldUnits preserved, fillBits decoded to PointFill, color/radius carried. Cached per-id generation via CachedMapperBase so a dense cloud's 100s of points are not re-decoded when generation unchanged (spy 2→1). No AssetRegistry, no GL.
class PointCloudMapper : public CachedMapperBase<scene::PointCloudObject, render::RePointCloudObject> {
   public:
    using AppType = scene::PointCloudObject;
    using ReType = render::RePointCloudObject;

    PointCloudMapper() = default;

    /// Pure translation: transform each point pos, decode fill, preserve worldUnits.
    data::Result<render::RePointCloudObject> map(const scene::PointCloudObject& app,
                                                 const scene::TranslateContext& ctx) const override;
};

} // namespace re::broker
