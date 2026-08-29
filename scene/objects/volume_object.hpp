#pragma once

// scene/objects/volume_object.hpp — VolumeObject concrete kind (T1).
//
// Scene object kind — see mesh_object.hpp header comment for the shared
// ObjectHeader delegation and open-extension rationale. T1 D.

#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "data/volume_dataset.hpp"
#include "scene/layer.hpp"
#include "scene/material_desc.hpp"
#include "volume/transfer_function.hpp"
#include "scene/iscene_object.hpp"

namespace re::scene {

/// VolumeObject — see header comment for role and open-extension guarantee.
class VolumeObject : public ObjectBase<VolumeObject> {
   public:
    static constexpr SceneKind Kind = SceneKind::Volume;

    VolumeObject() = default;
    VolumeObject(const VolumeObject&) = default;
    VolumeObject(VolumeObject&&) noexcept = default;
    VolumeObject& operator=(const VolumeObject&) = default;
    VolumeObject& operator=(VolumeObject&&) noexcept = default;
    ~VolumeObject() override = default;

    ObjectId id{0};
    AssetRef<data::VolumeDataset> volume{};
    glm::mat4 transform{1.0f};
    VolumeMaterialDesc material{};
    volume::TransferFunction transferFunction{{{0.0f, {0, 0, 0, 0}}, {1.0f, {1, 1, 1, 1}}}};
    float stepLength{0.01f};
    Layer layer{Layer::LAYER_0};
    int32_t priority{0};
    uint64_t generation{0};

    void setTransferFunction(volume::TransferFunction tf) noexcept {
        transferFunction = std::move(tf);
        ++generation;
    }
    void setMaterial(VolumeMaterialDesc m) noexcept {
        material = std::move(m);
        ++generation;
    }
};

} // namespace re::scene

REGISTER_SCENE_OBJECT(VolumeObject)
