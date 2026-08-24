#pragma once

// broker/camera_mapper.hpp — CameraMapper: ICachedMapper<scene::Camera, render::Camera> (T3 V3.2b, T4 V3.3).
//
// One file per mapper (guardrail broker_per_type). The per-field viewGen/
// projGen split means a Camera::rotate(1°) dirties only viewGen, not projGen
// (SPEC §10.4, T4) — the SYNCHRONIZER uses those generations to decide WHETHER
// this mapper runs at all. Pure translation is viewMatrix/projMatrix/position
// forwarding — no GL, no core/ GL calls (gpu_api_ownership: broker stays
// outside core/). T4 validates 2D ortho vs 3D perspective (plane present →
// ortho) via TranslateContext::hasPlane().
//
// mapCached memoizes per CAMERA IDENTITY (persistence-honesty task T21):
// each owning view id (TranslateContext::view.viewId) owns ONE memo slot
// holding its last translation together with the scene::CompositeKey it was
// computed from ({viewId, generations, FNV-1a fingerprint of the camera's
// stable parameter bytes}). A hit therefore implies byte-identical input, so
// serving the memo is indistinguishable from recomputing; a second camera's
// entry lives in ITS OWN slot, which is what kills the old single-slot
// defect where two views' cameras thrashed one shared entry (and at
// generation zero even served each other's matrices). One slot per id also
// bounds memory by the view count — a continuously orbiting camera replaces
// its slot instead of accumulating history. invalidate(id) evicts exactly
// that view's slot. Hit/miss counters are exposed as test evidence that
// alternating pans over several cameras produce per-camera hits (no
// cross-camera thrash).

#include <cstdint>
#include <unordered_map>

#include "broker/i_mapper.hpp"
#include "render/types.hpp"
#include "scene/camera.hpp"
#include "scene/composite_key.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Camera mapper — translation scene::Camera -> render::Camera.
///
/// Validates that a 2D view (plane present) carries an orthographic camera
/// and a 3D view (no plane) carries a perspective camera (T4 V3.3, SPEC
/// §3.1); mismatch returns typed error code 4 — on BOTH entry points, never
/// bypassed.
class CameraMapper : public ICachedMapper<scene::Camera, render::Camera> {
   public:
    using AppType = scene::Camera;
    using ReType = render::Camera;
    CameraMapper() = default;

    /// Pure translation: app.viewMatrix/projMatrix/eye -> Re camera.
    data::Result<render::Camera> map(const scene::Camera& app,
                                     const scene::TranslateContext& ctx) const override;

    /// Cached translation: validates (always — a hit must never bypass the
    /// 2D/3D pairing check), then serves the per-view memo when the
    /// CompositeKey of {viewId, gens, stable param bytes} is unchanged;
    /// otherwise re-translates and replaces that view's slot.
    data::Result<render::Camera> mapCached(const scene::Camera& app,
                                           const scene::TranslateContext& ctx) override;

    /// Evict the memo slot of view `id` (the id-keyed half of the
    /// ICachedMapper contract); all other views keep theirs.
    void invalidate(uint64_t id) override;

    // --- Test-evidence counters (spy surface for the cache contract) --------
    /// Memo hits served without re-translating.
    uint64_t cacheHits() const noexcept { return hits_; }
    /// Full translations performed (misses).
    uint64_t cacheMisses() const noexcept { return misses_; }
    /// Views currently holding a memo slot (one slot max per view id).
    std::size_t cacheEntries() const noexcept { return cache_.size(); }

   private:
    /// Build the cache key from the owning view id + both camera generations
    /// + an FNV-1a fingerprint of the camera's stable parameter bytes (via
    /// CompositeKey::hashStableBytes — the one canonical byte hash).
    static scene::CompositeKey makeKey(const scene::Camera& app,
                                       const scene::TranslateContext& ctx) noexcept;

    /// One memo slot per owning-view identity: the exact key the value was
    /// translated from plus the translated render-side camera.
    struct Memo {
        scene::CompositeKey key{};
        render::Camera value{};
    };
    std::unordered_map<uint64_t, Memo> cache_{};
    uint64_t hits_{0};
    uint64_t misses_{0};
};

} // namespace re::broker
