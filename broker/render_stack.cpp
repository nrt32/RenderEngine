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
    // CSG Puxel stage and renderer (V7 T6, Approach C) — co-owned with AppContext/tests so ReView never outlives them; the stage owns head R32UI + node 16B SSBOs with maxFpp 8 default clamped [1,16] (nodeCapacity=w*h*maxFpp*16 ≤152 MB 157286400) and the renderer is the stateless dispatcher that captures via imageAtomicExchange (front+back facing ±1) before the resolve k-way merge. (V7 T6)
    stack->csgStage = std::make_shared<render::CsgOitStage>(8u);
    stack->csg = std::make_shared<render::CsgRenderer>(stack->assets, stack->csgStage);
    return stack;
}

} // namespace re::broker
