#pragma once

// broker/view_compositor.hpp — ViewCompositor (SPEC §11 V3.2b T3, V3.5 T6 full).
//
// SRP: single responsibility is own ReView -> renderAll()/presentAll(dst) via
// render dispatch (owns ReView lifetime map LayoutId->ReView). Does not poll
// SceneStore or cache CompositeKey (that's ViewSynchronizer). Handles
// persistence: ReView identity by CompositeKey{Version,LayoutId,ViewId} stable,
// ViewTarget inner FBO recreated only on rect size change (size hash includes
// physical pixels framebufferSize + contentScale). No GL calls here except via
// render::View helpers (gpu_api_ownership).

#include <memory>
#include <unordered_map>
#include <vector>

#include "broker/broker.hpp"
#include "core/framebuffer.hpp"
#include "data/result.hpp"
#include "render/view.hpp"
#include "scene/composite_key.hpp"
#include "scene/view.hpp"

namespace re::broker {

/// View compositor — dispatch/present side of IViewBridge (SRP via composition).
///
/// Ownership (T13): the Broker is a SHARED reference (co-owned wiring); the
/// ReViews in `views_` are SOLELY OWNED (`unique_ptr`) — accessors returning
/// raw View pointers are documented non-owning views over that storage.
class ViewCompositor {
   public:
    explicit ViewCompositor(std::shared_ptr<Broker> broker)
        : broker_(std::move(broker)) {}

    // --- ReView lifetime (persistence by CompositeKey stable part) ------------

    /// Stable key for ReView identity (Version + LayoutId + ViewId).
    struct StableKey {
        uint32_t version{1};
        uint64_t layoutId{0};
        uint64_t viewId{0};
        bool operator==(const StableKey& o) const noexcept {
            return version == o.version && layoutId == o.layoutId && viewId == o.viewId;
        }
    };
    struct StableKeyHash {
        std::size_t operator()(const StableKey& k) const noexcept {
            std::size_t h = static_cast<std::size_t>(k.version);
            h ^= std::hash<uint64_t>{}(k.layoutId) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            h ^= std::hash<uint64_t>{}(k.viewId) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            return h;
        }
    };

    /// Get existing ReView by stable key (or nullptr).
    /// @note lifetime: non-owning view over compositor-owned `unique_ptr`
    /// storage (views_) — valid until the ReView is pruned/cleared or the
    /// compositor dies; never delete through it.
    render::View* /*borrow*/ getView(uint64_t layoutId, uint64_t viewId) noexcept;
    /// @note lifetime: same views_-owned storage borrow as the non-const
    /// getView().
    const render::View* /*borrow*/ getView(uint64_t layoutId, uint64_t viewId) const noexcept;

    /// Ensure ReView exists for appView (create if missing, otherwise return existing).
    /// Returned pointer is stable identity (same &ReView across syncs when layoutId+viewId same).
    /// @note lifetime: same views_-owned storage borrow as getView() —
    /// identity-stable across syncs while layoutId+viewId are unchanged.
    render::View* /*borrow*/ ensureView(uint64_t layoutId, const scene::View& appView);

    /// Prune ReViews for a layout to activeViewIds set (layout count/set change -> insert/erase).
    void pruneLayout(uint64_t layoutId, const std::vector<uint64_t>& activeViewIds);

    /// Total number of cached ReViews (for tests).
    size_t viewCount() const noexcept { return views_.size(); }

    /// Access all cached ReViews (for renderAll/presentAll).
    const std::unordered_map<StableKey, std::unique_ptr<render::View>, StableKeyHash>& views() const noexcept {
        return views_;
    }

    /// Dispatch already-synced ReViews to their targets (no poll).
    data::Result<void> renderAll();

    /// Present already-rendered ReViews via core::blit to destination.
    /// @note lifetime: `destination` is borrowed for the DURATION OF THIS
    /// CALL only (null = window default framebuffer); owned by the caller.
    data::Result<void> presentAll(core::Framebuffer* /*borrow*/ destination);

    /// For determinism: clear all cached ReViews.
    void clear() noexcept;

   private:
    std::shared_ptr<Broker> broker_;
    std::unordered_map<StableKey, std::unique_ptr<render::View>, StableKeyHash> views_{};
};

} // namespace re::broker
