#pragma once

// scene/object.hpp — SceneObject family for scene value library (SPEC §3.1, V3.1).
//
// Each object = { AssetRef, transform, presentation }, pure value semantics,
// copyable, no GL Handle, no core include. Links only to data/volume/glm.
//
// Ownership (T13): the asset reference is a shared_ptr<const data::*> —
// scene objects CO-OWN their immutable assets instead of borrowing raw CPU
// pointers, so a stored scene value can never dangle when the loader-side
// owner goes away. Copying an object copies the reference (cheap, shares the
// asset); no deep copy of voxels/positions is ever made (RE-agnostic data).

#include <cstdint>
#include <memory>
#include <variant>

#include <glm/mat4x4.hpp>

#include "data/mesh.hpp"
#include "data/volume_dataset.hpp"
#include "data/image.hpp"
#include "scene/material_desc.hpp"
#include "scene/plane_desc.hpp"
#include "volume/transfer_function.hpp"

namespace re::scene {

/// Stable handle type for scene objects (uint64_t).
using ObjectId = uint64_t;

/// Shared asset reference for scene objects (T13 ownership discipline): the
/// pointed-to data::* asset is IMMUTABLE after load and co-owned by every
/// scene object / store that references it, so a scene value can be copied
/// freely without ever dangling when the original loader-side owner goes away.
template <typename T>
using AssetRef = std::shared_ptr<const T>;

/// Mesh object: asset ref (data::Mesh) + transform + presentation.
struct MeshObject {
    ObjectId id{0};
    /// Shared reference to the immutable mesh asset. The object co-owns the
    /// asset: the bytes stay alive as long as any scene object or store holds
    /// a reference (no raw borrow, no lifetime coupling to the loader).
    AssetRef<data::Mesh> mesh{};
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

/// Mesh slice object: same asset/transform/presentation triple as MeshObject,
/// but rendered clipped by a cut plane. It deliberately does NOT carry the
/// plane itself — the plane belongs to the View (all slice objects in one view
/// are cut by the same plane), so it lives with the view state, not per object.
struct MeshSliceObject {
    ObjectId id{0};
    /// Shared reference to the immutable mesh asset (co-owned; see MeshObject).
    AssetRef<data::Mesh> mesh{};
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
    /// Shared reference to the immutable volume dataset (co-owned; see MeshObject).
    AssetRef<data::VolumeDataset> volume{};
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
    /// Shared reference to the immutable volume dataset (co-owned; see MeshObject).
    AssetRef<data::VolumeDataset> volume{};
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
    /// Shared reference to the immutable image asset (co-owned; see MeshObject).
    AssetRef<data::Image> image{};
    glm::mat4 transform{1.0f};
    MeshMaterialDesc presentation{};
    uint64_t generation{0};

    void setTransform(glm::mat4 m) noexcept {
        transform = m;
        ++generation;
    }
};

/// Contour object: mesh asset + clip plane + stroke color — the plane∩mesh
/// outline overlay (V3.8b T11, FR-app.3). Pure value, GL-free, RE-free: the
/// plane is the abstract PlaneDesc (Space::World vs VoxelIndex; conversion to
/// the world render::ClipPlane is broker's job), and the GPU side receives
/// only an AssetHandle (RE-minimal, SPEC §12.4) via broker::ContourMapper.
struct ContourObject {
    ObjectId id{0};
    /// Shared reference to the immutable mesh asset (co-owned; see MeshObject).
    AssetRef<data::Mesh> mesh{};
    glm::mat4 transform{1.0f};
    /// The plane whose ∩mesh outline this object shows (lives on the object,
    /// unlike MeshSliceObject whose plane comes from the View — a contour is
    /// meaningful only together with its own plane).
    PlaneDesc plane{};
    /// Straight RGBA stroke color (default pure red = the FR-app.3 MPR
    /// contour color, exact bytes 255,0,0,255).
    glm::vec4 color{1.0f, 0.0f, 0.0f, 1.0f};
    uint64_t generation{0};

    void setTransform(glm::mat4 m) noexcept {
        transform = m;
        ++generation;
    }
    void setPlane(PlaneDesc p) noexcept {
        plane = std::move(p);
        ++generation;
    }
    void setColor(glm::vec4 c) noexcept {
        color = c;
        ++generation;
    }
};

/// Variant over all scene object types (for store dispatch without enum switch).
using SceneObject = std::variant<MeshObject, MeshSliceObject, VolumeObject,
                                 VolumeSliceObject, PlaneObject, ContourObject>;

} // namespace re::scene
