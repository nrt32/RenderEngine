#pragma once

// broker/view_compositor.hpp — ViewCompositor (SPEC §11 V3.2b T3).
//
// SRP: single responsibility is own ReView -> renderAll()/presentAll(dst) via
// render dispatch (owns ReView lifetime map LayoutId->ReView). Does not poll
// SceneStore or cache CompositeKey (that's ViewSynchronizer). No GL calls here;
// delegates to core/ via render/ helpers (gpu_api_ownership).

#include "broker/broker.hpp"
#include "core/framebuffer.hpp"
#include "data/result.hpp"

namespace re::broker {

/// View compositor — dispatch/present side of IViewBridge (SRP via composition).
class ViewCompositor {
   public:
    explicit ViewCompositor(Broker* broker) : broker_(broker) {}

    /// Dispatch already-synced ReViews to their targets (no poll).
    data::Result<void> renderAll();

    /// Present already-rendered ReViews via core::blit to destination.
    data::Result<void> presentAll(core::Framebuffer* /*destination*/);

   private:
    Broker* broker_;
};

} // namespace re::broker
