#pragma once

// broker/i_view_bridge.hpp — IViewBridge / ISyncBridge / IRenderBridge (SPEC §11 V3.2b T3).
//
// ISP segregation: ISyncBridge{sync} + IRenderBridge{renderAll,presentAll}
// compose to IViewBridge (headless sync-only test depends only on ISyncBridge).
// App depends on IViewBridge abstraction (DIP), never on concrete ViewBridge.
// SRP: ViewSynchronizer owns cache/dirty, ViewCompositor owns dispatch/present,
// ViewBridge is coordinator only.

#include <span>

#include "core/framebuffer.hpp"
#include "data/result.hpp"
#include "scene/store.hpp"
#include "scene/view.hpp"

namespace re::broker {

/// Sync-only bridge (ISP headless slice).
class ISyncBridge {
   public:
    virtual ~ISyncBridge() = default;
    /// Sync app views + scene store into cached Re state (ViewSynchronizer).
    virtual data::Result<void> sync(std::span<const scene::View> views,
                                    const scene::SceneStore& scene) = 0;
};

/// Render/present bridge (ISP).
class IRenderBridge {
   public:
    virtual ~IRenderBridge() = default;
    virtual data::Result<void> renderAll() = 0;
    virtual data::Result<void> presentAll(core::Framebuffer* destination) = 0;
};

/// Full view bridge façade (composes sync + render).
class IViewBridge : public ISyncBridge, public IRenderBridge {
   public:
    virtual ~IViewBridge() = default;
};

} // namespace re::broker
