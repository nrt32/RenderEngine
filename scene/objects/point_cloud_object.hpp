#pragma once

// scene/objects/point_cloud_object.hpp — PointCloudObject concrete kind (T1).
//
// Additional open kind beyond the core six — proves the hierarchy scales
// without editing the variant alias or store visitors. Like the other mesh-
// backed kinds it carries an immutable mesh asset shared-ptr co-owned (T17) and
// a Phong presentation, so the generic mesh mapper can produce a real layer.
// The registrar plus SceneFactory entry make Factory::create(PointCloud) succeed
// without touching existing mapper files. T1 D.

#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "data/mesh.hpp"
#include "scene/material_desc.hpp"
#include "scene/iscene_object.hpp"

namespace re::scene {

/// PointCloudObject — see header comment for role and open-extension guarantee.
class PointCloudObject : public ObjectBase<PointCloudObject> {
   public:
    static constexpr SceneKind Kind = SceneKind::PointCloud;

    PointCloudObject() = default;
    PointCloudObject(const PointCloudObject&) = default;
    PointCloudObject(PointCloudObject&&) noexcept = default;
    PointCloudObject& operator=(const PointCloudObject&) = default;
    PointCloudObject& operator=(PointCloudObject&&) noexcept = default;
    ~PointCloudObject() override = default;

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

REGISTER_SCENE_OBJECT(PointCloudObject)
