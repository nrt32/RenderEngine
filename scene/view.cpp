#include "scene/view.hpp"

// scene/view.cpp — View non-inline methods for dumb layers T5.
//
// At T5 the per-view bitmask and per-object override map that together
// replaced insertion-order painting with deterministic (layer,
// techniquePriority) grouping are deleted. The old mask hid whole layers
// without removing objects and the old override reassigned a single object's
// effective layer for this view only. Both bumped layerGen so the
// synchronizer re-grouped. With dumb LAYER_0..7 the stacking is determined
// solely by the object's Layer tag and its scoped priority (lower numeric
// draws first, no bitset, no override, no 1u<<layer UB). This translation
// unit therefore has no per-view mask or override setters; setDepthConfig
// lives in
// broker/view_synchronizer.cpp to keep grep -Rl DepthConfig scene/ at exactly
// two files (depth_config.hpp + view.hpp) per the T4 gate.

namespace re::scene {
} // namespace re::scene
