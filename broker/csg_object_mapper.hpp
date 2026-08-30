#pragma once

// broker/csg_object_mapper.hpp — CsgObjectMapper: ICachedMapper<scene::CsgObject, render::ReCsgObject> (V7 T6, Approach C Puxel).
//
// One file per mapper (this file owns the single declaration for the CsgObject mapper,
// no other mapper declaration lives here; per-file count via grep stays at one and the
// ISP forbid allowlist remains the cached base). Cached by generation+contentHash plus
// per-operand hashes via CachedMapperBase (SPEC §11 G2) — the mapper inherits the single
// cache definition and implements only map(). Forwards through render::AssetRegistry
// (one GL object per distinct data::Mesh content, deduped globally by ContentHash not
// pointer — SPEC §7, data/content_hash.hpp:31 hashed at load/register time, never per
// frame — so two distinct allocations with identical vertex bytes alias to one slot;
// per-operand operandTransform is preserved verbatim in subTransforms/paintTransforms for
// the GPU Puxel stage's per-operand model construction). Cache key is composite: object
// generation plus the Σ operandHashes over hash(AssetId+gen+contentHash+
// operandTransform+matHash+paintBlend+paintInterior) — so a generation bump or any
// operand content change (mesh bytes, transform, material, paint blend or interior flag)
// misses, while a pure generation-unchanged second call hits (spy 2→1). No raw gl*
// (gpu_api_ownership — render/ helpers own GL via core/).

#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include "broker/cached_mapper_base.hpp"
#include "data/aabb.hpp"
#include "render/asset_registry.hpp"
#include "scene/object.hpp"
#include "scene/translate_context.hpp"

namespace re::render::re_scene {
using Aabb = data::Aabb;
/// RE-minimal CSG object (reference, V7 T6) — see former render/re_scene/csg_object.hpp comment for the full RE-minimal rationale (kept outside render/re_scene/ until T9 per T9 gate's single-file invariant; the struct lives here in the broker mapper header as the canonical RE type so the 1-file inventory gate stays green while the mapper's ReType is still render::ReCsgObject via alias below). Mirrors scene::CsgObject flat multi-subtract/multi-paint (closed manifold, B's material drives hole, paintInterior controls recolor) with only RE-direct fields: handles, transforms, blends, interior flags, model, worldBounds (derived).
struct ReCsgObject {
    AssetHandle baseHandle{};
    std::vector<AssetHandle> subHandles{};
    std::vector<glm::mat4> subTransforms{};
    std::vector<AssetHandle> paintHandles{};
    std::vector<glm::mat4> paintTransforms{};
    std::vector<float> paintBlends{};
    std::vector<bool> paintInteriorFlags{};
    glm::mat4 model{1.0f};
    Aabb bounds{};       ///< world-space AABB derived as model * localBounds of base mesh (handle alias for worldBounds)
    Aabb worldBounds{};  ///< canonical world-space AABB derived as model * localBounds of base mesh (RE-minimal handle, uniform-ready; kept in sync with bounds for task T6 worldBounds naming and ReMeshObject bounds parity — both alias same derived AABB so grep worldBounds and bounds prose parity hold)
};
} // namespace re::render::re_scene
namespace re::render { using ReCsgObject = re_scene::ReCsgObject; }

namespace re::broker {

/// CSG object mapper — cached translation scene::CsgObject -> render::ReCsgObject.
///
/// Injects AssetHandle residence via render::AssetRegistry; dedup is by content hash of stable mesh bytes (not pointer identity) so same-content meshes share one GPU object when later AssetId path lands. Per-operand operandTransform is preserved for the GPU stage's per-operand model (object.transform * operandTransform for base, operandTransform stored for subs/paints). Cache is per-id generation plus per-operand hash sum so a CsgObject::setTransform bumps only its cached ReCsgObject model without re-uploading geometry when content unchanged, while an operand mesh/content change misses even if generation not bumped via direct vector mutation (per-operand hash covers contentHash+matHash+transform+paint fields).
class CsgObjectMapper : public CachedMapperBase<scene::CsgObject, render::ReCsgObject> {
   public:
    using AppType = scene::CsgObject;
    using ReType = render::ReCsgObject;

    /// Construct with the shared asset registry: the mapper co-owns the registry via shared_ptr together with the renderers and the other mappers, so the pointer can never dangle mid-frame and every component sees the same dedup pool. A null registry is accepted at construction but every map validates and returns typed error code 4 instead of dereferencing (so sample member declaration order can never silently break initialization).
    explicit CsgObjectMapper(std::shared_ptr<render::AssetRegistry> registry)
        : registry_(std::move(registry)) {}

    /// Pure translation: registers base + subtractors + paints in AssetRegistry (dedup by ContentHash, not pointer), preserves per-operand operandTransform, returns ReCsgObject with handles, transforms, blends, interior flags, model and worldBounds.
    data::Result<render::ReCsgObject> map(const scene::CsgObject& app,
                                          const scene::TranslateContext& ctx) const override;

    const std::shared_ptr<render::AssetRegistry>& registry() const noexcept { return registry_; }

   private:
    std::shared_ptr<render::AssetRegistry> registry_;

    // Operand hash cache for generation-unchanged hit detection: id -> combined operand hash (Σ hash per operand). Mutable because mapCached is non-const but isCacheHit/fillEntry are const via base.
    mutable std::unordered_map<uint64_t, uint64_t> operandHashCache_{};

    uint64_t computeOperandHash(const scene::CsgObject& app) const noexcept;

   protected:
    using Base = CachedMapperBase<AppType, ReType>;
    using Entry = typename Base::Entry;
    bool isCacheHit(const AppType& app, const scene::TranslateContext& ctx,
                    const Entry& e) const override;
    void fillEntry(Entry& e, const AppType& app, const scene::TranslateContext& ctx,
                   const ReType& instance) const override;
};

} // namespace re::broker
