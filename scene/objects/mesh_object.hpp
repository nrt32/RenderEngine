#pragma once

// scene/objects/mesh_object.hpp — MeshObject concrete kind (T1 polymorphic hierarchy).
//
// Concrete scene object for triangle meshes — one of the fifteen concrete
// objects/*.hpp kinds that derive from ObjectBase<Derived> and register via the
// REGISTER_SCENE_OBJECT static registrar into SceneFactory and the Broker. The
// class shares the duplicated ObjectHeader{ObjectId, transform, generation,
// setTransform} via ObjectBase's CRTP delegation to the Derived's public
// fields `ObjectId id; glm::mat4 transform; uint64_t generation;`, so generation
// bumping and transform storage stay consistent and future slab allocation can
// move the header without touching each concrete header. T1 Phase A.

#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "data/mesh.hpp"
#include "scene/geometry_kind.hpp"
#include "scene/layer.hpp"
#include "scene/material_desc.hpp"
#include "scene/iscene_object.hpp"

namespace re::scene {

/// MeshObject — collapsed mesh-backed object (T5).
///
/// The 11 byte-identical headers (CubeObject, SphereObject, Cylinder, Torus,
/// Cone, Arrow, Grid, Axes, Capsule, PointCloud, Teapot sharing
/// `AssetRef<Mesh> mesh; mat4 transform; MeshMaterialDesc presentation;` at
/// scene/objects/*.hpp:36-40) are collapsed into one MeshObject carrying
/// GeometryKind {Mesh, Cube, Sphere, Cylinder, Torus, Cone, Arrow, Grid, Axes,
/// Capsule, PointCloud, Teapot}. SceneKind stays for technique dispatch only
/// (6 values: Mesh, MeshSlice, Volume, VolumeSlice, Plane, Contour); adding a
/// Sphere no longer needs a new header — `MeshObject{ .geometryKind =
/// GeometryKind::Sphere }` via the single MeshObjectMapper renders within 1/255
/// of the old per-kind path. SceneFactory + REGISTER_SCENE_OBJECT remain for
/// truly new techniques (e.g., StreamlineObject), not for data-driven mesh
/// variations. T5.
class MeshObject : public ObjectBase<MeshObject> {
   public:
    static constexpr SceneKind Kind = SceneKind::Mesh;

    MeshObject() = default;
    MeshObject(const MeshObject&) = default;
    MeshObject(MeshObject&&) noexcept = default;
    MeshObject& operator=(const MeshObject&) = default;
    MeshObject& operator=(MeshObject&&) noexcept = default;
    ~MeshObject() override = default;

    ObjectId id{0};
    AssetRef<data::Mesh> mesh{};
    glm::mat4 transform{1.0f};
    MeshMaterialDesc presentation{};
    GeometryKind geometryKind{GeometryKind::Mesh};
    Layer layer{Layer::LAYER_0};
    int32_t priority{0};
    uint64_t generation{0};

    void setPresentation(MeshMaterialDesc p) noexcept {
        presentation = std::move(p);
        ++generation;
    }
    void setGeometryKind(GeometryKind k) noexcept {
        geometryKind = k;
        ++generation;
    }
};

} // namespace re::scene

REGISTER_SCENE_OBJECT(MeshObject)
