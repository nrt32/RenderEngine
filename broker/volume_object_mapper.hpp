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

#include <unordered_map>

#include "broker/i_mapper.hpp"
#include "render/types.hpp"
#include "render/volume_renderer.hpp" // render::VolumeInstance
#include "scene/object.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Volume object mapper — cached translation scene::VolumeObject ->
/// render::VolumeInstance.
class VolumeObjectMapper
    : public ICachedMapper<scene::VolumeObject, render::VolumeInstance> {
   public:
    using AppType = scene::VolumeObject;
    using ReType = render::VolumeInstance;

    /// Pure translation: carries the shared dataset reference and the TF
    /// value across; the transform becomes the instance model. Typed errors:
    /// code 1 null dataset reference.
    data::Result<render::VolumeInstance> map(
        const scene::VolumeObject& app,
        const scene::TranslateContext& ctx) const override;

    /// Cached translation: short-circuits when generation unchanged for id.
    data::Result<render::VolumeInstance> mapCached(
        const scene::VolumeObject& app,
        const scene::TranslateContext& ctx) override;

    /// Invalidate cached entry for the given object id.
    void invalidate(uint64_t id) override;

   private:
    struct Entry {
        uint64_t generation{0};
        render::VolumeInstance instance{};
    };
    std::unordered_map<uint64_t, Entry> cache_;
};

} // namespace re::broker
