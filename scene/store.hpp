#pragma once

// scene/store.hpp — SceneStore / ViewStore stable handles + per-field generation (SPEC §3.1, §10).
//
// Pure value library, no GL, no core. Provides stable uint64_t handles with
// generation bump on add/remove/mutate. Single bump(FieldId) entry point per SPEC §10.4.

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "scene/object.hpp"
#include "scene/view.hpp"

namespace re::scene {

/// Field identifier for per-field generation tracking (SPEC §10.4).
enum class FieldId : uint8_t {
    Rect = 0,
    Plane = 1,
    CameraView = 2,
    CameraProj = 3,
    Items = 4,
    Transform = 5,
    Material = 6,
    TransferFunction = 7,
};

/// SceneStore: owns SceneObject family with stable handles + generation.
///
/// Each object gets a stable uint64_t id (never reused with same generation —
/// generation increments on removal so stale handles are detectable). storeGen
/// is the global monotonic counter (SPEC §10.4 hybrid poll early-out).
class SceneStore {
   public:
    SceneStore() = default;

    /// Add a mesh object; returns stable id. Bumps storeGen and object's generation.
    uint64_t addMeshObject(MeshObject obj);
    uint64_t addMeshSliceObject(MeshSliceObject obj);
    uint64_t addVolumeObject(VolumeObject obj);
    uint64_t addVolumeSliceObject(VolumeSliceObject obj);
    uint64_t addPlaneObject(PlaneObject obj);

    /// Getters — nullptr if not found or generation mismatch (stale handle is nullptr).
    const MeshObject* getMeshObject(uint64_t id) const noexcept;
    const MeshSliceObject* getMeshSliceObject(uint64_t id) const noexcept;
    const VolumeObject* getVolumeObject(uint64_t id) const noexcept;
    const VolumeSliceObject* getVolumeSliceObject(uint64_t id) const noexcept;
    const PlaneObject* getPlaneObject(uint64_t id) const noexcept;

    /// Mutable getters for mutation (bump via bump()).
    MeshObject* getMeshObjectMut(uint64_t id) noexcept;
    VolumeObject* getVolumeObjectMut(uint64_t id) noexcept;

    /// Remove by id; bumps storeGen; retains generation tombstone for stale detection.
    /// Returns true if existed.
    bool removeMeshObject(uint64_t id) noexcept;
    bool removeMeshSliceObject(uint64_t id) noexcept;
    bool removeVolumeObject(uint64_t id) noexcept;
    bool removeVolumeSliceObject(uint64_t id) noexcept;
    bool removePlaneObject(uint64_t id) noexcept;

    /// Global store generation — monotonic, bumped on every add/remove/mutate.
    uint64_t storeGeneration() const noexcept { return storeGen_; }

    /// Single entry point for field mutation bump (SPEC §10.4 SRP God-object guard).
    void bump(FieldId /*field*/) noexcept { ++storeGen_; }

    /// Counts.
    size_t meshObjectCount() const noexcept { return meshObjects_.size(); }
    size_t volumeObjectCount() const noexcept { return volumeObjects_.size(); }
    size_t planeObjectCount() const noexcept { return planeObjects_.size(); }

    /// Dirty fields since gen (bounded set — for T1 returns all if storeGen changed).
    std::vector<FieldId> dirtyFieldsSince(uint64_t lastGen) const noexcept;

   private:
    uint64_t allocId() noexcept { return nextId_++; }
    uint64_t storeGen_{0};
    uint64_t nextId_{1};

    std::unordered_map<uint64_t, MeshObject> meshObjects_;
    std::unordered_map<uint64_t, MeshSliceObject> meshSliceObjects_;
    std::unordered_map<uint64_t, VolumeObject> volumeObjects_;
    std::unordered_map<uint64_t, VolumeSliceObject> volumeSliceObjects_;
    std::unordered_map<uint64_t, PlaneObject> planeObjects_;

    // Tombstone generations for removed ids (to detect stale handles).
    std::unordered_map<uint64_t, uint64_t> tombstoneGen_;
};

/// ViewStore: owns View objects with stable handles + per-field generation.
class ViewStore {
   public:
    ViewStore() = default;

    /// Add view; returns stable id. Bumps storeGen.
    uint64_t addView(View view);
    const View* getView(uint64_t id) const noexcept;
    View* getViewMut(uint64_t id) noexcept;
    bool removeView(uint64_t id) noexcept;

    uint64_t storeGeneration() const noexcept { return storeGen_; }
    void bump(FieldId /*field*/) noexcept { ++storeGen_; }
    size_t count() const noexcept { return views_.size(); }

    std::vector<FieldId> dirtyFieldsSince(uint64_t lastGen) const noexcept;

   private:
     uint64_t allocId() noexcept { return nextId_++; }
     uint64_t storeGen_{0};
     uint64_t nextId_{1};
     std::unordered_map<uint64_t, View> views_;
     std::unordered_map<uint64_t, uint64_t> tombstoneGen_;
};

} // namespace re::scene
