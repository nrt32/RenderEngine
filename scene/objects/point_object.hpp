#pragma once

// scene/objects/point_object.hpp — PointObject concrete kind for single 3D→sphere / 2D circle points (V7 T2).
//
// Points have two render paths per the V7 design locked at 2026-08-30: a single 3D point reuses MeshRenderer with GeometryKind::Sphere (lit sphere impostor, world-space radius default so it scales with the scene and worldUnits false → constant 10 px screen radius for markers that must stay legible regardless of camera distance or DPI), while 2D points and dense clouds go through PointRenderer's impostor billboard (instanced quad [−1,−1]..[1,1] expanded from center→clip→ndc→viewport using Camera right/up, radiusScreen computed as worldUnits ? radius*viewport.w/pos.w/tan(fov/2) : radiusPx, then fragment shader discards where r2=dot(mapping,mapping)>1 and shades hollow/grid via fill, writing gl_FragDepth from the ray-sphere intersection for 3D lit spheres but flat alpha halo for 2D where is2D()==true via ClipPlane presence). The fill mode {Solid, Hollow, GridDashed} and fillParam (grid density) are pure value tags, not GL state, so scene/ stays GL-free/RE-free and disposition_scene remains satisfied; the mapper turns them into a uniform-ready fill uniform without including render headers. PointObject mirrors the task's D: PointObject{vec3 position, float radius, bool worldUnits, vec4 color, PointFill fill, float fillParam} plus the shared ObjectBase header {id, transform, layer, priority, generation} and registration via REGISTER_SCENE_OBJECT. (V7 T2)

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include "scene/iscene_object.hpp"
#include "scene/layer.hpp"
#include "scene/point_fill.hpp"

namespace re::scene {

/// PointObject — single point marker (V7 T2).
///
/// Carries a world-space position (applied with the object's transform for
/// placement, so position is local to the object's model), a radius (world
/// default; worldUnits false → constant pixel radius, e.g., 10 px stays 10 px
/// at two camera distances within 1/255 per FR-render.8), a color (RGBA, alpha
/// drives LinkedListOIT capture when <1), and fill tags {Solid,Hollow,GridDashed}
/// plus fillParam for dense marker styling. Derives from ObjectBase<PointObject>
/// with Kind=Point (7), layer LAYER_0/priority 0 defaults, and generation bumping
/// via setLayer/setPriority through the mixin; registration via REGISTER_SCENE_OBJECT
/// into SceneFactory so SceneFactory::hasKind(Point)==true and the broker can
/// resolve a PointObjectMapper via Broker::get<PointObject,RePointObject>().
class PointObject : public ObjectBase<PointObject> {
   public:
    static constexpr SceneKind Kind = SceneKind::Point;

    PointObject() = default;
    PointObject(const PointObject&) = default;
    PointObject(PointObject&&) noexcept = default;
    PointObject& operator=(const PointObject&) = default;
    PointObject& operator=(PointObject&&) noexcept = default;
    ~PointObject() override = default;

    ObjectId id{0};
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    float radius{5.0f};
    bool worldUnits{true};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    PointFill fill{PointFill::Solid};
    float fillParam{0.0f};
    glm::mat4 transform{1.0f};
    Layer layer{Layer::LAYER_0};
    int32_t priority{0};
    uint64_t generation{0};

    void setPosition(glm::vec3 p) noexcept {
        position = p;
        ++generation;
    }
    void setRadius(float r) noexcept {
        radius = r;
        ++generation;
    }
    void setColor(glm::vec4 c) noexcept {
        color = c;
        ++generation;
    }
    void setFill(PointFill f) noexcept {
        fill = f;
        ++generation;
    }

    bool operator==(const PointObject& o) const noexcept {
        return id == o.id && position == o.position && radius == o.radius &&
               worldUnits == o.worldUnits && color == o.color && fill == o.fill &&
               fillParam == o.fillParam && transform == o.transform && layer == o.layer &&
               priority == o.priority && generation == o.generation;
    }
    bool operator!=(const PointObject& o) const noexcept { return !(*this == o); }
};

} // namespace re::scene

REGISTER_SCENE_OBJECT(PointObject)
