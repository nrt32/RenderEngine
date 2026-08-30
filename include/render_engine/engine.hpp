#pragma once

// include/render_engine/engine.hpp — viz::Engine facade for the 80% visualization case (SPEC §3, TASKS T1).
//
// The 80% visualization use case — loading a mesh or volume and drawing it into a
// framebuffer or window — previously required manual wiring of SceneStore plus
// Broker plus AppContext plus View plus camera framing plus the per-view item list.
// Each sample repeated four steps (load → shared_ptr → object → add) and the
// fitPerspectiveViewToPixels plus Rect plus Camera ceremony. Engine collapses that
// into one object that owns its AppContext (and the AppContext's SceneStore) and
// exposes a minimal typed API: addMesh / addVolume return a stable ObjectId,
// setView installs one or more views from a plain descriptor, render(Framebuffer&)
// drives the broker façade sync → renderAll → presentAll, and appContext()/store()
// give advanced users full access to the broker path. A single-site helper
// Engine::createView hides the Rect plus Camera plus framing ceremony that every
// sample repeated. Persistence internals (content hash, layout keys) stay inside
// the broker and are not part of this header.

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "broker/app_context.hpp"
#include "core/framebuffer.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "scene/camera.hpp"
#include "scene/csg_op.hpp"
#include "scene/depth_config.hpp"
#include "scene/light.hpp"
#include "scene/line_style.hpp"
#include "scene/material_desc.hpp"
#include "scene/object.hpp"
#include "scene/objects/csg_object.hpp"
#include "scene/objects/line_object.hpp"
#include "scene/objects/point_cloud_object.hpp"
#include "scene/objects/point_object.hpp"
#include "scene/point_fill.hpp"
#include "scene/store.hpp"
#include "scene/view.hpp"
#include "volume/transfer_function.hpp"

namespace re::viz {

/// Plain descriptor for Engine::setView — mirrors View's three user-facing fields
/// (rect, camera, object ids) in declaration order so aggregate initialization
/// matches the gate example `{{0,0,800,600}, cam, {id}}`. Clear color stays the
/// engine default unless the caller uses the View overload directly.
struct ViewDescriptor {
    ::re::scene::Rect rect{0, 0, 800, 600};
    ::re::scene::Camera camera{};
    std::vector<::re::scene::ObjectId> objectIds{};
};

/// Descriptor for a flat Puxel CSG object — base plus subtractors and paints.
///
/// The V7 CSG model is flat multi-subtract/multi-paint per CsgObject: base is the
/// primary operand whose surviving fragments are kept, subtractors remove interior
/// fragments, paints recolor. The descriptor mirrors CsgObject fields but stays a
/// plain value for the facade; the engine builds a CsgObject and delegates to
/// SceneStore::addCsgObject which assigns a stable ObjectId and bumps generation.
struct CsgDesc {
    ::re::scene::AssetRef<::re::data::Mesh> baseMesh{};
    glm::mat4 baseTransform{1.0f};
    ::re::scene::MeshMaterialDesc baseMaterial{};
    std::vector<::re::scene::CsgOperand> subtractors{};
    std::vector<::re::scene::CsgPaintOperand> paints{};
    glm::mat4 transform{1.0f};
    ::re::scene::Layer layer{::re::scene::Layer::LAYER_0};
    int32_t priority{0};
};

/// Descriptor for a single point marker.
struct PointDesc {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    float radius{5.0f};
    bool worldUnits{true};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    ::re::scene::PointFill fill{::re::scene::PointFill::Solid};
    float fillParam{0.0f};
    glm::mat4 transform{1.0f};
    ::re::scene::Layer layer{::re::scene::Layer::LAYER_0};
    int32_t priority{0};
};

/// Descriptor for a batched point cloud.
struct PointCloudDesc {
    std::vector<::re::scene::PointData> points{};
    bool worldUnits{true};
    glm::mat4 transform{1.0f};
    ::re::scene::Layer layer{::re::scene::Layer::LAYER_0};
    int32_t priority{0};
};

/// Descriptor for a polyline / line collection.
struct LineDesc {
    std::vector<::re::scene::LineSegment> segments{};
    glm::vec4 color{1.0f, 0.0f, 0.0f, 1.0f};
    float width{2.0f};
    bool worldUnits{false};
    ::re::scene::LineCap cap{::re::scene::LineCap::Square};
    ::re::scene::LineJoin join{::re::scene::LineJoin::Miter};
    float miterLimit{4.0f};
    ::re::scene::LineStyle style{::re::scene::LineStyle::Solid};
    ::re::scene::DashPattern dash{};
    glm::mat4 transform{1.0f};
    ::re::scene::Layer layer{::re::scene::Layer::LAYER_0};
    int32_t priority{0};
};

/// Engine — one-liner facade for visualization consumers (TASKS T1, SPEC §3).
///
/// Owns an AppContext (which owns the SceneStore, the Broker, the full mapper
/// inventory and the ViewBridge façade) and a current view list. The 80% path never
/// touches Broker or SceneStore directly: addMesh / addVolume load the asset,
/// create the scene object and return a stable ObjectId; setView installs the
/// views; render drives the bridge. Advanced users can reach the full broker path
/// via appContext() / store(). The helper createView centralizes the
/// Rect + Camera + aspect ceremony that was previously spread across every sample
/// via fitPerspectiveViewToPixels.
class Engine {
   public:
    /// Default wiring — same params as AppContext (OIT off, camera mapper on).
    Engine() : ctx_(::re::broker::AppContext::Params{}) {}

    /// Explicit wiring for callers that need OIT or a different mapper set.
    explicit Engine(const ::re::broker::AppContext::Params& params)
        : ctx_(params) {}

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept = default;
    Engine& operator=(Engine&&) noexcept = default;
    ~Engine() = default;

    // ---- asset helpers -------------------------------------------------------

    /// Add a mesh asset already loaded via `utils::loadMeshAsset` to the store.
    ///
    /// The 4-step ceremony (`load → shared_ptr → register → add`) now lives in
    /// `utils/` (`utils::loadMeshAsset` → `store.registerMeshAsset` → `addMesh`);
    /// this facade owns only the final `addMeshObject` delegation and never
    /// touches the filesystem or `io/` loaders. Returns the stable `ObjectId`
    /// minted by the store; load failures are reported by `utils::loadMeshAsset`
    /// before this call, so no typed `Result` is needed here (SPEC §5, T1/T2).
    ::re::scene::ObjectId addMesh(
        ::re::scene::AssetRef<::re::data::Mesh> asset,
        const glm::mat4& transform,
        const ::re::scene::MeshMaterialDesc& material) {
        ::re::scene::MeshObject obj;
        obj.mesh = std::move(asset);
        obj.transform = transform;
        obj.presentation = material;
        return ctx_.store().addMeshObject(std::move(obj));
    }

    /// Convenience: mesh with identity transform and white material.
    ::re::scene::ObjectId addMesh(
        ::re::scene::AssetRef<::re::data::Mesh> asset) {
        return addMesh(std::move(asset), glm::mat4(1.0f),
                       ::re::scene::MeshMaterialDesc{});
    }

    /// Convenience: mesh with explicit transform, white material.
    ::re::scene::ObjectId addMesh(
        ::re::scene::AssetRef<::re::data::Mesh> asset,
        const glm::mat4& transform) {
        return addMesh(std::move(asset), transform,
                       ::re::scene::MeshMaterialDesc{});
    }

    /// Convenience: mesh with material and identity transform.
    ::re::scene::ObjectId addMesh(
        ::re::scene::AssetRef<::re::data::Mesh> asset,
        const ::re::scene::MeshMaterialDesc& material) {
        return addMesh(std::move(asset), glm::mat4(1.0f), material);
    }

    /// Convenience: mesh baseColor directly (Phong opaque path).
    ::re::scene::ObjectId addMesh(
        ::re::scene::AssetRef<::re::data::Mesh> asset,
        const glm::mat4& transform, const glm::vec4& baseColor) {
        ::re::scene::MeshMaterialDesc mat;
        mat.phong.baseColor = baseColor;
        return addMesh(std::move(asset), transform, mat);
    }

    /// Add a volume asset already loaded via `utils::loadVolumeAsset`.
    ::re::scene::ObjectId addVolume(
        ::re::scene::AssetRef<::re::data::VolumeDataset> asset,
        const glm::mat4& transform,
        const ::re::volume::TransferFunction& tf) {
        ::re::scene::VolumeObject obj;
        obj.volume = std::move(asset);
        obj.transform = transform;
        obj.transferFunction = tf;
        return ctx_.store().addVolumeObject(std::move(obj));
    }

    ::re::scene::ObjectId addVolume(
        ::re::scene::AssetRef<::re::data::VolumeDataset> asset) {
        ::re::volume::TransferFunction tf(
            {{{0.0f, ::re::volume::RgbaColor{0, 0, 0, 0}},
              {1.0f, ::re::volume::RgbaColor{1, 1, 1, 1}}}});
        return addVolume(std::move(asset), glm::mat4(1.0f), tf);
    }

    ::re::scene::ObjectId addVolume(
        ::re::scene::AssetRef<::re::data::VolumeDataset> asset,
        const glm::mat4& transform) {
        ::re::volume::TransferFunction tf(
            {{{0.0f, ::re::volume::RgbaColor{0, 0, 0, 0}},
              {1.0f, ::re::volume::RgbaColor{1, 1, 1, 1}}}});
        return addVolume(std::move(asset), transform, tf);
    }

    ::re::scene::ObjectId addVolume(
        ::re::scene::AssetRef<::re::data::VolumeDataset> asset,
        const ::re::volume::TransferFunction& tf) {
        return addVolume(std::move(asset), glm::mat4(1.0f), tf);
    }

    // ---- CSG helpers ---------------------------------------------------------

    /// Add a flat Puxel CSG object via descriptor — delegates to SceneStore::addObject.
    ::re::scene::ObjectId addCsg(const CsgDesc& desc) {
        ::re::scene::CsgObject obj;
        obj.transform = desc.transform;
        obj.layer = desc.layer;
        obj.priority = desc.priority;
        ::re::scene::CsgOperand base;
        base.mesh = desc.baseMesh;
        base.operandTransform = desc.baseTransform;
        base.material = desc.baseMaterial;
        obj.base = std::move(base);
        obj.subtractors = desc.subtractors;
        obj.paints = desc.paints;
        return ctx_.store().addCsgObject(std::move(obj));
    }

    /// Convenience: base mesh plus subtractors and paints with identity transforms.
    ::re::scene::ObjectId addCsg(
        ::re::scene::AssetRef<::re::data::Mesh> base,
        const std::vector<::re::scene::CsgOperand>& subtractors,
        const std::vector<::re::scene::CsgPaintOperand>& paints = {}) {
        CsgDesc d;
        d.baseMesh = std::move(base);
        d.subtractors = subtractors;
        d.paints = paints;
        return addCsg(d);
    }

    /// Overload taking a fully formed CsgObject value (advanced).
    ::re::scene::ObjectId addCsg(::re::scene::CsgObject obj) {
        return ctx_.store().addCsgObject(std::move(obj));
    }

    // ---- point helpers -------------------------------------------------------

    /// Add a single point marker via descriptor.
    ::re::scene::ObjectId addPoint(const PointDesc& desc) {
        ::re::scene::PointObject obj;
        obj.position = desc.position;
        obj.radius = desc.radius;
        obj.worldUnits = desc.worldUnits;
        obj.color = desc.color;
        obj.fill = desc.fill;
        obj.fillParam = desc.fillParam;
        obj.transform = desc.transform;
        obj.layer = desc.layer;
        obj.priority = desc.priority;
        return ctx_.store().addPointObject(std::move(obj));
    }

    /// Convenience: position + radius + color.
    ::re::scene::ObjectId addPoint(const glm::vec3& pos, float radius,
                                   const glm::vec4& color, bool worldUnits = true,
                                   ::re::scene::PointFill fill = ::re::scene::PointFill::Solid) {
        PointDesc d;
        d.position = pos;
        d.radius = radius;
        d.color = color;
        d.worldUnits = worldUnits;
        d.fill = fill;
        return addPoint(d);
    }

    /// Add a batched point cloud via descriptor.
    ::re::scene::ObjectId addPointCloud(const PointCloudDesc& desc) {
        ::re::scene::PointCloudObject obj;
        obj.points = desc.points;
        obj.worldUnits = desc.worldUnits;
        obj.transform = desc.transform;
        obj.layer = desc.layer;
        obj.priority = desc.priority;
        return ctx_.store().addPointCloudObject(std::move(obj));
    }

    /// Convenience: vector<PointData> plus shared worldUnits flag.
    ::re::scene::ObjectId addPointCloud(
        const std::vector<::re::scene::PointData>& points, bool worldUnits = true) {
        PointCloudDesc d;
        d.points = points;
        d.worldUnits = worldUnits;
        return addPointCloud(d);
    }

    /// Overload taking a fully formed PointObject value (advanced).
    ::re::scene::ObjectId addPoint(::re::scene::PointObject obj) {
        return ctx_.store().addPointObject(std::move(obj));
    }

    /// Overload taking a fully formed PointCloudObject value (advanced).
    ::re::scene::ObjectId addPointCloud(::re::scene::PointCloudObject obj) {
        return ctx_.store().addPointCloudObject(std::move(obj));
    }

    // ---- line helpers --------------------------------------------------------

    /// Add a line / polyline via descriptor — delegates to SceneStore::addLineObject.
    ::re::scene::ObjectId addLine(const LineDesc& desc) {
        ::re::scene::LineObject obj;
        obj.segments = desc.segments;
        obj.color = desc.color;
        obj.width = desc.width;
        obj.worldUnits = desc.worldUnits;
        obj.cap = desc.cap;
        obj.join = desc.join;
        obj.miterLimit = desc.miterLimit;
        obj.style = desc.style;
        obj.dash = desc.dash;
        obj.transform = desc.transform;
        obj.layer = desc.layer;
        obj.priority = desc.priority;
        return ctx_.store().addLineObject(std::move(obj));
    }

    /// Alias for polyline — same descriptor, same storage.
    ::re::scene::ObjectId addPolyline(const LineDesc& desc) {
        return addLine(desc);
    }

    /// Convenience: two-point segment plus styling.
    ::re::scene::ObjectId addLine(const glm::vec3& a, const glm::vec3& b,
                                  const glm::vec4& color, float width = 2.0f,
                                  bool worldUnits = false) {
        LineDesc d;
        d.segments.push_back(::re::scene::LineSegment{a, b});
        d.color = color;
        d.width = width;
        d.worldUnits = worldUnits;
        return addLine(d);
    }

    /// Overload taking a fully formed LineObject value (advanced).
    ::re::scene::ObjectId addLine(::re::scene::LineObject obj) {
        return ctx_.store().addLineObject(std::move(obj));
    }

    ::re::scene::ObjectId addPolyline(::re::scene::LineObject obj) {
        return ctx_.store().addLineObject(std::move(obj));
    }

    // ---- view helpers -------------------------------------------------------

    /// Install a single view from a plain descriptor (rect + camera + ids).
    /// Replaces any previously installed views. Generates a stable view id
    /// internally; the id is not part of the descriptor so callers never handle
    /// it directly. The view's clear color defaults to the engine's historical
    /// sample default (0.10, 0.10, 0.12, 1.0) so an opaque mesh renders against
    /// the same background as the direct AppContext path used in the gate oracle.
    void setView(const ViewDescriptor& desc) {
        ::re::scene::View v;
        v.id = nextViewId_++;
        v.rect = desc.rect;
        v.camera = desc.camera;
        v.setItemIds(desc.objectIds);
        v.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        applyDepthDefault(v);
        views_.clear();
        views_.push_back(std::move(v));
    }

    /// Install a single fully-formed View (advanced: keeps the caller's rect,
    /// plane, clear color, depth flag and per-view lights exactly as given).
    void setView(const ::re::scene::View& view) {
        views_.clear();
        views_.push_back(view);
    }

    /// Install an explicit view list (advanced: MPR 2×2 etc.).
    void setViews(const std::vector<::re::scene::View>& views) {
        views_ = views;
    }
    void setViews(std::span<const ::re::scene::View> views) {
        views_.assign(views.begin(), views.end());
    }

    /// Publish per-View lights through the facade — mirrors View light setter
    /// bumping lightsGen/generation exactly when the value changes (SPEC §12.3,
    /// TASKS T15 — empty lights = fixed headlight preserved, one Directional
    /// shifts diffuse ≥5/255 deterministically; non-empty via LightMapper to
    /// ReLight upload via ViewSynchronizer per-field cache before drawLayer).
    void setLights(uint64_t viewId, std::vector<::re::scene::Light> lights) {
        for (auto& v : views_) {
            if (v.id == viewId) {
                if (v.lights != lights) {
                    v.lights = std::move(lights);
                    ++v.lightsGen;
                    ++v.generation;
                }
                return;
            }
        }
    }

    /// Single-site helper that centralizes the Rect + Camera ceremony every
    /// sample previously repeated via fitPerspectiveViewToPixels. Creates a View
    /// with the given rect, camera and object ids; the caller can then pass it
    /// to setView. This is the ONE site for that ceremony inside the facade.
    static ::re::scene::View createView(
        const ::re::scene::Rect& rect,
        const ::re::scene::Camera& camera,
        const std::vector<::re::scene::ObjectId>& ids) {
        ::re::scene::View v;
        v.id = 1;
        v.rect = rect;
        v.camera = camera;
        v.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        v.setItemIds(ids);
        applyDepthDefault(v);
        return v;
    }

    /// Convenience rect as four ints (x, y, w, h) — avoids manual Rect aggregate
    /// at call sites that prefer ints.
    static ::re::scene::View createView(
        int x, int y, int w, int h,
        const ::re::scene::Camera& camera,
        const std::vector<::re::scene::ObjectId>& ids) {
        return createView(::re::scene::Rect{x, y, w, h}, camera, ids);
    }

    /// Convenience that also sets the view rect to cover the full window
    /// (`Rect{0,0,width,height}`) without touching the camera's projection.
    /// This replaces the per-sample `Rect{0,0,width,height}` manual aggregate
    /// while keeping the camera's eye/center/up and fov/near/far exactly as
    /// the caller configured it. For the full framing case that recomputes the
    /// perspective aspect as `width/height`, use the `fovDeg/near/far` overload
    /// below — this overload is the 80% copy-paste that needs only `w`/`h`.
    static ::re::scene::View createView(
        int width, int height,
        ::re::scene::Camera camera,
        const std::vector<::re::scene::ObjectId>& ids) {
        ::re::scene::View v;
        v.id = 1;
        v.setRect(::re::scene::Rect{0, 0, width, height});
        v.camera = std::move(camera);
        v.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        v.setItemIds(ids);
        applyDepthDefault(v);
        return v;
    }

    /// Full framing helper: width/height plus explicit fov/near/far — directly
    /// mirrors fitPerspectiveViewToPixels(width,height, framing). This is the
    /// one-stop call that replaces the per-sample `fitPerspectiveViewToPixels`
    /// plus `Rect` plus `Camera` triple.
    static ::re::scene::View createView(
        int width, int height, float fovDeg, float nearPlane, float farPlane,
        ::re::scene::Camera camera,
        const std::vector<::re::scene::ObjectId>& ids) {
        ::re::scene::View v;
        v.id = 1;
        v.setRect(::re::scene::Rect{0, 0, width, height});
        const float aspect = height != 0 ? static_cast<float>(width) /
                                               static_cast<float>(height)
                                         : 1.0f;
        camera.setPerspective(fovDeg, aspect, nearPlane, farPlane);
        v.camera = std::move(camera);
        v.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        v.setItemIds(ids);
        applyDepthDefault(v);
        return v;
    }

    // ---- render -------------------------------------------------------------

    /// Render the current view list into `target` framebuffer via the broker
    /// façade (sync → renderAll → presentAll). The AppContext-owned broker path
    /// is unchanged — Engine forwards to bridge().sync/renderAll/presentAll so
    /// all FR-render.*/FR-app.* gates stay via regression lock R3. Returns a
    /// typed error if sync or render fails.
    ::re::data::Result<void> render(
        ::re::core::Framebuffer& target) {
        return render(&target);
    }

    /// Render into `*target` where `nullptr` means the window's default
    /// framebuffer. This overload lets tests drive an offscreen FBO while
    /// samples that own a Window can call `engine.render()` with no argument
    /// (present to the window's default FBO).
    ::re::data::Result<void> render(
        ::re::core::Framebuffer* /*borrow*/ target) {
        auto s = ctx_.bridge().sync(views_, ctx_.store());
        if (s.failed()) return s;
        auto r = ctx_.bridge().renderAll();
        if (r.failed()) return r;
        return ctx_.bridge().presentAll(target);
    }

    /// Present to the window's default framebuffer (null destination). Call
    /// after a Window has been made current; the bridge blits each ReView's
    /// target 1:1 into its window rect.
    ::re::data::Result<void> render() {
        return render(nullptr);
    }

    // ---- escape hatches (advanced) -----------------------------------------

    /// Full broker composition root — for callers that need direct mapper
    /// registration, custom sync, or the raw SceneStore. The broker path stays
    /// and this accessor is the documented escape hatch.
    ::re::broker::AppContext& appContext() noexcept { return ctx_; }
    const ::re::broker::AppContext& appContext() const noexcept {
        return ctx_;
    }

    /// Direct store access (advanced: manual add/get/remove, generation
    /// queries, hybrid dirty tracking). The facade's helpers delegate to this
    /// store, so mixing both styles sees a consistent generation.
    ::re::scene::SceneStore& store() noexcept { return ctx_.store(); }
    const ::re::scene::SceneStore& store() const noexcept {
        return ctx_.store();
    }

    /// Current view list (read-only snapshot) — for diagnostics or for callers
    /// that build views via Engine::createView and then mutate them before
    /// setView(s).
    const std::vector<::re::scene::View>& views() const noexcept {
        return views_;
    }

   private:
    static void applyDepthDefault(::re::scene::View& v) {
        v.setDepthConfig(::re::scene::DepthConfig{true});
    }
    ::re::broker::AppContext ctx_;
    std::vector<::re::scene::View> views_{};
    uint64_t nextViewId_{1};
};

} // namespace re::viz

// Convenience alias so unqualified `viz::Engine` also works when the caller
// does `using namespace re;`.
namespace viz {
using Engine = ::re::viz::Engine;
} // namespace viz
