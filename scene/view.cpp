#include "scene/view.hpp"

namespace re::scene {

void View::setLayerMask(LayerMask m) noexcept {
    if (layerMask != m) {
        layerMask = m;
        ++layerGen;
        ++generation;
    }
}

void View::setOverride(uint64_t id, Layer l) {
    auto it = layerOverrides.find(id);
    if (it == layerOverrides.end() || it->second != l) {
        layerOverrides[id] = l;
        ++layerGen;
        ++generation;
    }
}

void View::clearOverride(uint64_t id) noexcept {
    auto it = layerOverrides.find(id);
    if (it != layerOverrides.end()) {
        layerOverrides.erase(it);
        ++layerGen;
        ++generation;
    }
}

void View::clearAllOverrides() noexcept {
    if (!layerOverrides.empty()) {
        layerOverrides.clear();
        ++layerGen;
        ++generation;
    }
}

} // namespace re::scene
