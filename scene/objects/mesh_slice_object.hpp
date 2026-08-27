#pragma once

// scene/objects/mesh_slice_object.hpp — MeshSliceObject concrete kind (T1).
//
// Scene object kind — see mesh_object.hpp header comment for the shared
// ObjectHeader delegation and open-extension rationale. T1 D.

#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "data/mesh.hpp"
#include "scene/material_desc.hpp"
#include "scene/iscene_object.hpp"

namespace re::scene {

/// MeshSliceObject — see header comment for role and open-extension guarantee.
class MeshSliceObject : public ObjectBase<MeshSliceObject> {
   public:
    static constexpr SceneKind Kind = SceneKind::MeshSlice;

    MeshSliceObject() = default;
    MeshSliceObject(const MeshSliceObject&) = default;
    MeshSliceObject(MeshSliceObject&&) noexcept = default;
    MeshSliceObject& operator=(const MeshSliceObject&) = default;
    MeshSliceObject& operator=(MeshSliceObject&&) noexcept = default;
    ~MeshSliceObject() override = default;

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

REGISTER_SCENE_OBJECT(MeshSliceObject)
