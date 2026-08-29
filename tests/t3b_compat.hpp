#pragma once
// tests/t3b_compat.hpp — T3b compatibility helpers: old renderer.render() deleted,
// ported to View path (single OIT via ViewCompositor). Used by multiple test
// files to keep them green after T3b collapse.

#include "data/result.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_renderer.hpp"
#include "render/slice_renderer.hpp"
#include "render/plane_renderer.hpp"
#include "render/volume_renderer.hpp"
#include "render/volume_slice_renderer.hpp"
#include "render/contour_renderer.hpp"
#include "render/view.hpp"
#include "render/types.hpp"
#include "core/framebuffer.hpp"
#include <memory>

namespace re::tests {

// MeshRenderer via View (single drawInstances blend-off) — T3b View port keeps View alive via static for readback after return
inline data::Result<void> renderMeshViaView(render::MeshRenderer& r, const render::MeshScene& s, const render::Camera& cam, uint32_t w, uint32_t h) {
    static thread_local std::optional<render::View> s_view;
    auto ptr = std::make_shared<render::MeshRenderer>(r.assetRegistry(), r.transparencyPipeline());
    s_view.emplace(render::ViewRect{0,0,static_cast<int>(w),static_cast<int>(h)}, glm::vec4(0,0,0,0));
    s_view->setCamera(cam);
    s_view->clearItems();
    s_view->addItem(s, ptr);
    auto res = s_view->renderWithEnsure();
    if (res.ok()) {
        // Keep framebuffer bound for immediate readPixel after return (View lifetime via static)
        s_view->target()->framebuffer().bind();
    }
    return res;
}
inline data::Result<void> renderMeshViaView(render::MeshRenderer& r, const render::MeshScene& s, const render::Camera& cam, const render::RenderTarget& rt) {
    // Direct draw to provided target (for tests that create their own FBO and read from it)
    auto& ctx = core::REContext::current();
    ctx.beginPass(rt.framebuffer, rt.width, rt.height, rt.clearColor.r, rt.clearColor.g, rt.clearColor.b, rt.clearColor.a);
    return r.drawLayer(s, cam);
}
// Overload for any RenderTarget-like with .width/.height
inline data::Result<void> renderMeshViaView(render::MeshRenderer& r, const render::MeshScene& s, const render::Camera& cam, core::Framebuffer* /*fb*/, uint32_t w, uint32_t h) {
    return renderMeshViaView(r, s, cam, w, h);
}

// VolumeRenderer via View — keeps View alive via static for readback
inline data::Result<void> renderVolumeViaView(render::VolumeRenderer& r, const render::VolumeScene& s, const render::Camera& cam, uint32_t w, uint32_t h) {
    static thread_local std::optional<render::View> s_view;
    auto ptr = std::make_shared<render::VolumeRenderer>(r.assets());
    s_view.emplace(render::ViewRect{0,0,static_cast<int>(w),static_cast<int>(h)}, glm::vec4(0,0,0,0));
    s_view->setCamera(cam);
    s_view->clearItems();
    s_view->addItem(s, ptr);
    auto res = s_view->renderWithEnsure();
    if (res.ok()) {
        s_view->target()->framebuffer().bind();
    }
    return res;
}
inline data::Result<void> renderVolumeViaView(render::VolumeRenderer& r, const render::VolumeScene& s, const render::Camera& cam, const render::RenderTarget& rt) {
    auto& ctx = core::REContext::current();
    ctx.beginPass(rt.framebuffer, rt.width, rt.height, rt.clearColor.r, rt.clearColor.g, rt.clearColor.b, rt.clearColor.a);
    return r.drawLayer(s, cam);
}

// SliceRenderer (mesh slice) via View — keeps View alive via static
inline data::Result<void> renderSliceMeshViaView(render::SliceRenderer& r, const render::SliceScene& s, const render::Camera& cam, uint32_t w, uint32_t h) {
    static thread_local std::optional<render::View> s_view;
    auto ptr = std::make_shared<render::SliceRenderer>(r.assetRegistry());
    s_view.emplace(render::ViewRect{0,0,static_cast<int>(w),static_cast<int>(h)}, glm::vec4(0,0,0,0));
    s_view->setCamera(cam);
    s_view->clearItems();
    s_view->addItem(s, ptr);
    auto res = s_view->renderWithEnsure();
    if (res.ok()) s_view->target()->framebuffer().bind();
    return res;
}
inline data::Result<void> renderSliceMeshViaView(render::SliceRenderer& r, const render::SliceScene& s, const render::Camera& cam, const render::RenderTarget& rt) {
    auto& ctx = core::REContext::current();
    ctx.beginPass(rt.framebuffer, rt.width, rt.height, rt.clearColor.r, rt.clearColor.g, rt.clearColor.b, rt.clearColor.a);
    return r.drawLayer(s, cam);
}
inline data::Result<void> renderSliceMeshViaView(render::SliceRenderer& r, const render::SliceScene& s, const render::ClipPlane& plane, const render::Camera& cam, const render::RenderTarget& rt) {
    render::SliceScene sceneWithPlane = s;
    sceneWithPlane.plane = plane;
    auto& ctx = core::REContext::current();
    ctx.beginPass(rt.framebuffer, rt.width, rt.height, rt.clearColor.r, rt.clearColor.g, rt.clearColor.b, rt.clearColor.a);
    return r.drawLayer(sceneWithPlane, cam);
}
inline data::Result<void> renderSliceMeshViaView(render::SliceRenderer& r, const render::SliceScene& s, const render::Camera& cam, const render::ClipPlane& plane, const render::RenderTarget& rt) {
    render::SliceScene sceneWithPlane = s;
    sceneWithPlane.plane = plane;
    auto& ctx = core::REContext::current();
    ctx.beginPass(rt.framebuffer, rt.width, rt.height, rt.clearColor.r, rt.clearColor.g, rt.clearColor.b, rt.clearColor.a);
    return r.drawLayer(sceneWithPlane, cam);
}

// PlaneRenderer via View — keeps View alive via static
inline data::Result<void> renderPlaneViaView(render::PlaneRenderer& r, const render::PlaneScene& s, const render::Camera& cam, uint32_t w, uint32_t h) {
    static thread_local std::optional<render::View> s_view;
    (void)r;
    s_view.emplace(render::ViewRect{0,0,static_cast<int>(w),static_cast<int>(h)}, glm::vec4(0,0,0,0));
    s_view->setCamera(cam);
    s_view->clearItems();
    s_view->addItem(s, std::make_shared<render::PlaneRenderer>());
    auto res = s_view->renderWithEnsure();
    if (res.ok()) s_view->target()->framebuffer().bind();
    return res;
}
inline data::Result<void> renderPlaneViaView(render::PlaneRenderer& r, const render::PlaneScene& s, const render::Camera& cam, const render::RenderTarget& rt) {
    auto& ctx = core::REContext::current();
    ctx.beginPass(rt.framebuffer, rt.width, rt.height, rt.clearColor.r, rt.clearColor.g, rt.clearColor.b, rt.clearColor.a);
    return r.drawLayer(s, cam);
}

// VolumeSlice via View — keeps View alive via static
inline data::Result<void> renderSliceViaView(render::VolumeSliceRenderer& r, const render::VolumeSliceScene& s, const render::Camera& cam, uint32_t w, uint32_t h) {
    static thread_local std::optional<render::View> s_view;
    auto ptr = std::make_shared<render::VolumeSliceRenderer>(r.assets());
    s_view.emplace(render::ViewRect{0,0,static_cast<int>(w),static_cast<int>(h)}, glm::vec4(0,0,0,0));
    s_view->setCamera(cam);
    s_view->clearItems();
    s_view->addItem(s, ptr);
    auto res = s_view->renderWithEnsure();
    if (res.ok()) s_view->target()->framebuffer().bind();
    return res;
}
inline data::Result<void> renderSliceViaView(render::VolumeSliceRenderer& r, const render::VolumeSliceScene& s, const render::Camera& cam, const render::RenderTarget& rt) {
    auto& ctx = core::REContext::current();
    ctx.beginPass(rt.framebuffer, rt.width, rt.height, rt.clearColor.r, rt.clearColor.g, rt.clearColor.b, rt.clearColor.a);
    return r.drawLayer(s, cam);
}

// Contour via View — keeps View alive via static
inline data::Result<void> renderContourViaView(render::ContourRenderer& r, const render::ContourScene& s, const render::Camera& cam, uint32_t w, uint32_t h) {
    static thread_local std::optional<render::View> s_view;
    auto ptr = std::make_shared<render::ContourRenderer>(r.assetRegistry());
    s_view.emplace(render::ViewRect{0,0,static_cast<int>(w),static_cast<int>(h)}, glm::vec4(0,0,0,0));
    s_view->setCamera(cam);
    s_view->clearItems();
    s_view->addItem(s, ptr);
    auto res = s_view->renderWithEnsure();
    if (res.ok()) s_view->target()->framebuffer().bind();
    return res;
}
inline data::Result<void> renderContourViaView(render::ContourRenderer& r, const render::ContourScene& s, const render::Camera& cam, const render::RenderTarget& rt) {
    auto& ctx = core::REContext::current();
    ctx.beginPass(rt.framebuffer, rt.width, rt.height, rt.clearColor.r, rt.clearColor.g, rt.clearColor.b, rt.clearColor.a);
    return r.drawLayer(s, cam);
}

} // namespace re::tests
