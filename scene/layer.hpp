#pragma once

// scene/layer.hpp — visual stacking layers for deterministic composition (eight layers, technique priority orthogonal).
//
// Every scene object carries a Layer tag (Background..OverlayTop) that decides its visual stacking order independent of its creation or insertion order — painter's order is then defined first by layer index, then by technique priority within the same layer, so swapping itemIds never changes the image. The mask type is a 32-bit bitset (1u shifted by layer) future-proofed beyond eight bits, with the initial eight entries covering the standard stack. This header owns only the enumeration and the bitmask type; View and the synchronizer own masking, per-view overrides, and the orthogonal technique ordering. Pure value types, no GL, no render dependency.

#include <cstdint>

namespace re::scene {

/// Visual stacking order for objects and overlays — eight discrete layers that cover the standard visualization stack from background through translucent volume, sliced volume, textured plane, opaque mesh, mesh slice, mesh contour, to the topmost overlay. Lower numeric values are drawn first and may be overdrawn by higher layers; the final image is deterministic regardless of store insertion order because the synchronizer groups by (layer, techniquePriority) ascending before dispatching to the render-side view.
enum class Layer : uint8_t {
    Background = 0,
    Volume = 1,
    VolumeSlice = 2,
    Plane = 3,
    Mesh = 4,
    MeshSlice = 5,
    Contour = 6,
    OverlayTop = 7,
    Count = 8
};

/// Bitmask that selects which layers are visible for a given view — bit N corresponds to the layer index (shifted). The mask is per-view so a view can hide an entire layer without removing its objects from the store, keeping store generation stable while changing only view generation.
using LayerMask = uint32_t;

} // namespace re::scene
