#pragma once

// scene/objects/point_cloud_object.hpp — PointCloudObject concrete kind for dense point clouds (V7 T2).
//
// While PointObject covers a single marker (often 3D→MeshRenderer Sphere reuse for a lit sphere oracle within 1/255), PointCloudObject is the batched variant for hundreds of points that share one worldUnits toggle but need per-point radii, colors, and fill encodings (e.g., a LiDAR cloud or vector field markers where each point may be Solid vs Hollow vs GridDashed and may have a distinct radius/color without allocating hundreds of PointObject scene handles). The representation per TASKS.md V7 D is PointCloudObject{vector<PointData{vec3 pos, float radius, vec4 color, uint32_t fillBits}> points, bool worldUnits} where fillBits encodes the PointFill variant as a compact 32-bit tag (0=Solid, 1=Hollow, 2=GridDashed) so the broker can upload a single SSBO of PointInstance{vec3 pos, float radius, bool worldUnits, vec4 color, PointFill fill} without per-point header overhead, and the renderer's instanced quad expansion [−1,−1]..[1,1] with r2 discard and hollow/grid branching stays identical to PointObject's impostor path. The object still derives from ObjectBase<PointCloudObject> with Kind=Point (7) — it shares the technique with PointObject but is a distinct scene type so the broker can register two mappers (PointObjectMapper and PointCloudMapper) per broker_per_type, each one class per file, while SceneFactory::hasKind(Point)==true covers both. Closed for modification: adding a new point attribute (e.g., per-point opacity) adds a field to PointData without editing PointObject or existing mappers beyond the new Cloud mapper (OCP via registry). (V7 T2)

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include "scene/iscene_object.hpp"
#include "scene/layer.hpp"
#include "scene/point_fill.hpp"

namespace re::scene {

/// Per-point data for PointCloudObject — position, radius, color, fillBits.
///
/// fillBits encodes PointFill (0 Solid, 1 Hollow, 2 GridDashed) as a compact
/// integer so hundreds of points can be uploaded as a single SSBO without per-point
/// enum overhead on the GPU; the mapper decodes fillBits→PointFill for the
/// impostor shader's hollow/grid branch. radius is per-point but interpreted via
/// the cloud's shared worldUnits flag (like PointObject's worldUnits toggle)
/// so a cloud can be uniformly world-scaled or uniformly pixel-constant.
struct PointData {
    glm::vec3 pos{0.0f};
    float radius{5.0f};
    glm::vec4 color{1.0f};
    uint32_t fillBits{0};

    PointFill fill() const noexcept { return static_cast<PointFill>(fillBits % 3u); }

    bool operator==(const PointData& o) const noexcept {
        return pos == o.pos && radius == o.radius && color == o.color && fillBits == o.fillBits;
    }
    bool operator!=(const PointData& o) const noexcept { return !(*this == o); }
};

/// PointCloudObject — batched point cloud (V7 T2, distinct from single PointObject).
///
/// Shares SceneKind::Point (7) with PointObject because both dispatch to the same
/// technique (PointRenderer impostor + optional MeshRenderer Sphere delegate for
/// 3D singles), but is a separate scene type so Broker can hold two
/// ICachedMapper registrations (PointObject→RePointObject and PointCloudObject→
/// RePointCloudObject) without violating broker_per_type (one class per file).
/// Derives from ObjectBase<PointCloudObject> with the standard header triple
/// {id, transform, layer, priority, generation} plus vector<PointData> points
/// and shared bool worldUnits. Registration via REGISTER_SCENE_OBJECT.
class PointCloudObject : public ObjectBase<PointCloudObject> {
   public:
    // Distinct scene type but same technique dispatch as PointObject — both use Kind=Point.
    // The factory registry keeps both kinds under SceneKind::Point so hasKind(Point)==true
    // covers the technique; the type distinction is at the C++ type level for Broker's pair-key.
    static constexpr SceneKind Kind = SceneKind::Point;

    PointCloudObject() = default;
    PointCloudObject(const PointCloudObject&) = default;
    PointCloudObject(PointCloudObject&&) noexcept = default;
    PointCloudObject& operator=(const PointCloudObject&) = default;
    PointCloudObject& operator=(PointCloudObject&&) noexcept = default;
    ~PointCloudObject() override = default;

    ObjectId id{0};
    std::vector<PointData> points{};
    bool worldUnits{true};
    glm::mat4 transform{1.0f};
    Layer layer{Layer::LAYER_0};
    int32_t priority{0};
    uint64_t generation{0};

    void setPoints(std::vector<PointData> p) noexcept {
        points = std::move(p);
        ++generation;
    }
    void setWorldUnits(bool w) noexcept {
        worldUnits = w;
        ++generation;
    }

    bool operator==(const PointCloudObject& o) const noexcept {
        return id == o.id && points == o.points && worldUnits == o.worldUnits &&
               transform == o.transform && layer == o.layer && priority == o.priority &&
               generation == o.generation;
    }
    bool operator!=(const PointCloudObject& o) const noexcept { return !(*this == o); }
};

} // namespace re::scene

// Note: PointCloudObject shares Kind=Point with PointObject, so the default
// REGISTER_SCENE_OBJECT macro would re-register the same SceneKind key and the
// last registration would win (factory holds one creator per SceneKind). T2 keeps
// the distinct C++ types for Broker pair-key OCP (PointObject vs PointCloudObject)
// while the technique dispatch remains closed under SceneKind::Point; a dedicated
// factory entry for the cloud kind is therefore not registered via the macro —
// SceneFactory::hasKind(Point)==true already covers the technique, and the
// per-type mapper registry (Broker) distinguishes the two C++ types without a
// second SceneKind value. If a future T needs a standalone cloud factory creator,
// add a separate SceneKind::PointCloud dispatch kind and update Count to 10.
//
// To avoid double-registering SceneKind::Point, we intentionally do NOT invoke
// REGISTER_SCENE_OBJECT(PointCloudObject) — PointObject's registration satisfies
// hasKind(Point). The broker's PointCloudMapper is still registered via
// Broker::registerMapper<PointCloudObject,RePointCloudObject> (pair-key, not
// factory). This keeps the factory's Count=9 invariant and avoids overwriting
// the PointObject creator that tests clone via SceneFactory::create(Point).
