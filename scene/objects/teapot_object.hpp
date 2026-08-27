#pragma once

// scene/objects/teapot_object.hpp — TeapotObject concrete kind (T1 gate).
//
// Sixteenth kind not present in the old variant alias — adding TeapotObject
// proves open extension: one header plus one Broker::registerMapper<TeapotObject>
// line renders through ViewSynchronizer with zero edits to SceneStore or the
// synchronizer dispatch, and SceneFactory::create(Teapot) succeeds while
// variant< MeshObject would have required editing the alias and every visitor).
// The object is a mesh-backed kind (Utah teapot) sharing the mesh mapper's
// asset path, so its center pixel composite matches the analytic Phong headlight
// within 1/255 (gate: analytic color, not >0). T1 Phase C.

#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "data/mesh.hpp"
#include "scene/material_desc.hpp"
#include "scene/iscene_object.hpp"

namespace re::scene {

/// TeapotObject — see header comment for role and open-extension guarantee.
class TeapotObject : public ObjectBase<TeapotObject> {
   public:
    static constexpr SceneKind Kind = SceneKind::Teapot;

    TeapotObject() = default;
    TeapotObject(const TeapotObject&) = default;
    TeapotObject(TeapotObject&&) noexcept = default;
    TeapotObject& operator=(const TeapotObject&) = default;
    TeapotObject& operator=(TeapotObject&&) noexcept = default;
    ~TeapotObject() override = default;

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

REGISTER_SCENE_OBJECT(TeapotObject)
