#pragma once

// broker/line_object_mapper.hpp — LineObjectMapper: ICachedMapper<scene::LineObject, render::LineScene> (V7 T7).
//
// This mapper is the per-type broker for polyline / segmented lines (scene::LineObject → render::LineScene) that implements the state-of-art GPU line pipeline locked at 2026-08-30 where each segment a→b is expanded on the GPU into a view-space quad strip via SSBO+gl_VertexID with 6 virtual vertices per segment covering a±n·wA/b±n·wB where n=normalize(perp(viewport·(b−a))) so the width stays constant in screen space, the CPU populates an SSBO of LineSegmentSSBO{a,b,color,width,s0,s1,worldUnits,dashLength,gapLength,offset,cap,join,miterLimit} with s as cumulative viewport length length(viewport·(b−a)) so the shader can evaluate inDash=step(mod(s+offset,patternLen),dashLen) with smoothstep(fwidth) AA, joins use miterLimit 4→bevel via prev/next at nodes, caps are round/square, and worldUnits width is scaled via projection delta like points. Scene/ carries only semantic stroke state (vector<LineSegment> segments, color, width, worldUnits, cap/join/miterLimit, style, dash) in local space — this mapper transforms each segment's endpoints by the object's world transform, maps scene::LineCap/LineJoin/DashPattern to the render's ReLineCap/ReLineJoin plus dash scalars, and preserves the premultiplied-alpha color for LinkedListOIT. Cached per-id generation via CachedMapperBase, one file per mapper keeps broker_per_type green, ISP via ICachedMapper, no raw GL. (V7 T7)

#include <memory>

#include "broker/cached_mapper_base.hpp"
#include "render/line_renderer.hpp"
#include "scene/object.hpp"
#include "scene/translate_context.hpp"

namespace re::render {
/// RE-minimal line alias — the renderer's LineScene collection of LineInstances that the LineRenderer draws via SSBO+gl_VertexID view-quad strip; reusing the renderer's collection keeps the broker-to-render hand-off uniform-ready and the std430 SSBO layout (96B per segment) consistent.
using ReLineObject = LineScene;
} // namespace re::render

namespace re::broker {

/// Line object mapper — cached translation scene::LineObject -> render::ReLineObject (LineScene).
///
/// Transforms each segment's endpoints by app.transform, maps cap/join/dash/style to the RE equivalents, preserves width/worldUnits/color, and caches per-id generation. The CPU does not compute s0/s1 arc-lengths that the renderer derives from viewport at draw time — the mapper only carries semantic stroke state. No AssetRegistry, no GL.
class LineObjectMapper : public CachedMapperBase<scene::LineObject, render::ReLineObject> {
   public:
    using AppType = scene::LineObject;
    using ReType = render::ReLineObject;

    LineObjectMapper() = default;

    /// Pure translation: transform segment endpoints, map stroke styling.
    data::Result<render::ReLineObject> map(const scene::LineObject& app,
                                           const scene::TranslateContext& ctx) const override;
};

} // namespace re::broker
