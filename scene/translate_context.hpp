#pragma once

// scene/translate_context.hpp — TranslateContext skeleton (SPEC §11.4, V3.2a T2).
//
// Role-segregated context for mapper translation: instead of one flat fat
// struct {viewPlane, view, volumeModel, dims} that every mapper must accept,
// or a live back-reference to the whole render-side view (which would couple
// scene/ to render/), the context is split into ViewContext (needed by every
// mapper) and an optional VolumeContext (only where Space::VoxelIndex→world
// conversion is needed: PlaneMapper, VolumeSliceObjectMapper). Mappers take
// const TranslateContext& but touch only their role; CameraMapper touches only
// view, PlaneMapper touches volume for voxel→world math but not viewPlane when
// hasPlane()==false. Keeping the absent plane VALID for 3D keeps mappers
// substitutable across 2D and 3D views (no strengthened preconditions).
//
// Pure value type, header-only, GL-free, RE-free (scene/ owns the type;
// broker/ re-exports it via alias so consumers include broker only).
// No behavior change yet — unblocks T3/T5/T6.

#include <optional>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "data/aabb.hpp"
#include "scene/plane_desc.hpp"

namespace re::scene {

// VG8: single canonical Aabb — alias to data::Aabb (one definition, one default).
using Aabb = data::Aabb;

/// View-scoped context — needed by every mapper (ISP role interface).
struct ViewContext {
    /// Owning layout/page scope — carried from StableKey{version,layoutId,viewId}
    /// so per-view memos keyed by {layoutId,viewId} never alias two layouts that
    /// hold different views under the same viewId (T14b alias fix — previously
    /// CameraMapper hard-coded layoutId=0 and keyed cache_[id] only, so layout
    /// A viewId=42 and layout B viewId=42 shared one ReCamera slot).
    uint64_t layoutId{0};
    /// Identity of the app view this translation serves (its stable View
    /// handle). Cached mappers whose input type carries no intrinsic id
    /// (scene::Camera is a plain value copied freely between views) key
    /// their per-view memo entries on it, so two cameras synced in the same
    /// pass can never evict or serve each other's entries. 0 = "no owning
    /// view" (direct mapper calls outside a sync pass) — still safe because
    /// cache keys also fold in the camera's own per-field generations and
    /// stable parameter bytes.
    uint64_t viewId{0};
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
/// Design decision (2026-08-23 architecture review): mappers receive this
/// role-segregated snapshot rather than a flat fat struct {viewPlane, view,
/// volumeModel, dims} or a live back-reference to the whole ReView — a mapper
/// then depends only on the data its translation actually reads, and the null
/// plane stays a first-class "3D view" state instead of an edge case.
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
