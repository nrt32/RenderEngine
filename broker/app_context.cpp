// broker/app_context.cpp — AppContext: builds the full broker wiring (see
// header for the composition-root contract and the Params semantics).

#include "broker/app_context.hpp"

#include "broker/camera_mapper.hpp"
#include "broker/contour_mapper.hpp"
#include "broker/csg_object_mapper.hpp"
#include "broker/line_object_mapper.hpp"
#include "broker/material_mapper.hpp"
#include "broker/mesh_object_mapper.hpp"
#include "broker/mesh_slice_object_mapper.hpp"
#include "broker/plane_mapper.hpp"
#include "broker/plane_object_mapper.hpp"
#include "broker/csg_tree.hpp"
#include "broker/point_cloud_mapper.hpp"
#include "broker/point_object_mapper.hpp"
#include "broker/view_bridge.hpp"
#include "broker/view_compositor.hpp"
#include "broker/volume_object_mapper.hpp"
#include "broker/volume_slice_object_mapper.hpp"

namespace re::broker {

AppContext::AppContext(Params params) {
    // One store instance shared by every renderer and mapper in this context.
    assets_ = std::make_shared<render::AssetRegistry>();
    stack_ = RenderStack::create(assets_, params.enableOIT);

    broker_ = std::make_shared<Broker>();
    // The presentation mapper is registered once and SHARED by the mesh and
    // mesh-slice object mappers, so both paths dedup into one canonical set.
    auto materials = std::make_shared<MaterialMapper>(assets_);
    if (params.registerCameraMapper) {
        broker_->registerMapper(std::make_unique<CameraMapper>());
    }
    broker_->registerMapper(
        std::make_unique<MeshObjectMapper>(assets_, materials));
    broker_->registerMapper(
        std::make_unique<MeshSliceObjectMapper>(assets_, materials));
    broker_->registerMapper(std::make_unique<VolumeObjectMapper>(assets_));
    broker_->registerMapper(std::make_unique<VolumeSliceObjectMapper>(assets_));
    broker_->registerMapper(std::make_unique<PlaneMapper>());
    broker_->registerMapper(std::make_unique<PlaneObjectMapper>(assets_));
    broker_->registerMapper(std::make_unique<ContourMapper>(assets_));
    if (params.enableCsg) {
        broker_->registerMapper(std::make_unique<CsgObjectMapper>(assets_));
        broker_->registerMapper(std::make_unique<CsgTreeObjectMapper>(assets_));
    }
    if (params.enablePoints) {
        broker_->registerMapper(std::make_unique<PointObjectMapper>());
        broker_->registerMapper(std::make_unique<PointCloudMapper>());
    }
    if (params.enableLines) {
        broker_->registerMapper(std::make_unique<LineObjectMapper>());
    }

    bridge_ = ViewBridge::create(broker_, stack_);
}

AppContext::~AppContext() = default;

ViewCompositor* /*borrow*/ AppContext::compositor() noexcept {
    return bridge_ ? bridge_->compositor() : nullptr;
}

} // namespace re::broker
