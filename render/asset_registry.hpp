#pragma once

// render/asset_registry.hpp — generational GPU-asset registry (SPEC §9 V2.5,
// V2 T3, T7 V3.6 content-hash + dual-key shim).
//
// The asset system of the multi-view workstream: a single registry owns exactly
// ONE GPU object per individual CPU object registered, GLOBALLY across every
// mesh-family renderer. Scenes carry copyable AssetHandles — `{index,
// generation}` — instead of raw `const data::*` pointers, and handles are the
// currency views exchange: a View's Scene holds handles, and the renderers
// resolve them through the shared registry. Registering the same `data::Mesh`
// twice (e.g. once through the MeshRenderer path and once through the
// SliceRenderer path) yields one GPU object — the registry dedups by
// content hash of stable bytes (T7, not bare pointer) — which fixes the
// pre-V2 per-renderer double-upload and the T7 hot-reload identity bug
// (SPEC §7 T7, Q3/Q28). Dual-key shim: `byObject_` (pointer) + `byHash_`
// (content hash) in V3.6; shim removed V4.
//
// Generational safety (dangling-handle detection): every slot carries a
// generation that starts at 1 and is bumped each time the slot is freed (and
// again when a freed slot is reused by a later registration). A handle is valid
// only while its {index,generation} exactly matches the slot's; a stale handle
// (out-of-range index, generation mismatch — a freed, reused, or fabricated
// handle) resolves to a typed error (SPEC §5, no exceptions), never a crash.
// The default-constructed handle {0,0} is reserved as the null handle: real
// handles always have generation >= 1.
//
// render/ is GL-call-free: the registry uploads geometry through
// MeshGeometry::create (core/ RAII buffers), never raw GL calls (guardrail
// gpu_api_ownership). The registry is single-threaded (SPEC §5) and not
// thread-safe.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "data/mesh.hpp"
#include "data/result.hpp"
#include "render/mesh_geometry.hpp"

namespace re::render {

/// Copyable handle to a registered GPU asset (SPEC §9 V2.5): the registry slot
/// index plus the slot's generation at issue time.
///
/// Handles are cheap to copy and are the currency scene instances and views
/// exchange — a `MeshInstance` holds one instead of a raw `const data::Mesh*`
/// pointer, so a scene never touches a CPU mesh directly. A handle is valid
/// only for the registry that issued it, and only until the slot is freed:
/// resolving a stale handle returns a typed error, never a crash (SPEC §5).
/// The default-constructed handle `{0, 0}` is the reserved null handle
/// (generation 0 is never issued to a live slot).
struct AssetHandle {
    std::uint32_t index = 0u;      ///< Slot index in the registry's slot table.
    std::uint32_t generation = 0u; ///< Slot generation at issue time.

    /// True for the reserved null handle {0, 0}: the "no asset" instance
    /// (renderers skip it, like the pre-V2 null mesh pointer).
    bool isNull() const noexcept {
        return index == 0u && generation == 0u;
    }
};

/// Two handles are equal iff both their index and generation match.
inline bool operator==(const AssetHandle& a, const AssetHandle& b) noexcept {
    return a.index == b.index && a.generation == b.generation;
}

inline bool operator!=(const AssetHandle& a, const AssetHandle& b) noexcept {
    return !(a == b);
}

/// Generational GPU-asset registry (SPEC §9 V2.5, V2 T3, T7 V3.6).
///
/// Owns exactly one `MeshGeometry` per distinct `data::Mesh` content
/// (content hash of stable bytes, not bare pointer). `registerAsset()` dedups
/// by content hash: registering the same mesh by pointer or a distinct
/// pointer with identical bytes returns the EXISTING handle and does not
/// upload a second GPU object. Dual-key shim `byObject_ + byHash_` in V3.6
/// keeps the pointer path for diagnostics while the hash path is the binding
/// key from `scene::SceneStore::AssetId` (T7, Q3). The mesh-family renderers
/// resolve their handles through the shared registry, so the same content is
/// uploaded once even when both renderers draw it.
///
/// `unregister()` frees a slot (destroying its GPU object), bumps the slot's
/// generation so every outstanding handle to it becomes stale immediately, and
/// makes the slot reusable; a later registration may reuse the freed index
/// with a fresh generation. Resolved geometry pointers stay valid until the
/// slot is freed (each slot's geometry is heap-stable) — the renderers resolve
/// per draw and never retain pointers across registrations.
class AssetRegistry {
   public:
    AssetRegistry() = default;

    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;

    AssetRegistry(AssetRegistry&&) noexcept = default;
    AssetRegistry& operator=(AssetRegistry&&) noexcept = default;

    /// Register `mesh`, uploading its GPU geometry once, and return a copyable
    /// handle. Registering the same CPU object again returns the EXISTING
    /// handle: the slot count stays unchanged and both handles resolve to the
    /// same GPU object (the registry's dedup invariant, V2 T3 gate). Returns a
    /// typed error if the geometry upload fails (no GL context).
    data::Result<AssetHandle> registerAsset(const data::Mesh& mesh);

    /// Resolve `handle` to its GPU geometry. Returns a typed error for an
    /// out-of-range index (code 1), a generation mismatch — a stale/dangling
    /// handle to a freed, reused, or fabricated slot (code 2) — or a handle to
    /// a freed slot (code 3). Never crashes (SPEC §5).
    data::Result<MeshGeometry*> resolve(const AssetHandle& handle);

    /// Free the slot `handle` references: destroy its GPU object, bump the
    /// slot's generation (so the handle and every copy of it become stale),
    /// and make the slot reusable by a later registration. Returns a typed
    /// error for the same invalid-handle cases as `resolve`.
    data::Result<void> unregister(const AssetHandle& handle);

    /// The number of currently registered (live) GPU objects — one per
    /// distinct content hash (V2 T3 gate: registering the same mesh twice
    /// leaves this at exactly 1; T7 extends to content-hash dedup).
    std::size_t slotCount() const noexcept {
        return liveCount_;
    }

   private:
    /// A single registry slot: GPU geometry (heap-stable), stable content hash,
    /// and generation (0 = never allocated; bumped on every free and reuse).
    /// `cpuObject` is the diagnostic pointer shim retained in V3.6 dual-key
    /// mode (not the dedup key — `contentHash` is).
    /// @note lifetime: `cpuObject` borrows the CALLER-owned CPU mesh purely
    /// for diagnostics; entries keyed by it are erased in unregister(), and
    /// the registry never dereferences or frees it (the CPU mesh stays
    /// RE-agnostic, SPEC §7).
    struct Slot {
        std::unique_ptr<MeshGeometry> geometry;
        const data::Mesh* /*borrow*/ cpuObject = nullptr;
        std::uint64_t contentHash{0u};
        std::uint32_t generation = 0u;
    };

    std::vector<Slot> slots_;
    std::vector<std::size_t> freeIndices_;
    std::unordered_map<const data::Mesh*, AssetHandle> byObject_; // dual-key shim
    std::unordered_map<uint64_t, AssetHandle> byHash_;           // content-hash key (T7)
    std::size_t liveCount_{0u};
};

} // namespace re::render