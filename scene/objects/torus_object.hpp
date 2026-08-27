#pragma once

// scene/objects/torus_object.hpp — TorusObject concrete kind (T1).
//
// Additional open kind beyond the core six — proves the hierarchy scales
// without editing the variant alias or store visitors. Like the other mesh-
// backed kinds it carries an immutable mesh asset shared-ptr co-owned (T17) and
// a Phong presentation, so the generic mesh mapper can produce a real layer.
// The registrar plus SceneFactory entry make Factory::create(Torus) succeed
// without touching existing mapper files. T1 D.

#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "data/mesh.hpp"
#include "scene/material_desc.hpp"
#include "scene/iscene_object.hpp"

namespace re::scene {

/// TorusObject — see header comment for role and open-extension guarantee.
class TorusObject : public ObjectBase<TorusObject> {
   public:
    static constexpr SceneKind Kind = SceneKind::Torus;

    TorusObject() = default;
    TorusObject(const TorusObject&) = default;
    TorusObject(TorusObject&&) noexcept = default;
    TorusObject& operator=(const TorusObject&) = default;
    TorusObject& operator=(TorusObject&&) noexcept = default;
    ~TorusObject() override = default;

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

REGISTER_SCENE_OBJECT(TorusObject)
