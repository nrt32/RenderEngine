// broker/view_compositor.cpp — ViewCompositor skeleton (no GL direct, delegates to core/ via render/).

#include "broker/view_compositor.hpp"

namespace re::broker {

data::Result<void> ViewCompositor::renderAll() {
    // T5 will wire ReView list + per-view render dispatch via IRHIContext.
    // For T3 this is a no-op success (broker forwarding keeps V2 renderers green).
    return data::Result<void>(data::value);
}

data::Result<void> ViewCompositor::presentAll(core::Framebuffer* /*destination*/) {
    // T5 wires core::blit. No-op for T3.
    return data::Result<void>(data::value);
}

} // namespace re::broker
