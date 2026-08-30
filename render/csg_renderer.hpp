#pragma once

// render/csg_renderer.hpp — CsgRenderer: stateless IRenderable for flat Puxel CSG (V7 T3).
//
// The V7 design (TASKS.md V7 T3) introduces GPU CSG via Approach C: a two-stage SSBO Puxel pipeline where the first stage captures fragments with imageAtomicExchange into a head R32UI texture plus a counter and a node buffer containing {colorU32, depth, facing, matId} 16B padded, the second stage running csg_resolve.frag which gathers each pixel's list, insertion-sorts near->far, classifies with the flat A∩⋂B' plus paint-recolor rule (subW union, baseW visibility, paintW recolor, Bback facing -1 cap emission carrying B's material, paintInterior bool selecting volume interior versus surface strip with blend override), writing survivors linear per-pixel sorted into resolvedBuffer plus counts into resolvedCount for the final LinkedListOIT::endWithCsg k-way merge. CsgRenderer is the stateless per-draw dispatcher that owns only shared handles to the AssetRegistry and the CsgOitStage (injected at construction, validated per draw with typed error code 4 if null, so sample member declaration order can never dangle), and its drawCsg(base, subtractors, paints) method iterates the operands, installs the CsgOitStage capture program, sets the per-operand uniforms (model, view, proj, packed colorU32, matId, capacity), disables culling so both front and back faces are emitted with facing ±1 via gl_FrontFacing, and draws each operand's MeshGeometry through the shared registry (RE-minimal handle, not CPU mesh bytes). The renderer is intentionally stateless (no per-frame cache) so the stage's begin/resolve owns the only mutable GPU state, keeping the single-writer discipline via REContext and the gpu_api_ownership guardrail (render/ never calls raw gl*). (V7 T3)

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "core/re_context.hpp"
#include "data/result.hpp"
#include "render/asset_registry.hpp"
#include "render/csg_stage.hpp"
#include "render/i_renderable.hpp"
#include "render/types.hpp"

namespace re::render {

/// Per-operand draw description for CSG (CPU side, RE-minimal handle).
///
/// The mesh is referenced by its AssetHandle into the shared AssetRegistry (the RE-minimal
/// currency, not raw data::Mesh bytes), the transform is the operand-local matrix multiplied
/// with the object's world transform, and the material is encoded as a packed RGBA8 uint
/// plus a matId that the resolve shader uses for classification (base 0, subtractors 1..N,
/// paints after). The registry resolves the handle to MeshGeometry at draw time, so one GPU
/// object is shared globally across views and renderers.
struct CsgDrawOperand {
    AssetHandle handle{};
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    std::uint32_t matId{0u};
};

/// Paint operand for CSG recolor: the operand plus interior/surface selection and blend.
///
/// When `paintInterior` is true, every surviving base fragment whose screen column is inside
/// the paint operand's solid (between its front and back faces) is recolored with `blend`
/// mixing toward the paint operand's color; when false, only a thin surface strip adjacent
/// to the paint operand's surface is recolored. The resolve shader implements this via the
/// same inside/outside counters used for subtraction (subW union, paintW recolor).
struct CsgPaintOperandDraw {
    CsgDrawOperand operand{};
    bool paintInterior{true};
    float blend{1.0f};
};

/// Stateless CSG dispatcher: captures CSG operands into a CsgOitStage via imageAtomicExchange.
///
/// Holds only shared handles to the AssetRegistry and the CsgOitStage (both injected at
/// construction, validated per draw with typed error code 4 if null). The `drawCsg` method
/// is the only entry point for the V7 T3 gate: it draws the base operand plus each
/// subtractor and paint operand into the stage's SSBO linked list with both front and back
/// faces (facing ±1 via gl_FrontFacing) using the stage's capture program. The stage's
/// `resolve` is called separately by the caller after all draws. The renderer also
/// satisfies `IRenderable` for future View integration (broker will wrap it), but the
/// type-erased `drawLayer` is a no-op in the headless T3 gate and the real path is
/// `drawCsg` (the task's explicit API). Render/ never calls raw GL; every GL call flows
/// through core/ wrappers and the stage's program cache (guardrail gpu_api_ownership).
class CsgRenderer final : public IRenderable {
   public:
    /// Construct with the shared asset registry and the shared CSG stage. Both are
    /// co-owned (shared_ptr) so declaration order in samples can never dangle; a null
    /// registry or stage is accepted at construction but every draw validates and
    /// returns a typed error code 4 instead of dereferencing.
    explicit CsgRenderer(std::shared_ptr<AssetRegistry> registry,
                         std::shared_ptr<CsgOitStage> stage);

    CsgRenderer(const CsgRenderer&) = delete;
    CsgRenderer& operator=(const CsgRenderer&) = delete;
    CsgRenderer(CsgRenderer&&) noexcept = default;
    CsgRenderer& operator=(CsgRenderer&&) noexcept = default;
    ~CsgRenderer() final = default;

    /// Draw the flat CSG operands into the stage's capture buffers. The base operand
    /// defines the primary solid A, each subtractor defines a B solid whose interior
    /// is subtracted (union of subtractors, Bback facing -1 cap emission with B's
    /// material), and each paint operand recolors surviving base fragments where
    /// inside(P) (paintInterior true → volume interior, false → surface strip,
    /// blend override). Both front and back faces are appended with facing ±1 via
    /// `gl_FrontFacing` (cull disabled) so the resolve stage can reconstruct solid
    /// intervals. Returns a typed error if the registry or stage is null, if any
    /// handle is stale, or if the capture program cannot be built.
    data::Result<void> drawCsg(const CsgDrawOperand& base,
                               const std::vector<CsgDrawOperand>& subtractors,
                               const std::vector<CsgPaintOperandDraw>& paints,
                               const Camera& camera);

    /// IRenderable no-op for the type-erased View path (broker wraps drawCsg via a
    /// captured lambda; the direct drawLayer is not used in the T3 headless gate).
    data::Result<void> drawLayer(const Camera& camera) override;

    const std::shared_ptr<AssetRegistry>& registry() const noexcept { return registry_; }
    const std::shared_ptr<CsgOitStage>& stage() const noexcept { return stage_; }

   private:
    std::shared_ptr<AssetRegistry> registry_;
    std::shared_ptr<CsgOitStage> stage_;
};

} // namespace re::render
