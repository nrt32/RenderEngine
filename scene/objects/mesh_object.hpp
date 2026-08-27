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
#include "scene/material_desc.hpp"
#include "scene/iscene_object.hpp"

namespace re::scene {

/// MeshObject — see header comment for role and open-extension guarantee.
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
    uint64_t generation{0};

    void setPresentation(MeshMaterialDesc p) noexcept {
        presentation = std::move(p);
        ++generation;
    }
};

} // namespace re::scene

REGISTER_SCENE_OBJECT(MeshObject)
