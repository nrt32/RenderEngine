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
class ViewBridge : public IViewBridge {
   public:
    /// Construct with explicit synchronizer/compositor (SRP injection).
    ViewBridge(std::unique_ptr<ViewSynchronizer> sync,
               std::unique_ptr<ViewCompositor> comp)
        : sync_(std::move(sync)), comp_(std::move(comp)) {
        if (sync_ && comp_) sync_->setCompositor(comp_.get());
    }

    /// Convenience: build from Broker (creates default synchronizer+compositor).
    explicit ViewBridge(Broker* broker)
        : sync_(std::make_unique<ViewSynchronizer>(broker, nullptr)),
          comp_(std::make_unique<ViewCompositor>(broker)) {
        sync_->setCompositor(comp_.get());
    }

    data::Result<void> sync(std::span<const scene::View> views,
                            const scene::SceneStore& scene) override {
        return sync_->sync(views, scene);
    }

    /// Extended sync with explicit layoutId persistence (SPEC §10.2 composite key).
    data::Result<void> syncWithLayout(std::span<const scene::View> views,
                                      const scene::SceneStore& scene, uint64_t layoutId) {
        return sync_->sync(views, scene, layoutId);
    }

    data::Result<void> renderAll() override { return comp_->renderAll(); }

    data::Result<void> presentAll(core::Framebuffer* destination) override {
        return comp_->presentAll(destination);
    }

    ViewSynchronizer* synchronizer() noexcept { return sync_.get(); }
    ViewCompositor* compositor() noexcept { return comp_.get(); }

   private:
    std::unique_ptr<ViewSynchronizer> sync_;
    std::unique_ptr<ViewCompositor> comp_;
};

} // namespace re::broker
