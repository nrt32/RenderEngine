#pragma once

// render/asset_registry.hpp — the unified typed multi-kind GPU asset store
// (SPEC §9 V2.5 for the mesh kind; SPEC §7 T14 for the volume/image/material
// kinds).
//
// One registry instance owns exactly ONE GPU object per distinct asset
// CONTENT, globally across every renderer that resolves through it:
//
//   data::Mesh          → MeshGeometry      (mesh kind, V2 T3 + T7)
//   data::VolumeDataset → core::Texture3D   (volume kind, T14)
//   data::Image         → core::Texture2D   (image kind, T14)
//   PhongMaterial value → canonical IMaterial (material kind, T14)
//
// Scenes carry copyable generational handles instead of raw CPU pointers, and
// the renderers resolve them through the shared registry. Registering the same
// content twice (the same CPU object, or two distinct allocations with
// identical bytes) yields one GPU object — the store dedups by the content
// hash of stable bytes, not by pointer identity — which fixes both the pre-V2
// MeshRenderer+SliceRenderer double-upload and the T14 per-renderer
// double-upload of volumes/images (two VolumeRenderer instances now share one
// Texture3D GL id). The material kind extends the same dedup to material
// VALUES so identical Phong parameters share one canonical instance (the
// scene→RE material hand-off that consumes it is the §12.2 MaterialMapper
// work, tracked with the broker mapper inventory).
//
// Keying and lifetime (T14): every slot carries a `generation` plus the
// `contentHash` it was created from, and every registration takes a reference
// on the slot (`refs`). Registration of already-present content increments
// `refs`; releasing decrements; the slot's GPU object is destroyed and its
// generation bumped (invalidating every outstanding handle) only when the last
// reference drops. A handle whose slot was freed, reused, or fabricated
// resolves to a typed error — never a crash (SPEC §5). The default-constructed
// handle is the reserved null handle: real handles always carry
// `generation >= 1`.
//
// Renderer integration: all four technique renderers hold a shared_ptr to one
// registry (mesh/slice/contour via constructor injection since V2 T5; volume/
// plane likewise since T14) and resolve exclusively through owner-driven
// handles (T7): volumes/images are registered at load/sync time via
// registerVolume/registerImage (hashed at load/register time, never per frame
// per data/content_hash.hpp:31) and renderers resolve via the handles with
// O(1) resolveVolume/resolveImage — no per-frame hashing and no lazy
// store-pinned slots. The process-wide default instance behind the renderer
// defaults is created by `shared()` and must be torn down with `resetShared()`
// while a GL context is still current (the test fixture does this in TearDown).
//
// render/ is GL-call-free: the store uploads geometry through
// MeshGeometry::create and textures through core::Texture3D/Texture2D RAII
// wrappers, never raw GL calls (guardrail gpu_api_ownership). Single-threaded
// (SPEC §5), not thread-safe.
//
// Hash note: the mesh/volume/image content hashes come from the single
// GL-free definition in data/content_hash.hpp (`data::computeContentHash`
// overloads) — the same functions the app-side scene::AssetId uses, so both
// layers compute one asset's content identity identically without either
// layer including the other (broker is the only scene+render library).
// The material kind's hash is local to asset_registry.cpp by design:
// it is defined on the RE-side PhongMaterial VALUE (RE-minimal, §12.4).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/texture2d.hpp"
#include "core/texture3d.hpp"
#include "data/image.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "render/imaterial.hpp"
#include "render/mesh_geometry.hpp"
#include "render/phong_material.hpp"

namespace re::render {

/// Copyable handle to a registered GPU geometry (mesh kind, SPEC §9 V2.5): the
/// registry slot index plus the slot's generation at issue time, plus the
/// content hash the slot was created from (SPEC §7 T7, data/content_hash.hpp:31
/// hashed at load/register time, never per frame). Unified with
/// VolumeTextureHandle/ImageTextureHandle shape: `{index,generation,hash}` is
/// the single identity (content-hash IS identity, no pointer shim, no pinned
/// refs==0 slots). Handles are cheap to copy and are the currency scene
/// instances and views exchange — a `MeshInstance` holds one instead of a raw
/// `const data::Mesh*` pointer, so a scene never touches a CPU mesh directly.
/// A handle is valid only for the registry that issued it, and only until its
/// slot's last reference is released: resolving a stale handle returns a typed
/// error, never a crash (SPEC §5). The default-constructed handle
/// `{0,0,0}` is the reserved null handle (generation 0 is never issued to a
/// live slot).
struct AssetHandle {
    std::uint32_t index = 0u;       ///< Slot index in the registry's slot table.
    std::uint32_t generation = 0u;  ///< Slot generation at issue time.
    std::uint64_t contentHash = 0u;  ///< Content hash the slot was created from.

    /// True for the reserved null handle {0,0,0}: the "no asset" instance
    /// (renderers skip it, like the pre-V2 null mesh pointer).
    bool isNull() const noexcept {
        return index == 0u && generation == 0u && contentHash == 0u;
    }
};

/// Two handles are equal iff their index, generation and content hash match.
inline bool operator==(const AssetHandle& a, const AssetHandle& b) noexcept {
    return a.index == b.index && a.generation == b.generation &&
           a.contentHash == b.contentHash;
}

inline bool operator!=(const AssetHandle& a, const AssetHandle& b) noexcept {
    return !(a == b);
}

/// Copyable handle to a registered GPU 3D texture (volume kind, T14).
///
/// Same generational contract as `AssetHandle`, plus the `contentHash` the
/// slot was created from (the same three-field shape as the app-side
/// `scene::AssetId`): a fabricated handle with the right index and generation
/// but the wrong content hash is rejected at resolve time with a typed error.
/// The default-constructed value is the reserved null handle.
struct VolumeTextureHandle {
    std::uint32_t index = 0u;       ///< Slot index in the volume table.
    std::uint32_t generation = 0u;  ///< Slot generation at issue time.
    std::uint64_t contentHash = 0u; ///< Content hash the slot was created from.

    /// True for the reserved null handle (all fields zero).
    bool isNull() const noexcept {
        return index == 0u && generation == 0u && contentHash == 0u;
    }
};

inline bool operator==(const VolumeTextureHandle& a,
                       const VolumeTextureHandle& b) noexcept {
    return a.index == b.index && a.generation == b.generation &&
           a.contentHash == b.contentHash;
}

inline bool operator!=(const VolumeTextureHandle& a,
                       const VolumeTextureHandle& b) noexcept {
    return !(a == b);
}

/// Copyable handle to a registered GPU 2D texture (image kind, T14).
///
/// Same contract as `VolumeTextureHandle`, for the `data::Image →
/// core::Texture2D` table.
struct ImageTextureHandle {
    std::uint32_t index = 0u;       ///< Slot index in the image table.
    std::uint32_t generation = 0u;  ///< Slot generation at issue time.
    std::uint64_t contentHash = 0u; ///< Content hash the slot was created from.

    /// True for the reserved null handle (all fields zero).
    bool isNull() const noexcept {
        return index == 0u && generation == 0u && contentHash == 0u;
    }
};

inline bool operator==(const ImageTextureHandle& a,
                       const ImageTextureHandle& b) noexcept {
    return a.index == b.index && a.generation == b.generation &&
           a.contentHash == b.contentHash;
}

inline bool operator!=(const ImageTextureHandle& a,
                       const ImageTextureHandle& b) noexcept {
    return !(a == b);
}

/// Copyable handle to a registered canonical material (material kind, T14).
///
/// Same contract as the texture handles: `{index, generation, contentHash}`
/// with the content hash computed from every `PhongMaterial` VALUE field
/// (baseColor RGBA, specular RGB, shininess, ambient, diffuse — float bit
/// patterns in that order), so two distinct allocations carrying identical
/// material values alias to one canonical store-owned instance. The
/// default-constructed value is the reserved null handle.
struct MaterialHandle {
    std::uint32_t index = 0u;       ///< Slot index in the material table.
    std::uint32_t generation = 0u;  ///< Slot generation at issue time.
    std::uint64_t contentHash = 0u; ///< Value hash the slot was created from.

    /// True for the reserved null handle (all fields zero).
    bool isNull() const noexcept {
        return index == 0u && generation == 0u && contentHash == 0u;
    }
};

inline bool operator==(const MaterialHandle& a,
                       const MaterialHandle& b) noexcept {
    return a.index == b.index && a.generation == b.generation &&
           a.contentHash == b.contentHash;
}

inline bool operator!=(const MaterialHandle& a,
                       const MaterialHandle& b) noexcept {
    return !(a == b);
}

/// Generational, ref-counted slot table for one CPU→GPU asset kind (T14
/// internal machinery shared by the volume, image, and material tables).
///
/// Dedup key: the caller-computed `contentHash` of stable bytes (never the
/// pointer). Each slot owns its GPU object (`unique_ptr`) and co-owns the CPU
/// bytes (`shared_ptr<const CpuT>`, T13 ownership ladder), so a live slot
/// always resolves to live bytes. Reference counting: `acquire` with
/// `claimRefs = 1` models owner registration (create with refs 1, or increment
/// on a hit); `claimRefs = 0` was the former lazy find-or-upload path (no
/// reference change, refs==0 pinned slots) deleted in T7 — all current callers
/// use `1`, content-hash IS identity, no lazy insertion; `release` decrements
/// and destroys/invalidates the slot only when the count reaches zero. Slots
/// are reused from a free list with a fresh generation so outstanding handles
/// to previous occupants stay stale.
///
/// Not thread-safe (SPEC §5 single render thread).
template <typename CpuT, typename GpuT>
class GpuSlotTable {
   public:
    /// The co-owned immutable CPU view stored per slot.
    using CpuPtr = std::shared_ptr<const CpuT>;

    /// Kind-specific upload: create the GPU object for `cpu`'s bytes (e.g.
    /// Texture3D::create + voxel upload). Returning an error aborts the
    /// acquisition without mutating the table.
    using Factory = data::Result<std::unique_ptr<GpuT>> (*)(const CpuT&);

    /// Index + live generation of a slot, returned by acquire().
    struct Location {
        std::uint32_t index{0u};
        std::uint32_t generation{0u};
    };

    /// Find `cpu`'s content by hash, or upload it via `factory`. `claimRefs`
    /// is added to the slot's reference count on BOTH paths (1 for owner
    /// registration; 0 was the deleted T7 lazy path, now unused). Returns the
    /// slot location.
    data::Result<Location> acquire(const CpuPtr& cpu, std::uint64_t contentHash,
                                   std::uint32_t claimRefs, Factory factory) {
        if (!cpu) {
            return data::makeError<Location>(
                4, "AssetRegistry: null asset shared_ptr");
        }
        auto hit = byHash_.find(contentHash);
        if (hit != byHash_.end()) {
            if (hit->second < slots_.size()) {
                Slot& slot = slots_[hit->second];
                if (slot.live && slot.contentHash == contentHash) {
                    slot.refs += claimRefs;
                    return data::makeValue<Location>(
                        Location{hit->second, slot.generation});
                }
            }
            // Stale map entry (its slot was freed) — drop it and allocate.
            byHash_.erase(hit);
        }

        // Upload first so a failure never mutates the table.
        auto created = factory(*cpu);
        if (created.failed()) {
            return data::makeError<Location>(created.error().code,
                                             created.error().message);
        }

        Location loc;
        if (!freeIndices_.empty()) {
            // Reuse a freed slot: its generation was bumped at free time and
            // is bumped again here, so every handle to the previous occupant
            // stays stale.
            loc.index = static_cast<std::uint32_t>(freeIndices_.back());
            freeIndices_.pop_back();
            Slot& slot = slots_[loc.index];
            slot.gpu = std::move(*created);
            slot.cpuObject = cpu;
            slot.contentHash = contentHash;
            slot.refs = claimRefs;
            slot.live = true;
            ++slot.generation;
            loc.generation = slot.generation;
        } else {
            // Fresh slot: generation starts at 1 (0 is the never-allocated /
            // null-handle marker).
            Slot slot;
            slot.gpu = std::move(*created);
            slot.cpuObject = cpu;
            slot.contentHash = contentHash;
            slot.refs = claimRefs;
            slot.generation = 1u;
            slot.live = true;
            loc.index = static_cast<std::uint32_t>(slots_.size());
            loc.generation = 1u;
            slots_.push_back(std::move(slot));
        }
        byHash_[contentHash] = loc.index;
        ++liveCount_;
        return data::makeValue<Location>(loc);
    }

    /// Resolve `(index, generation, contentHash)` to the live GPU object.
    /// Typed errors: code 1 out-of-range index, code 2 stale handle
    /// (generation or content-hash mismatch — a freed, reused, or fabricated
    /// handle; freed slots are detected through their bumped generation).
    /// Never crashes (SPEC §5). (Code 3 is the mesh-kind freed-slot code and
    /// is never produced by this table.)
    data::Result<GpuT*> resolve(std::uint32_t index, std::uint32_t generation,
                                std::uint64_t contentHash) const {
        if (index >= slots_.size()) {
            return data::makeError<GpuT*>(
                1, "AssetRegistry: handle index " + std::to_string(index) +
                       " out of range (slot table size " +
                       std::to_string(slots_.size()) + ")");
        }
        const Slot& slot = slots_[index];
        if (!slot.live || slot.generation != generation) {
            return data::makeError<GpuT*>(
                2, "AssetRegistry: stale handle (generation " +
                       std::to_string(generation) + " != slot generation " +
                       std::to_string(slot.generation) + ")");
        }
        if (slot.contentHash != contentHash) {
            return data::makeError<GpuT*>(
                2, "AssetRegistry: stale handle (contentHash mismatch)");
        }
        return data::makeValue<GpuT*>(slot.gpu.get());
    }

    /// Release one reference on `(index, generation, contentHash)`. When the
    /// count reaches zero the GPU object is destroyed (invalidation: the
    /// generation is bumped so every outstanding handle goes stale) and the
    /// slot becomes reusable. Typed errors mirror `resolve`.
    data::Result<void> release(std::uint32_t index, std::uint32_t generation,
                               std::uint64_t contentHash) {
        if (index >= slots_.size()) {
            return data::makeError<void>(1, "AssetRegistry: handle index out "
                                            "of range");
        }
        Slot& slot = slots_[index];
        if (!slot.live || slot.generation != generation) {
            return data::makeError<void>(
                2, "AssetRegistry: stale handle (generation mismatch)");
        }
        if (slot.contentHash != contentHash) {
            return data::makeError<void>(
                2, "AssetRegistry: stale handle (contentHash mismatch)");
        }
        if (slot.refs > 0u) {
            --slot.refs;
        }
        if (slot.refs != 0u) {
            return data::Result<void>(data::value); // other refs keep it alive
        }
        byHash_.erase(slot.contentHash);
        slot.gpu.reset();     // destroys the GPU object
        slot.cpuObject.reset(); // releases the store's co-owned CPU reference
        slot.contentHash = 0u;
        slot.live = false;
        ++slot.generation; // every outstanding handle to this slot is stale
        freeIndices_.push_back(index);
        --liveCount_;
        return data::Result<void>(data::value);
    }

    /// Current reference count of a live slot (typed errors mirror resolve).
    data::Result<std::uint32_t> refsAt(std::uint32_t index,
                                       std::uint32_t generation,
                                       std::uint64_t contentHash) const {
        auto resolved = resolve(index, generation, contentHash);
        if (resolved.failed()) {
            return data::makeError<std::uint32_t>(resolved.error().code,
                                                  resolved.error().message);
        }
        return data::makeValue<std::uint32_t>(slots_[index].refs);
    }

    /// Number of currently live slots (one per distinct content hash).
    std::size_t liveCount() const noexcept {
        return liveCount_;
    }

   private:
    struct Slot {
        std::unique_ptr<GpuT> gpu;   ///< Owned GPU object (reset on free).
        CpuPtr cpuObject;            ///< Co-owned CPU bytes (reset on free).
        std::uint64_t contentHash{0u};
        std::uint32_t generation{0u}; ///< 0 = never allocated; bumped on free.
        std::uint32_t refs{0u};       ///< Outstanding owner references.
        bool live{false};
    };
    std::vector<Slot> slots_;
    std::vector<std::size_t> freeIndices_;
    std::unordered_map<std::uint64_t, std::uint32_t> byHash_;
    std::size_t liveCount_{0u};
};

/// Unified typed multi-kind GPU asset store (SPEC §9 V2.5 mesh kind; SPEC §7
/// T14 volume/image/material kinds) — see the file-header comment for the full
/// design.
///
/// Owns exactly one GPU object per distinct asset content across ALL technique
/// renderers that resolve through the instance. Four kinds share the same
/// generational handle contract (`{index, generation[, contentHash]}`, typed
/// stale-handle errors, ref-counted release):
///
/// | Kind | Register | Resolve | Release |
/// |---|---|---|---|
/// | `data::Mesh → MeshGeometry` | `registerAsset` | `resolve` | `unregister` |
/// | `data::VolumeDataset → core::Texture3D` | `registerVolume` | `resolveVolume` | `unregisterVolume` |
/// | `data::Image → core::Texture2D` | `registerImage` | `resolveImage` | `unregisterImage` |
/// | `PhongMaterial value → canonical IMaterial` | `registerMaterial` | `resolveMaterial` | `unregisterMaterial` |
///
/// The mesh kind keeps its V2-era idempotent registration shape (dedup by
/// content hash + diagnostic pointer shim; regression lock R3 — its public
/// behavior is unchanged except that unregistering content that was
/// registered N times now requires N unregisters to destroy the GPU object).
/// The volume/image/material kinds use the same semantics via `GpuSlotTable`
/// from day one, with no pointer-keyed maps anywhere.
class AssetRegistry {
   public:
    AssetRegistry() = default;

    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;

    AssetRegistry(AssetRegistry&&) noexcept = default;
    AssetRegistry& operator=(AssetRegistry&&) noexcept = default;

    // ------------------------------------------------------------------
    // Process-wide default instance (renderer constructor defaults, T14).
    // ------------------------------------------------------------------

    /// The process-wide registry used as the default asset store by the
    /// volume/plane renderer constructors: two default-constructed renderers
    /// therefore share one GPU object per content (the T14 invariant).
    /// Single-threaded by design (SPEC §5); recreated on demand after
    /// resetShared().
    static std::shared_ptr<AssetRegistry> shared();

    /// Destroy the process-wide instance NOW, while a GL context is current,
    /// so its GPU objects are deleted with valid GL state instead of during
    /// static destruction after context death. Safe to call when the shared
    /// instance was never created. The next shared() recreates it empty.
    static void resetShared();

    // ------------------------------------------------------------------
    // Mesh kind: data::Mesh → MeshGeometry (V2 T3 / T7 — unchanged API).
    // ------------------------------------------------------------------

    /// Register `mesh`, uploading its GPU geometry once, and return a copyable
    /// handle. Registering the same content again (same object or identical
    /// bytes) returns the EXISTING handle and takes one more reference on the
    /// slot: the slot count stays unchanged and both handles resolve to the
    /// same GPU object. Returns a typed error if the geometry upload fails
    /// (no GL context).
    data::Result<AssetHandle> registerAsset(const data::Mesh& mesh);

    /// Resolve `handle` to its GPU geometry. Returns a typed error for an
    /// out-of-range index (code 1), a generation mismatch — a stale/dangling
    /// handle to a freed, reused, or fabricated slot (code 2) — or a handle to
    /// a freed slot (code 3). Never crashes (SPEC §5).
    data::Result<MeshGeometry*> resolve(const AssetHandle& handle);

    /// Release one reference on the slot `handle` references. When the last
    /// reference drops: destroy its GPU object, bump the slot's generation
    /// (so the handle and every copy become stale), and make the slot
    /// reusable. Returns a typed error for the same invalid-handle cases as
    /// `resolve`.
    data::Result<void> unregister(const AssetHandle& handle);

    /// The number of currently registered (live) GPU geometries — one per
    /// distinct content hash (V2 T3 gate: registering the same mesh twice
    /// leaves this at exactly 1; T7 extends dedup to identical-byte copies).
    std::size_t slotCount() const noexcept {
        return liveCount_;
    }

    // ------------------------------------------------------------------
    // Volume kind: data::VolumeDataset → core::Texture3D (T14).
    // ------------------------------------------------------------------

    /// Acquire `dataset` as a GPU 3D texture: uploads it once (GL_R32F,
    /// trilinear filtering, clamp-to-edge) and takes one reference. Registering
    /// identical content again — including through a second renderer or a
    /// distinct allocation with identical voxels — returns the SAME handle
    /// backed by ONE GL texture (content-hash dedup, no pointer maps).
    data::Result<VolumeTextureHandle> registerVolume(
        const std::shared_ptr<const data::VolumeDataset>& dataset);

    /// Resolve `handle` to its live texture. Typed errors mirror the volume
    /// kind: code 1 out-of-range index, code 2 stale handle (freed, reused,
    /// or fabricated) — never a crash (SPEC §5).
    data::Result<core::Texture3D*> resolveVolume(
        const VolumeTextureHandle& handle) const;

    /// Release one reference; the texture is destroyed and its handle
    /// invalidated when the last reference drops.
    data::Result<void> unregisterVolume(const VolumeTextureHandle& handle);

    /// Live reference count of the slot `handle` points at (gate evidence:
    /// registering twice yields refs 2, one release keeps it resolvable).
    data::Result<std::uint32_t> volumeRefs(
        const VolumeTextureHandle& handle) const;

    /// Number of currently live volume textures (one per distinct voxel
    /// content).
    std::size_t volumeSlotCount() const noexcept {
        return volumes_.liveCount();
    }

    // ------------------------------------------------------------------
    // Image kind: data::Image → core::Texture2D (T14).
    // ------------------------------------------------------------------

    /// Acquire `image` as a GPU 2D texture: converts to RGBA8 (row-flipped to
    /// GL bottom-up order) and uploads once, taking one reference. Identical
    /// pixel content shares one GL texture regardless of CPU address.
    data::Result<ImageTextureHandle> registerImage(
        const std::shared_ptr<const data::Image>& image);

    /// Resolve `handle` to its live texture. Typed errors mirror the volume
    /// kind (codes 1/2 — stale handles are errors, never a crash).
    data::Result<core::Texture2D*> resolveImage(
        const ImageTextureHandle& handle) const;

    /// Release one reference; the texture is destroyed and its handle
    /// invalidated when the last reference drops.
    data::Result<void> unregisterImage(const ImageTextureHandle& handle);

    /// Live reference count of the slot `handle` points at.
    data::Result<std::uint32_t> imageRefs(
        const ImageTextureHandle& handle) const;

    /// Number of currently live image textures (one per distinct pixel
    /// content).
    std::size_t imageSlotCount() const noexcept {
        return images_.liveCount();
    }

    // ------------------------------------------------------------------
    // Material kind: PhongMaterial value → canonical IMaterial (T14).
    // ------------------------------------------------------------------

    /// Acquire a canonical shared material for `material`'s VALUE: the store
    /// keeps one immutable `PhongMaterial` instance per distinct value tuple
    /// (baseColor RGBA, specular RGB, shininess, ambient, diffuse — every
    /// field participates in the identity hash) and takes one reference.
    /// Registering identical values again — distinct allocation or not —
    /// returns the SAME handle backed by ONE canonical instance, so identical
    /// materials dedup exactly like meshes/textures do. The store-owned
    /// canonical is immutable by convention: resolve it read-only
    /// (`resolveMaterial` returns `IMaterial*`), never mutate it in place
    /// (mutation would desynchronize the slot's contentHash from its bytes —
    /// register new values instead).
    data::Result<MaterialHandle> registerMaterial(
        const std::shared_ptr<const PhongMaterial>& material);

    /// Resolve `handle` to its live canonical material. Typed errors mirror
    /// the texture kinds (codes 1/2 — stale handles are errors, never a
    /// crash). The pointer aliases store-owned storage: valid until the
    /// slot's last reference is released or the store dies.
    data::Result<IMaterial*> resolveMaterial(const MaterialHandle& handle) const;

    /// Release one reference; the canonical instance is destroyed and its
    /// handle invalidated when the last reference drops.
    data::Result<void> unregisterMaterial(const MaterialHandle& handle);

    /// Live reference count of the slot `handle` points at (gate evidence for
    /// the ref-counting contract).
    data::Result<std::uint32_t> materialRefs(const MaterialHandle& handle) const;

    /// Number of currently live canonical materials (one per distinct value
    /// tuple).
    std::size_t materialSlotCount() const noexcept {
        return materials_.liveCount();
    }

   private:
    /// A single mesh-kind slot: GPU geometry (heap-stable), stable content
    /// hash, reference count, and generation (0 = never allocated; bumped on
    /// every free and reuse). Content-hash IS identity (SPEC §7 T7,
    /// data/content_hash.hpp:31 hashed at load/register time, never per frame);
    /// no pointer shim remains — dedup is solely via `byHash_`.
    struct Slot {
        std::unique_ptr<MeshGeometry> geometry;
        std::uint64_t contentHash{0u};
        std::uint32_t refs{0u};
        std::uint32_t generation = 0u;
    };

    std::vector<Slot> slots_;
    std::vector<std::size_t> freeIndices_;
    // Content-hash dedup only (SPEC §7 T7): `byHash_` maps stable-byte hash to
    // slot; identical bytes alias one slot regardless of heap address, no
    // pointer-key map remains (T7 deletes the dual-key shim).
    std::unordered_map<uint64_t, AssetHandle> byHash_;
    std::size_t liveCount_{0u};

    GpuSlotTable<data::VolumeDataset, core::Texture3D> volumes_;
    GpuSlotTable<data::Image, core::Texture2D> images_;
    GpuSlotTable<PhongMaterial, IMaterial> materials_;
};

/// Shared mesh-geometry resolution for every mesh-family renderer
/// (MeshRenderer, SliceRenderer, ContourRenderer — the former per-renderer
/// `geometryFor` copy-paste, now ONE helper over the AssetRegistry).
///
/// A null `registry` (only possible by explicit construction request — the
/// shared-ownership injection makes member-init-order nulls impossible, T13)
/// fails with typed error code 4 naming `rendererName`; otherwise the handle
/// resolves through `registry->resolve` (typed stale/dangling-handle errors,
/// SPEC §5). The single owner of GPU geometry is the registry itself (SPEC §9
/// V2.5): resolving a handle returns the one GPU object registered for that
/// CPU mesh, shared across renderers and views.
///
/// @note lifetime: non-owning view of registry-owned storage (the shared
/// slot's unique_ptr) — valid until the handle's slot is unregistered.
data::Result<MeshGeometry*> resolveMeshGeometry(
    const std::shared_ptr<AssetRegistry>& registry, const AssetHandle& handle,
    std::string_view rendererName);

} // namespace re::render
