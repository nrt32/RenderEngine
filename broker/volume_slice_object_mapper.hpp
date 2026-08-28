#pragma once

// broker/volume_slice_object_mapper.hpp — VolumeSliceObjectMapper (SPEC §11.3
// per-type inventory `volume_slice_object_mapper.*`): cached translation
//
//   scene::VolumeSliceObject{dataset ref, transform, TF}
//     + view plane (TranslateContext::view.viewPlane — §11.4: the VIEW owns
//       the plane, the item does not carry one)
//     → render::VolumeSliceInstance{dataset ref, TF value, model, world plane}
//
// The mapped instance is drawn by render::VolumeSliceRenderer: the GPU
// extraction samples the cached R32F texture exactly where each pixel ray
// crosses the plane (the T16 capability), so a bridged volume-slice layer is a
// REAL interactive GPU slice draw — there is no CPU slice image and no no-op
// placeholder anywhere on this path.
//
// Contextual mapping (SPEC §11.4): the plane comes from the view by value and
// is converted to world space through broker::convertViewPlaneToClipPlane (the
// ONE PlaneMapper conversion rule) using a VolumeContext derived from THE
// OBJECT ITSELF {volumeModel = its transform, dims = its dataset dims,
// unit voxel spacing} — so two slice views of the same dataset with different
// display-frame transforms each convert correctly without sharing state.
// A missing view plane is a typed error, never a silently skipped layer (a
// missing slice is visually indistinguishable from an empty viewport).
//
// Cache: keyed by object id + generation + PLANE IDENTITY (the plane rides in
// through the context, not through the object, so a scroll that only bumps the
// view's planeGen must re-translate even though the object generation did not
// move). One file per mapper (guardrail broker_per_type).
//
// Typed errors (SPEC §5): code 1 = null dataset reference; code 2 = context
// carries no view plane (a volume slice without a plane has no geometry);
// code 3 = the VoxelIndex → world conversion failed (propagated codes from
// PlaneMapper's rule). No raw gl* (guardrail gpu_api_ownership).

#include <memory>
#include <optional>

#include "broker/cached_mapper_base.hpp"
#include "render/asset_registry.hpp"
#include "render/types.hpp"
#include "render/volume_slice_renderer.hpp" // render::VolumeSliceInstance
#include "scene/object.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Volume slice object mapper — cached translation scene::VolumeSliceObject
/// (+ view plane) -> render::VolumeSliceInstance (T7 owner-driven).
class VolumeSliceObjectMapper
    : public CachedMapperBase<scene::VolumeSliceObject, render::VolumeSliceInstance> {
   public:
    using AppType = scene::VolumeSliceObject;
    using ReType = render::VolumeSliceInstance;

    explicit VolumeSliceObjectMapper(
        std::shared_ptr<render::AssetRegistry> registry =
            render::AssetRegistry::shared())
        : registry_(std::move(registry)) {}

    /// Pure translation (see header for the plane contract and error codes).
    /// Registers volume via AssetRegistry (T7, no per-frame hash) and produces handle.
    data::Result<render::VolumeSliceInstance> map(
        const scene::VolumeSliceObject& app,
        const scene::TranslateContext& ctx) const override;

    const std::shared_ptr<render::AssetRegistry>& registry() const noexcept {
        return registry_;
    }

   private:
    std::shared_ptr<render::AssetRegistry> registry_;

   protected:
    using Base = CachedMapperBase<AppType, ReType>;
    using Entry = typename Base::Entry;
    bool isCacheHit(const AppType& app, const scene::TranslateContext& ctx,
                    const Entry& e) const override;
    void fillEntry(Entry& e, const AppType& app,
                   const scene::TranslateContext& ctx,
                   const ReType& instance) const override;
};

} // namespace re::broker
