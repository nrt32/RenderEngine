#pragma once

// scene/objects/line_object.hpp — LineObject concrete kind for polyline / line segments (V7 T2).
//
// Lines use the state-of-art SSBO+gl_VertexID 6-vertex view-quad strip per segment (no geometry shader, analytic fwidth AA, Rougier mod(s, patternLen) dash with miterLimit 4→bevel fallback and round/square caps, worldUnits toggle) per the locked V7 design. The scene side therefore carries only semantic stroke state: a vector of LineSegments {vec3 a, b} (world-space endpoints, transformed by the object's model matrix), a base color (RGBA, alpha <1 routes to LinkedListOIT premultiplied capture), a width (world default, worldUnits false → constant pixel width, e.g., 2 px stays 2 px and ≥90% of the geometric ±width/2 band stays within 1/255 of the base color per FR-render.9), caps {Round, Square}, joins {Miter→Bevel via miterLimit 4}, and a dash pattern {dashLength, gapLength, offset} (solid when gapLen≈0). The SSB0 LineSegmentSSBO{a,b,color,width,s0,s1,worldUnits} is built CPU-side with s as cumulative viewport length so the shader can evaluate inDash = step(mod(s+offset, patternLen), dashLen) with smoothstep(fwidth) AA at the dash transition, discarding gaps and premultiplying alpha. Scene/ stays GL/RE-free and only depends on glm + line_style + iscene_object, preserving disposition_scene and gpu_api_ownership; the broker's LineObjectMapper will hash width/worldUnits/cap/join/miterLimit/dash for its cache key. (V7 T2)

#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include "scene/iscene_object.hpp"
#include "scene/layer.hpp"
#include "scene/line_style.hpp"

namespace re::scene {

/// Single line segment — world-space endpoints a→b (before object transform).
///
/// The object's transform (mat4) is applied to both endpoints by the mapper
/// when building the SSBO, so a polyline's segments stay in object-local space
/// and can be instanced via one object transform without per-segment matrices.
struct LineSegment {
    glm::vec3 a{0.0f};
    glm::vec3 b{0.0f, 0.0f, 1.0f};

    bool operator==(const LineSegment& o) const noexcept { return a == o.a && b == o.b; }
    bool operator!=(const LineSegment& o) const noexcept { return !(*this == o); }
};

/// LineObject — polyline / segmented line with stroke styling (V7 T2).
///
/// Derives from ObjectBase<LineObject> with Kind=Line (8), layer LAYER_0/priority 0
/// defaults, and registration via REGISTER_SCENE_OBJECT into SceneFactory so
/// SceneFactory::hasKind(Line)==true and Broker can resolve a LineObjectMapper.
/// Stroke state is fully value-semantic: color, width, worldUnits, cap/join/
/// miterLimit, dash pattern. The vector<LineSegment> segments forms the polyline
/// (contiguous segments share endpoints; join handling uses prev/next at nodes
/// with miterLimit 4→bevel per spec). Generation bumps on mutation via setColor
/// etc. and via the ObjectBase setLayer/setPriority mixin. (V7 T2)
class LineObject : public ObjectBase<LineObject> {
   public:
    static constexpr SceneKind Kind = SceneKind::Line;

    LineObject() = default;
    LineObject(const LineObject&) = default;
    LineObject(LineObject&&) noexcept = default;
    LineObject& operator=(const LineObject&) = default;
    LineObject& operator=(LineObject&&) noexcept = default;
    ~LineObject() override = default;

    ObjectId id{0};
    std::vector<LineSegment> segments{};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float width{2.0f};
    bool worldUnits{false};
    LineCap cap{LineCap::Square};
    LineJoin join{LineJoin::Miter};
    float miterLimit{4.0f};
    LineStyle style{LineStyle::Solid};
    DashPattern dash{};
    glm::mat4 transform{1.0f};
    Layer layer{Layer::LAYER_0};
    int32_t priority{0};
    uint64_t generation{0};

    void setColor(glm::vec4 c) noexcept {
        color = c;
        ++generation;
    }
    void setWidth(float w) noexcept {
        width = w;
        ++generation;
    }
    void setCap(LineCap c) noexcept {
        cap = c;
        ++generation;
    }
    void setJoin(LineJoin j) noexcept {
        join = j;
        ++generation;
    }
    void setDash(DashPattern d) noexcept {
        dash = d;
        ++generation;
    }
    void setStyle(LineStyle s) noexcept {
        style = s;
        ++generation;
    }
    void setSegments(std::vector<LineSegment> s) noexcept {
        segments = std::move(s);
        ++generation;
    }

    bool operator==(const LineObject& o) const noexcept {
        return id == o.id && segments == o.segments && color == o.color && width == o.width &&
               worldUnits == o.worldUnits && cap == o.cap && join == o.join &&
               miterLimit == o.miterLimit && style == o.style && dash == o.dash && transform == o.transform &&
               layer == o.layer && priority == o.priority && generation == o.generation;
    }
    bool operator!=(const LineObject& o) const noexcept { return !(*this == o); }
};

} // namespace re::scene

REGISTER_SCENE_OBJECT(LineObject)
