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
#include "data/result.hpp"
#include "io/mesh/obj_mesh_loader.hpp"
#include "io/volume/nrrd_volume_loader.hpp"
#include "scene/camera.hpp"
#include "scene/material_desc.hpp"
#include "scene/object.hpp"
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

    /// Load a mesh from `path` and add it to the store with `transform` and
    /// `material`. The 4-step ceremony (load → shared_ptr → MeshObject → add)
    /// that was duplicated across five samples is now one call. Returns a typed
    /// error with domain MeshIo on load failure; no partial object ever enters
    /// the store.
    ::re::data::Result<::re::scene::ObjectId> addMesh(
        const std::string& path, const glm::mat4& transform,
        const ::re::scene::MeshMaterialDesc& material) {
        auto loaded = ::re::io::loadObjMesh(path);
        if (loaded.failed()) {
            return ::re::data::Result<::re::scene::ObjectId>(
                ::re::data::error, loaded.error());
        }
        auto shared =
            std::make_shared<const ::re::data::Mesh>(std::move(*loaded));
        ::re::scene::MeshObject obj;
        obj.mesh = shared;
        obj.transform = transform;
        obj.presentation = material;
        const ::re::scene::ObjectId id =
            ctx_.store().addMeshObject(std::move(obj));
        return ::re::data::Result<::re::scene::ObjectId>(
            ::re::data::value, id);
    }

    /// Convenience: mesh baseColor directly (Phong opaque path). Keeps call sites
    /// that care only about a solid color from constructing a full material desc.
    ::re::data::Result<::re::scene::ObjectId> addMesh(
        const std::string& path, const glm::mat4& transform,
        const glm::vec4& baseColor) {
        ::re::scene::MeshMaterialDesc mat;
        mat.phong.baseColor = baseColor;
        return addMesh(path, transform, mat);
    }

    /// Add mesh with identity transform and default white material — the
    /// single-argument form that makes `examples/minimal.cpp` a 20-line copy-paste.
    ::re::data::Result<::re::scene::ObjectId> addMesh(
        const std::string& path) {
        return addMesh(path, glm::mat4(1.0f),
                       ::re::scene::MeshMaterialDesc{});
    }

    /// Add mesh with identity plus material — `addMesh(path, material)` form.
    ::re::data::Result<::re::scene::ObjectId> addMesh(
        const std::string& path,
        const ::re::scene::MeshMaterialDesc& material) {
        return addMesh(path, glm::mat4(1.0f), material);
    }

    /// Load a volume from `path` and add it with `transform` and an optional
    /// transfer function. Overload without a TF uses an opaque linear ramp.
    ::re::data::Result<::re::scene::ObjectId> addVolume(
        const std::string& path, const glm::mat4& transform,
        const ::re::volume::TransferFunction& tf) {
        auto loaded = ::re::io::loadNrrdVolume(path);
        if (loaded.failed()) {
            return ::re::data::Result<::re::scene::ObjectId>(
                ::re::data::error, loaded.error());
        }
        auto shared = std::make_shared<const ::re::data::VolumeDataset>(
            std::move(*loaded));
        ::re::scene::VolumeObject obj;
        obj.volume = shared;
        obj.transform = transform;
        obj.transferFunction = tf;
        const ::re::scene::ObjectId id =
            ctx_.store().addVolumeObject(std::move(obj));
        return ::re::data::Result<::re::scene::ObjectId>(
            ::re::data::value, id);
    }

    ::re::data::Result<::re::scene::ObjectId> addVolume(
        const std::string& path, const glm::mat4& transform) {
        ::re::volume::TransferFunction tf(
            {{{0.0f, ::re::volume::RgbaColor{0, 0, 0, 0}},
              {1.0f, ::re::volume::RgbaColor{1, 1, 1, 1}}}});
        return addVolume(path, transform, tf);
    }

    ::re::data::Result<::re::scene::ObjectId> addVolume(
        const std::string& path) {
        return addVolume(path, glm::mat4(1.0f));
    }

    ::re::data::Result<::re::scene::ObjectId> addVolume(
        const std::string& path,
        const ::re::volume::TransferFunction& tf) {
        return addVolume(path, glm::mat4(1.0f), tf);
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
