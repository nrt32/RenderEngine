#pragma once

// scene/object.hpp — SceneObject family for scene value library (SPEC §3.1, V3.1).
//
// Each object = { AssetRef, transform, presentation }, pure value semantics,
// copyable, no GL Handle, no core include. Links only to data/volume/glm.

#include <cstdint>
#include <variant>

#include <glm/mat4x4.hpp>

#include "data/mesh.hpp"
#include "data/volume_dataset.hpp"
#include "data/image.hpp"
#include "scene/material_desc.hpp"
#include "volume/transfer_function.hpp"

namespace re::scene {

/// Stable handle type for scene objects (uint64_t).
using ObjectId = uint64_t;

/// Mesh object: asset ref (data::Mesh) + transform + presentation.
struct MeshObject {
    ObjectId id{0};
    const data::Mesh* mesh{nullptr};
    glm::mat4 transform{1.0f};
    MeshMaterialDesc presentation{};
    uint64_t generation{0};

    void setTransform(glm::mat4 m) noexcept {
        transform = m;
        ++generation;
    }
    void setPresentation(MeshMaterialDesc p) noexcept {
        presentation = p;
        ++generation;
    }
};

/// Mesh slice object: same asset/transform/presentation but rendered as clipped slice.
/// Plane lives on View, not here (SPEC §11.4).
struct MeshSliceObject {
    ObjectId id{0};
    const data::Mesh* mesh{nullptr};
    glm::mat4 transform{1.0f};
    MeshMaterialDesc presentation{};
    uint64_t generation{0};

    void setTransform(glm::mat4 m) noexcept {
        transform = m;
        ++generation;
    }
    void setPresentation(MeshMaterialDesc p) noexcept {
        presentation = p;
        ++generation;
    }
};

/// Volume object: volume dataset + transform + volume presentation + TF.
struct VolumeObject {
    ObjectId id{0};
    const data::VolumeDataset* volume{nullptr};
    glm::mat4 transform{1.0f};
    VolumeMaterialDesc material{};
    volume::TransferFunction transferFunction{{{0.0f, {0, 0, 0, 0}}, {1.0f, {1, 1, 1, 1}}}};
    float stepLength{0.01f};
    uint64_t generation{0};

    void setTransform(glm::mat4 m) noexcept {
        transform = m;
        ++generation;
    }
    void setTransferFunction(volume::TransferFunction tf) noexcept {
        transferFunction = std::move(tf);
        ++generation;
    }
};

/// Volume slice object: volume + transform + presentation (TF alongside).
struct VolumeSliceObject {
    ObjectId id{0};
    const data::VolumeDataset* volume{nullptr};
    glm::mat4 transform{1.0f};
    VolumeMaterialDesc material{};
    volume::TransferFunction transferFunction{{{0.0f, {0, 0, 0, 0}}, {1.0f, {1, 1, 1, 1}}}};
    float stepLength{0.01f};
    uint64_t generation{0};

    void setTransform(glm::mat4 m) noexcept {
        transform = m;
        ++generation;
    }
    void setTransferFunction(volume::TransferFunction tf) noexcept {
        transferFunction = std::move(tf);
        ++generation;
    }
};

/// Plane object: image asset + transform + material.
struct PlaneObject {
    ObjectId id{0};
    const data::Image* image{nullptr};
    glm::mat4 transform{1.0f};
    MeshMaterialDesc presentation{};
    uint64_t generation{0};

    void setTransform(glm::mat4 m) noexcept {
        transform = m;
        ++generation;
    }
};

/// Variant over all scene object types (for store dispatch without enum switch).
using SceneObject = std::variant<MeshObject, MeshSliceObject, VolumeObject, VolumeSliceObject, PlaneObject>;

} // namespace re::scene
