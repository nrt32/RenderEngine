#pragma once

// render/re_scene/csg_object.hpp — ReCsgObject RE-minimal handle for flat Puxel CSG (V7 T9, SPEC §12.4).
//
// This RE-minimal type carries only GPU-ready handles and uniform-ready transforms for the two-stage SSBO Puxel pipeline (Approach C, user binding 2026-08-30): Stage-1 capture via CsgOitStage (head R32UI plus counter plus node buffer 16B {colorU32, depth, facing, matId} padded, maxFpp clamped [1,16] default 8, nodeCapacity = w*h*maxFpp*16 bytes e.g. 640*480*8*16=39321600 37.5 MB well under the 152 MB reference 157286400, plus resolved SSBO) then csg_resolve.frag sort plus classify (flat A∩⋂B' plus paint recolor with Bback cap at facing -1) then final LinkedListOIT::endWithCsg k-way merge via over(). Scene side stays semantic: base Operand {AssetRef<Mesh> mesh, mat4 operandTransform, MeshMaterialDesc material} plus flat vectors of subtractors and PaintOperands {Operand oper; bool paintInterior; float blend;} where interior true means volume interior recolor and false means surface strip. Closed manifold only: Puxel classifier assumes watertight operands, non-manifold would misclassify. The RE type therefore stores AssetHandles for base, subHandles/subTransforms, paintHandles/paintTransforms/paintBlends/paintInteriorFlags, plus the object model mat4 (uniform-ready, object.transform * base.operandTransform) and worldBounds derived as model * localBounds of the base mesh (derived AABB for culling and Puxel bounds). No verbatim data::Mesh positions copy is stored — only handles, satisfying asset_indirection guardrail and the RE-minimal discipline. (V7 T9)

#include <vector>

#include <glm/mat4x4.hpp>

#include "data/aabb.hpp"
#include "render/asset_registry.hpp"

namespace re::render::re_scene {

using Aabb = data::Aabb;

/// RE-minimal CSG object for the GPU Puxel pipeline (V7 T9).
///
/// Mirrors scene::CsgObject flat multi-subtract/multi-paint (closed manifold,
/// B's material drives hole cap, paintInterior controls recolor) with only
/// RE-direct fields: handles, transforms, blends, interior flags, model,
/// worldBounds (derived). No raw mesh bytes, no scene desc verbatim.
struct ReCsgObject {
    AssetHandle baseHandle{};                      ///< handle — base mesh GPU handle via AssetRegistry
    std::vector<AssetHandle> subHandles{};         ///< handle — subtractor mesh handles (dedup by ContentHash)
    std::vector<glm::mat4> subTransforms{};        ///< uniform-ready — per-subtractor operandTransform preserved for GPU per-operand model
    std::vector<AssetHandle> paintHandles{};       ///< handle — paint mesh handles
    std::vector<glm::mat4> paintTransforms{};      ///< uniform-ready — per-paint operandTransform
    std::vector<float> paintBlends{};              ///< derived — per-paint blend override 0..1 (recolor mix factor)
    std::vector<bool> paintInteriorFlags{};        ///< derived — per-paint paintInterior true→volume interior, false→surface strip
    glm::mat4 model{1.0f};                         ///< uniform-ready — object.transform * base.operandTransform (uModel)
    Aabb bounds{};                                 ///< derived — world-space AABB = model * localBounds(base) (alias for worldBounds parity)
    Aabb worldBounds{};                            ///< derived — canonical world-space AABB = model * localBounds(base) (RE-minimal derived)
};

} // namespace re::render::re_scene

namespace re::render {
using ReCsgObject = re_scene::ReCsgObject;
}
