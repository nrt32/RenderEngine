#pragma once

// broker/view_bridge.hpp — ViewBridge façade composing ViewSynchronizer + ViewCompositor (SPEC §11 V3.2b T3).
//
// SRP via composition: ViewBridge is coordinator only (orchestration actor),
// owns no state beyond the two collaborators (StackOverflow Facade SRP tension
// per §11.3.1). App depends on IViewBridge abstraction (DIP), never holds a
// mapper handle.

#include <memory>
#include <span>

#include "broker/broker.hpp"
#include "broker/i_view_bridge.hpp"
#include "broker/render_stack.hpp"
#include "broker/view_compositor.hpp"
#include "broker/view_synchronizer.hpp"
#include "core/framebuffer.hpp"
#include "data/result.hpp"
#include "scene/store.hpp"
#include "scene/view.hpp"

namespace re::broker {

/// ViewBridge — façade over ViewSynchronizer + ViewCompositor (DIP).
///
/// Single responsibility is orchestration: delegates sync()->synchronizer,
/// renderAll()/presentAll()->compositor. App code path uses only IViewBridge&
/// (app never calls CameraMapper etc. directly).
///
/// Ownership (T13): the bridge is the only external owner of both
/// collaborators (`shared_ptr` handles — the shared control block exists so
/// the synchronizer can hold a real `weak_ptr` OBSERVER of its compositor,
/// the textbook mutual-back-pointer fix). No other component may retain these
/// handles: the bridge is the composition root. The optional RenderStack is a
/// SHARED co-owned wiring (both collaborators see the same renderer set —
/// without it item layers cannot be built and sync reports a typed error).
class ViewBridge : public IViewBridge {
   public:
    /// Construct with explicit synchronizer/compositor (SRP injection). The
    /// bridge takes ownership of both and wires the (weak) compositor
    /// back-pointer on the synchronizer.
    ViewBridge(std::shared_ptr<ViewSynchronizer> sync,
               std::shared_ptr<ViewCompositor> comp)
        : sync_(std::move(sync)), comp_(std::move(comp)) {
        if (sync_ && comp_) sync_->setCompositor(comp_);
    }

    /// Convenience: build from Broker (creates default synchronizer+compositor).
    /// @note lifetime: `broker` is a SHARED reference co-owned by both
    /// collaborators — wiring can never outlive it.
    explicit ViewBridge(std::shared_ptr<Broker> broker)
        : sync_(std::make_unique<ViewSynchronizer>(broker)),
          comp_(std::make_unique<ViewCompositor>(std::move(broker))) {
        sync_->setCompositor(comp_);
    }

    /// Full wiring: Broker + RenderStack (the composition-root form used by
    /// AppContext). Both collaborators share the stack.
    static std::shared_ptr<ViewBridge> create(
        std::shared_ptr<Broker> broker, std::shared_ptr<RenderStack> stack) {
        auto comp = std::make_shared<ViewCompositor>(broker, stack);
        auto sync = std::make_shared<ViewSynchronizer>(broker, comp,
                                                       nullptr, stack);
        return std::make_shared<ViewBridge>(std::move(sync), std::move(comp));
    }

    data::Result<void> sync(std::span<const scene::View> views,
                            const scene::SceneStore& scene) override {
        return sync_->sync(views, scene);
    }

    /// Layout-scoped sync: same contract as `sync`, plus an explicit layoutId
    /// so two layouts can hold different views under the same view ids without
    /// colliding — the persistence key becomes (layout, view) instead of the
    /// bare view id (composite-key identity).
    data::Result<void> syncWithLayout(std::span<const scene::View> views,
                                      const scene::SceneStore& scene, uint64_t layoutId) {
        return sync_->sync(views, scene, layoutId);
    }

    data::Result<void> renderAll() override { return comp_->renderAll(); }

    /// @note lifetime: `destination` is a call-scoped borrow (see
    /// ViewCompositor::presentAll).
    data::Result<void> presentAll(core::Framebuffer* /*borrow*/ destination) override {
        return comp_->presentAll(destination);
    }

    /// Non-owning views over bridge-owned storage.
    /// @note lifetime: both collaborators are solely owned by this bridge
    /// (shared handles so the synchronizer can observe the compositor weakly
    /// — see the class comment); the returned aliases are valid while the
    /// bridge is. Never delete through them.
    ViewSynchronizer* /*borrow*/ synchronizer() noexcept { return sync_.get(); }
    ViewCompositor* /*borrow*/ compositor() noexcept { return comp_.get(); }

   private:
    std::shared_ptr<ViewSynchronizer> sync_;
    std::shared_ptr<ViewCompositor> comp_;
};

} // namespace re::broker
