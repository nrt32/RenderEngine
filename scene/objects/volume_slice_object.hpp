#pragma once

// scene/objects/volume_slice_object.hpp — VolumeSliceObject concrete kind (T1).
//
// Scene object kind — see mesh_object.hpp header comment for the shared
// ObjectHeader delegation and open-extension rationale. T1 D.

#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "data/volume_dataset.hpp"
#include "scene/material_desc.hpp"
#include "volume/transfer_function.hpp"
#include "scene/iscene_object.hpp"

namespace re::scene {

/// VolumeSliceObject — see header comment for role and open-extension guarantee.
class VolumeSliceObject : public ObjectBase<VolumeSliceObject> {
   public:
    static constexpr SceneKind Kind = SceneKind::VolumeSlice;

    VolumeSliceObject() = default;
    VolumeSliceObject(const VolumeSliceObject&) = default;
    VolumeSliceObject(VolumeSliceObject&&) noexcept = default;
    VolumeSliceObject& operator=(const VolumeSliceObject&) = default;
    VolumeSliceObject& operator=(VolumeSliceObject&&) noexcept = default;
    ~VolumeSliceObject() override = default;

    ObjectId id{0};
    AssetRef<data::VolumeDataset> volume{};
    glm::mat4 transform{1.0f};
    VolumeMaterialDesc material{};
    volume::TransferFunction transferFunction{{{0.0f, {0, 0, 0, 0}}, {1.0f, {1, 1, 1, 1}}}};
    float stepLength{0.01f};
    uint64_t generation{0};

    void setTransferFunction(volume::TransferFunction tf) noexcept {
        transferFunction = std::move(tf);
        ++generation;
    }
};

} // namespace re::scene

REGISTER_SCENE_OBJECT(VolumeSliceObject)
