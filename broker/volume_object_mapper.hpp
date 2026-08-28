#pragma once

// broker/volume_object_mapper.hpp — VolumeObjectMapper (SPEC §11.3 per-type
// inventory `volume_object_mapper.*`): cached translation
//
//   scene::VolumeObject{dataset ref, transform, TF}
//     → render::VolumeInstance{dataset ref, TF value, model}
//
// This mapper is the fix for the review finding that the view synchronizer
// "silently drops volumes": a matched scene::VolumeObject used to be replaced
// by a locally-defined do-nothing IRenderable inside ViewSynchronizer::sync,
// so a bridged volume rendered nothing. The translation now produces a REAL
// render::VolumeInstance which the synchronizer wraps as a live ray-cast layer
// drawn by render::VolumeRenderer — never a no-op.
//
// One file per mapper (guardrail broker_per_type). Cached by object id +
// generation (a setTransform/setTransferFunction bump re-translates only that
// object; unchanged ids return the cached instance without touching the GPU
// asset store — the store itself dedups uploads by content hash, SPEC §7 T14).
//
// RE-minimal note (SPEC §12.4): the volume's tint/shading desc has no RE
// counterpart this iteration (the ray-cast consumes the transfer function and
// the model only); carrying unused fields across would be dead weight.
// `stepLength` likewise stays app-side: the renderer's analytic sampling step
// is the pinned FR-vol.3 constant the gates assert against.
//
// Typed errors (SPEC §5): code 1 = null dataset reference. No raw gl*
// (guardrail gpu_api_ownership).

#include <memory>

#include "broker/cached_mapper_base.hpp"
#include "render/asset_registry.hpp"
#include "render/types.hpp"
#include "render/volume_renderer.hpp" // render::VolumeInstance
#include "scene/object.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Volume object mapper — cached translation scene::VolumeObject ->
/// render::VolumeInstance (T7 owner-driven).
///
/// T7: registers the volume through the shared `AssetRegistry` at sync time
/// (hashed at load/register time, never per frame per data/content_hash.hpp:31)
/// and hands the renderer a `VolumeTextureHandle` instead of a shared_ptr;
/// the renderer resolves via O(1) handle (no per-frame FNV-1a). Volumes first,
/// then images (T7 staged).
class VolumeObjectMapper
    : public CachedMapperBase<scene::VolumeObject, render::VolumeInstance> {
   public:
    using AppType = scene::VolumeObject;
    using ReType = render::VolumeInstance;

    explicit VolumeObjectMapper(
        std::shared_ptr<render::AssetRegistry> registry =
            render::AssetRegistry::shared())
        : registry_(std::move(registry)) {}

    /// Pure translation: registers volume in AssetRegistry and carries TF/model.
    /// Typed errors: code 1 null dataset, code 2 null registry, code 3 register failed.
    data::Result<render::VolumeInstance> map(
        const scene::VolumeObject& app,
        const scene::TranslateContext& ctx) const override;

    const std::shared_ptr<render::AssetRegistry>& registry() const noexcept {
        return registry_;
    }

   private:
    std::shared_ptr<render::AssetRegistry> registry_;
};

} // namespace re::broker
