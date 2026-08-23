#pragma once

// scene/translate_context.hpp — TranslateContext skeleton (SPEC §11.4, V3.2a T2).
//
// ISP-segregated context for mapper translation (Q40:B — not flat viewPlane+view+volumeModel
// fat, not God ReView*). Segregated into ViewContext (needed by every mapper) and
// optional VolumeContext (only where Space::VoxelIndex→world conversion is needed:
// PlaneMapper, VolumeSliceObjectMapper). Mappers take const TranslateContext& but touch
// only their role; CameraMapper touches only view, PlaneMapper touches volume for
// voxel→world math but not viewPlane when hasPlane()==false (LSP — null viewPlane valid
// for 3D). Uniform map(AppT,Ctx) keeps substitutability (LSP).
//
// Pure value type, header-only, GL-free, RE-free (scene/ owns the type; broker/ will
// re-export via alias in T3). No behavior change yet — unblocks T3/T5/T6.

#include <optional>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "scene/plane_desc.hpp"

namespace re::scene {

/// Axis-aligned bounds for VolumeContext meshBounds (world-space AABB).
struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    bool operator==(const Aabb& o) const noexcept {
        return min == o.min && max == o.max;
    }
    bool operator!=(const Aabb& o) const noexcept { return !(*this == o); }
};

/// View-scoped context — needed by every mapper (ISP role interface).
struct ViewContext {
    /// View plane carried BY VALUE (T13: no raw borrow of the app View's
    /// plane — the context is a self-contained snapshot). `nullopt` means a
    /// 3D view — LSP valid.
    std::optional<PlaneDesc> viewPlane{std::nullopt};
    /// View matrix from Camera (lookAt).
    glm::mat4 viewMatrix{1.0f};
    /// Projection matrix from Camera.
    glm::mat4 projMatrix{1.0f};

    /// LSP predicate — keeps preconditions weak (absent plane is valid for 3D
    /// mappers).
    bool hasPlane() const noexcept { return viewPlane.has_value(); }
};

/// Volume-scoped context — only where voxel→world conversion is needed (ISP).
struct VolumeContext {
    /// Model matrix of the active volume (world transform).
    glm::mat4 volumeModel{1.0f};
    /// Volume dimensions in voxels (e.g. {256,256,128}).
    glm::ivec3 dims{0, 0, 0};
    /// Voxel spacing (uniform world units per voxel; anisotropic spacing via volumeModel scale if needed).
    float voxelSpacing{1.0f};
    /// Mesh bounds (world-space AABB) for the volume's proxy mesh bounds.
    Aabb meshBounds{};

    bool operator==(const VolumeContext& o) const noexcept {
        return volumeModel == o.volumeModel && dims == o.dims &&
               voxelSpacing == o.voxelSpacing && meshBounds == o.meshBounds;
    }
    bool operator!=(const VolumeContext& o) const noexcept { return !(*this == o); }
};

/// Segregated translate context — value type, header-only.
///
/// Composition: ViewContext (always) + optional<VolumeContext> (only for voxel→world).
/// ISP fix over flat {viewPlane,view,volumeModel,dims} fat struct; not God ReView*.
/// Follows SPEC §11.4.1 ISP/LSP binding (Q40:B 2026-08-23).
struct TranslateContext {
    /// View role — present for every mapper.
    ViewContext view{};
    /// Volume role — present only where Space::VoxelIndex→world needed.
    std::optional<VolumeContext> volume{std::nullopt};

    /// LSP predicate — null viewPlane valid for 3D (mappers ignore plane).
    bool hasPlane() const noexcept { return view.hasPlane(); }
    /// Whether volume context is available (voxel→world conversion possible).
    bool hasVolume() const noexcept { return volume.has_value(); }
};

} // namespace re::scene
