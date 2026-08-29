#pragma once

// scene/objects/plane_object.hpp — PlaneObject concrete kind (T1).
//
// Scene object kind — see mesh_object.hpp header comment for the shared
// ObjectHeader delegation and open-extension rationale. T1 D.

#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "data/image.hpp"
#include "scene/layer.hpp"
#include "scene/material_desc.hpp"
#include "scene/iscene_object.hpp"

namespace re::scene {

/// PlaneObject — see header comment for role and open-extension guarantee.
class PlaneObject : public ObjectBase<PlaneObject> {
   public:
    static constexpr SceneKind Kind = SceneKind::Plane;

    PlaneObject() = default;
    PlaneObject(const PlaneObject&) = default;
    PlaneObject(PlaneObject&&) noexcept = default;
    PlaneObject& operator=(const PlaneObject&) = default;
    PlaneObject& operator=(PlaneObject&&) noexcept = default;
    ~PlaneObject() override = default;

    ObjectId id{0};
    AssetRef<data::Image> image{};
    glm::mat4 transform{1.0f};
    MeshMaterialDesc presentation{};
    Layer layer{Layer::LAYER_0};
    int32_t priority{0};
    uint64_t generation{0};
};

} // namespace re::scene

REGISTER_SCENE_OBJECT(PlaneObject)
