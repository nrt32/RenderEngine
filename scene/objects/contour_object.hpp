#pragma once

// scene/objects/contour_object.hpp — ContourObject concrete kind (T1).
//
// Scene object kind — see mesh_object.hpp header comment for the shared
// ObjectHeader delegation and open-extension rationale. T1 D.

#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "data/mesh.hpp"
#include "scene/layer.hpp"
#include "scene/plane_desc.hpp"
#include "scene/iscene_object.hpp"

namespace re::scene {

/// ContourObject — see header comment for role and open-extension guarantee.
class ContourObject : public ObjectBase<ContourObject> {
   public:
    static constexpr SceneKind Kind = SceneKind::Contour;

    ContourObject() = default;
    ContourObject(const ContourObject&) = default;
    ContourObject(ContourObject&&) noexcept = default;
    ContourObject& operator=(const ContourObject&) = default;
    ContourObject& operator=(ContourObject&&) noexcept = default;
    ~ContourObject() override = default;

    ObjectId id{0};
    AssetRef<data::Mesh> mesh{};
    glm::mat4 transform{1.0f};
    PlaneDesc plane{};
    glm::vec4 color{1.0f, 0.0f, 0.0f, 1.0f};
    Layer layer{Layer::Contour};
    uint64_t generation{0};

    void setPlane(PlaneDesc p) noexcept {
        plane = std::move(p);
        ++generation;
    }
    void setColor(glm::vec4 c) noexcept {
        color = std::move(c);
        ++generation;
    }
};

} // namespace re::scene

REGISTER_SCENE_OBJECT(ContourObject)
