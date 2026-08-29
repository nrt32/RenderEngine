#pragma once

// scene/layer.hpp — dumb visual stacking layers for deterministic composition (eight anonymous layers, scoped priority orthogonal).
//
// Every scene object carries a Layer tag (eight anonymous values, 0..7) that decides its visual stacking order independent of its creation or insertion order — painter's order is then defined first by layer index ascending (lower numeric draws first and may be overdrawn by higher layers), then by technique priority within the same layer, then by per-object priority scoped to the same layer+type bucket, then by insertion order. The final image is deterministic regardless of store insertion order because the synchronizer groups by (layer, techniqueOrder, priority) ascending before dispatching to the render-side view. Layer has no semantic names (Volume/Mesh etc.) so techniqueKind is orthogonal — Volume and Mesh on the same lowest layer are ordered by techniqueOrder, not by layer name. There is no per-view mask type and no bitmask (1u << layer) — layers are not a bitset, no UB shift, no array sized by COUNT. This header owns only the enumeration; View and the synchronizer own ordering. Pure value types, no GL, no render dependency. Lower numeric draws first, 64 is doc edit only (no 1u<<layer UB, no array sized by COUNT).

#include <cstdint>

namespace re::scene {

/// Dumb visual stacking order — eight anonymous layers with no semantic names (Volume/Mesh etc.) so techniqueKind stays orthogonal. Lower numeric values are drawn first and may be overdrawn by higher layers; the final image is deterministic regardless of store insertion order because the synchronizer groups by (layer, techniqueOrder, priority) ascending before dispatching.
enum class Layer : uint16_t {
    LAYER_0 = 0,
    LAYER_1 = 1,
    LAYER_2 = 2,
    LAYER_3 = 3,
    LAYER_4 = 4,
    LAYER_5 = 5,
    LAYER_6 = 6,
    LAYER_7 = 7,
    COUNT = 8
};

} // namespace re::scene
