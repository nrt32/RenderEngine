#pragma once

// scene/objects/csg_object.hpp — CsgObject concrete kind for flat Puxel CSG (V7 T2).
//
// The V7 iteration introduces GPU CSG via Approach C — a two-stage SSBO Puxel pipeline (capture with imageAtomicExchange into a head R32UI + counter + node buffer {colorU32, depth, facing, matId} 16B padded, then csg_resolve.frag sort+classify per pixel writing survivors into a linear csgResolved SSBO + counts, then final LinkedListOIT::endWithCsg k-way merge via over()). The scene side stays purely semantic: it describes boolean intent, never computes booleans on the CPU. Closed manifold meshes only are allowed (the Puxel classifier assumes watertight operands, so a non-manifold hole would misclassify inside/outside). The base operand is an Operand {AssetRef<Mesh> mesh, mat4 operandTransform, MeshMaterialDesc material} carrying its own transform (relative to the object's world transform) and its Phong material; the subtractors vector holds additional Operands to subtract (multi-subtract flat — tree (A−B)∪C is expressed as two CsgObjects on the same Layer, layer-union is free via depth, Goldfeather SOP deferred, so no recursive Node tree is needed at T2). Hole interior uses B's material (asymmetric Subtract: the cap fragment emitted at the B back-face facing −1 carries B's cap material, not A's). The paints vector holds PaintOperands {Operand oper; bool paintInterior; float blend;} that recolor surviving base fragments where inside(P) — paintInterior true means the entire volume interior of P recolors (so every base fragment classified as inside the paint operand is tinted), false means only a thin surface strip adjacent to P's surface recolors, and blend overrides the mix factor (1.0 opaque recolor, <1 blend with base). Scene/ never includes render/ — this header stays GL-free/RE-free and only depends on data/mesh + material_desc + iscene_object, preserving disposition_scene and gpu_api_ownership guardrails. (V7 T2)

#include <vector>

#include <glm/mat4x4.hpp>

#include "data/mesh.hpp"
#include "scene/csg_op.hpp"
#include "scene/iscene_object.hpp"
#include "scene/layer.hpp"
#include "scene/material_desc.hpp"

namespace re::scene {

/// Operand for CSG — a closed manifold mesh operand with its own transform and material.
///
/// The mesh is co-owned via AssetRef (shared_ptr<const Mesh>) so CPU bytes stay alive while any scene object, store, or RE instance refers to them (T13 AssetRef discipline). operandTransform is the operand-local matrix multiplied with the object's world transform (so each B/C operand can be placed independently of the base A without requiring a separate scene object). material carries the PhongDesc that drives the hole cap (when this operand is a subtractor, its back-face cap fragments use this material) or the paint color (when used as a PaintOperand).
struct CsgOperand {
    AssetRef<data::Mesh> mesh{};
    glm::mat4 operandTransform{1.0f};
    MeshMaterialDesc material{};

    bool operator==(const CsgOperand& o) const noexcept {
        return mesh == o.mesh && operandTransform == o.operandTransform &&
               material.phong.baseColor == o.material.phong.baseColor &&
               material.phong.specular == o.material.phong.specular &&
               material.phong.shininess == o.material.phong.shininess &&
               material.phong.doubleSided == o.material.phong.doubleSided;
    }
    bool operator!=(const CsgOperand& o) const noexcept { return !(*this == o); }
};

/// Paint operand — recolors surviving base fragments where inside(oper).
///
/// paintInterior true → volume interior recolor (every surviving base fragment
/// classified as inside the paint mesh is recolored); false → surface strip
/// only (thin band adjacent to the operand surface recolors, volume interior
/// behind that strip keeps base color). blend is the override mix factor (0..1,
/// 1 = full paint color, 0 = keep base). The broker hashes blend+paintInterior
/// together with the operand's AssetId/gen/contentHash/operandTransform/matHash
/// for its cache key.
struct CsgPaintOperand {
    CsgOperand oper{};
    bool paintInterior{true};
    float blend{1.0f};

    bool operator==(const CsgPaintOperand& o) const noexcept {
        return oper == o.oper && paintInterior == o.paintInterior && blend == o.blend;
    }
    bool operator!=(const CsgPaintOperand& o) const noexcept { return !(*this == o); }
};

/// CsgObject — flat multi-subtract/multi-paint CSG object (V7 T2, closed manifold, tree deferred).
///
/// Derives from ObjectBase<CsgObject> so the shared ObjectHeader {id, transform, generation, setTransform} and layer/priority generation bumping are delegated via CRTP, ensuring future slab allocation can move the header without touching each concrete header. Kind is SceneKind::Csg (6). The object carries a base operand plus flat vectors of subtractors and paints — no recursive tree (tree (A−B)∪C is two CsgObjects on the same Layer, layer-union free via depth per TASKS.md V7 design, Goldfeather SOP deferred). Closed manifold only: the GPU Puxel classifier depends on watertight winding; non-manifold inputs are undefined. B's material drives the hole cap (asymmetric Subtract); paintInterior true → volume interior recolor, false → surface strip, blend override mixes. All fields are plain values, GL/RE-free, so scene/ disposition stays intact. (V7 T2)
class CsgObject : public ObjectBase<CsgObject> {
   public:
    static constexpr SceneKind Kind = SceneKind::Csg;

    CsgObject() = default;
    CsgObject(const CsgObject&) = default;
    CsgObject(CsgObject&&) noexcept = default;
    CsgObject& operator=(const CsgObject&) = default;
    CsgObject& operator=(CsgObject&&) noexcept = default;
    ~CsgObject() override = default;

    ObjectId id{0};
    glm::mat4 transform{1.0f};
    Layer layer{Layer::LAYER_0};
    int32_t priority{0};
    uint64_t generation{0};

    CsgOperand base{};
    std::vector<CsgOperand> subtractors{};
    std::vector<CsgPaintOperand> paints{};

    void setBase(CsgOperand b) noexcept {
        base = std::move(b);
        ++generation;
    }
    void setSubtractors(std::vector<CsgOperand> s) noexcept {
        subtractors = std::move(s);
        ++generation;
    }
    void setPaints(std::vector<CsgPaintOperand> p) noexcept {
        paints = std::move(p);
        ++generation;
    }
    void addSubtractor(CsgOperand op) noexcept {
        subtractors.push_back(std::move(op));
        ++generation;
    }
    void addPaint(CsgPaintOperand p) noexcept {
        paints.push_back(std::move(p));
        ++generation;
    }

    bool operator==(const CsgObject& o) const noexcept {
        if (id != o.id || transform != o.transform || layer != o.layer || priority != o.priority ||
            generation != o.generation)
            return false;
        if (!(base == o.base)) return false;
        if (subtractors != o.subtractors) return false;
        if (paints != o.paints) return false;
        return true;
    }
    bool operator!=(const CsgObject& o) const noexcept { return !(*this == o); }
};

} // namespace re::scene

REGISTER_SCENE_OBJECT(CsgObject)
