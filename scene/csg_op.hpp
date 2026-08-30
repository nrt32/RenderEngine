#pragma once

// scene/csg_op.hpp — CSG operation tags for the flat Puxel CSG model (V7 T2).
//
// The V7 iteration implements GPU CSG via Approach C — a two-stage SSBO Puxel pipeline with no CPU boolean. The scene side therefore carries only semantic tags, never geometry: a CsgObject stores a base Operand plus two flat lists — subtractors (closed manifold meshes whose material drives the hole interior when subtracting, asymmetric Subtract) and paints (recolor surviving base fragments where inside(P), with paintInterior true meaning the entire volume interior of P recolors and false meaning only a surface strip of P recolors, plus a blend override factor). The tree form (A−B)∪C is expressed as two CsgObjects on the same Layer (layer-union is free via depth, Goldfeather SOP deferred), so the only operation tags needed at the value level are Subtract (remove base fragments inside the operand) and Paint (recolor, not remove). Keeping this as a tiny value header preserves the GL-free, RE-free disposition of scene/ — no render types, no GL, no mediation-layer dependency — and lets the scene value for each operand be turned into render handles
// via the per-operand hash (AssetId plus generation plus content hash plus operand transform plus material hash plus paint blend plus paint interior flag) that the later Puxel stage consumes. This header is additive for T2 and satisfies the T2 documentation-map row. (V7 T2)

namespace re::scene {

/// CSG operation tag — the flat multi-subtract/multi-paint model keeps only
/// two operations per TASKS.md V7 design: Subtract removes base fragments that
/// lie inside the operand (B's material drives the cap when emitted, hole
/// interior uses B's MeshMaterialDesc), Paint recolors surviving base fragments
/// where inside(P) (paintInterior true → entire interior volume, false → thin
/// surface strip adjacent to the operand surface, blend override controls the
/// mix). Separate CsgOitStage handles the GPU Puxel sort+classify; scene never
/// computes booleans on the CPU, only tags the operands for the broker to
/// upload.
enum class CsgOp : uint8_t {
    Subtract = 0,
    Paint = 1
};

} // namespace re::scene
