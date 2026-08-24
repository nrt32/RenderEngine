// broker/render_stack.cpp — RenderStack factory (see header).

#include "broker/render_stack.hpp"

namespace re::broker {

std::shared_ptr<RenderStack> RenderStack::create(
    std::shared_ptr<render::AssetRegistry> assets, bool enableOIT) {
    auto stack = std::make_shared<RenderStack>();
    stack->assets = assets ? std::move(assets) : render::AssetRegistry::shared();
    if (enableOIT) {
        stack->pipeline = std::make_shared<render::LinkedListOIT>();
    }
    // The MeshRenderer is constructed WITH the pipeline when OIT is enabled
    // so its direct-render path and the compositor's capture stage agree on
    // the same pipeline instance (FR-render.3 engagement evidence).
    stack->mesh = std::make_shared<render::MeshRenderer>(stack->assets,
                                                         stack->pipeline);
    stack->meshSlice = std::make_shared<render::SliceRenderer>(stack->assets);
    stack->volume = std::make_shared<render::VolumeRenderer>(stack->assets);
    stack->slice = std::make_shared<render::VolumeSliceRenderer>(stack->assets);
    stack->plane = std::make_shared<render::PlaneRenderer>(stack->assets);
    stack->contour = std::make_shared<render::ContourRenderer>(stack->assets);
    return stack;
}

} // namespace re::broker
