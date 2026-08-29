#pragma once
// scene/field_id.hpp — per-field dirty tracking identifiers (T5 adds Priority=12 for dumb layers + scoped priority ordering).
//
// Each scene field that can dirty a cache carries its own FieldId so the synchronizer's per-field generation split can short-circuit when that field's generation is unchanged. The bounded dirty log (one slot per FieldId, raised in place) answers dirtyFieldsSince(lastGen) in O(#FieldIds). T5 adds Priority=12 alongside Layer=11 so a per-object priority bump dirties only the Layer field bucket (scoped priority inside same layer+type) without forcing a full material or plane re-translate — the store's bump(FieldId::Priority) plus the object's setPriority() keep the cache coherent via generation.
#include <cstdint>
namespace re::scene {
enum class FieldId : uint8_t {
    Rect = 0,
    Plane = 1,
    CameraView = 2,
    CameraProj = 3,
    Items = 4,
    Transform = 5,
    Material = 6,
    TransferFunction = 7,
    ClearColor = 8,
    DepthTest = 9,
    Lights = 10,
    Layer = 11,
    Priority = 12,
};
}
