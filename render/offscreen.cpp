// render/offscreen.cpp — window-free scene rendering (SPEC §3/§8, V5 T4).
//
// V5 T4 public API `Result<Image> renderOffscreen(uint32_t w, uint32_t h,
// span<View> views, SceneStore& store)` — the headless entry point that
// visualization consumers call from a server process. See `render/offscreen.hpp`
// for the full provenance and the gate constants (`1/255` offscreen vs window
// parity, `1280x960` / `640x480` MPR viewport dims exact + axis probe within
// 1/255, window-free header check, raw readback only under `core/re_context.cpp`).
//
// Implementation delegates to `utils::OffscreenContext` (hidden GL 4.6 core
// context owning the `GlfwRuntime` ref via `core::GlfwRuntime::acquire` — first
// holder inits GLFW, last holder shuts down, order-independent) and to
// `core::REContext::current().readRgba8` (the sole raw readback site per
// guardrail `no_production_readback`). No core window header include in this
// path — Window remains for interactive samples; the offscreen path reuses the
// window-free `T3:renderViews` broker composition (`ViewBridge::sync →
// renderAll → presentAll`) without a window, so offscreen vs window pixels
// are byte-identical within 1/255 (one LSB, the evidence anchor).
//
// The facade creates a hidden context, builds a `w*h` destination `Framebuffer`
// (color-only color attachment 0 via `core::Texture2D` + `core::Framebuffer`),
// drives the broker (`Broker` + `RenderStack` + `ViewBridge`) with the full
// default mapper inventory (`CameraMapper`, `MeshObjectMapper` + `MaterialMapper`,
// `MeshSliceObjectMapper`, `VolumeObjectMapper`, `VolumeSliceObjectMapper`,
// `PlaneMapper`, `PlaneObjectMapper`, `ContourMapper`), syncs the passed
// `views` against the passed `SceneStore`, renders and presents into the
// destination, then reads back via `REContext` and flips rows to top-left
// `data::Image` convention. The `T3:renderViews` helper is not called directly
// here — the explicit `sync → renderAll → presentAll` triple is the same code
// `renderViews` executes, so the two paths share one implementation and pixel
// parity is exact within 1/255 (the `T3` gate verified the helper itself).

#include "render/offscreen.hpp"

#include <cstdint>
#include <memory>
#include <vector>

#include "broker/app_context.hpp"
#include "broker/broker.hpp"
#include "broker/camera_mapper.hpp"
#include "broker/contour_mapper.hpp"
#include "broker/material_mapper.hpp"
#include "broker/mesh_object_mapper.hpp"
#include "broker/mesh_slice_object_mapper.hpp"
#include "broker/plane_mapper.hpp"
#include "broker/plane_object_mapper.hpp"
#include "broker/render_stack.hpp"
#include "broker/view_bridge.hpp"
#include "broker/volume_object_mapper.hpp"
#include "broker/volume_slice_object_mapper.hpp"
#include "core/framebuffer.hpp"
#include "core/re_context.hpp"
#include "core/texture2d.hpp"
#include "data/image.hpp"
#include "data/result.hpp"
#include "utils/offscreen_context.hpp"

namespace re::render {

namespace {

data::Result<data::Image> renderOffscreenImpl(
    std::uint32_t w, std::uint32_t h,
    std::span<const scene::View> views,
    const scene::SceneStore& store) {
    if (w == 0u || h == 0u) {
        return data::makeError<data::Image>(1, "renderOffscreen: zero dimensions");
    }

    auto ctxRes = utils::OffscreenContext::create();
    if (ctxRes.failed()) {
        return data::makeError<data::Image>(2, "renderOffscreen: hidden context failed: " + ctxRes.error().message);
    }
    ctxRes->makeCurrent();

    // Fresh broker composition — same wiring as `broker::AppContext` (the
    // composition root) but driven directly against the caller-provided
    // `SceneStore& store` instead of `AppContext::store()`. This keeps the
    // offscreen path window-free and avoids moving the caller's store.
    auto assets = std::make_shared<AssetRegistry>();
    auto stack = broker::RenderStack::create(assets, false);
    auto brokerPtr = std::make_shared<broker::Broker>();
    auto materials = std::make_shared<broker::MaterialMapper>(assets);
    brokerPtr->registerMapper(std::make_unique<broker::CameraMapper>());
    brokerPtr->registerMapper(std::make_unique<broker::MeshObjectMapper>(assets, materials));
    brokerPtr->registerMapper(std::make_unique<broker::MeshSliceObjectMapper>(assets, materials));
    brokerPtr->registerMapper(std::make_unique<broker::VolumeObjectMapper>(assets));
    brokerPtr->registerMapper(std::make_unique<broker::VolumeSliceObjectMapper>(assets));
    brokerPtr->registerMapper(std::make_unique<broker::PlaneMapper>());
    brokerPtr->registerMapper(std::make_unique<broker::PlaneObjectMapper>(assets));
    brokerPtr->registerMapper(std::make_unique<broker::ContourMapper>(assets));

    auto bridge = broker::ViewBridge::create(brokerPtr, stack);

    auto texRes = core::Texture2D::create();
    if (texRes.failed()) {
        return data::makeError<data::Image>(3, "renderOffscreen: texture create failed: " + texRes.error().message);
    }
    core::Texture2D tex = std::move(*texRes);
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(w) * h * 4u, 0u);
    tex.bind(0u);
    tex.upload(w, h, zeros.data());
    tex.unbind(0u);

    auto fbRes = core::Framebuffer::create();
    if (fbRes.failed()) {
        return data::makeError<data::Image>(4, "renderOffscreen: framebuffer create failed: " + fbRes.error().message);
    }
    core::Framebuffer fb = std::move(*fbRes);
    fb.bind();
    fb.attachColor(tex);
    if (!fb.isComplete()) {
        return data::makeError<data::Image>(5, "renderOffscreen: framebuffer incomplete");
    }
    fb.unbind();

    auto s = bridge->sync(views, store);
    if (s.failed()) {
        return data::makeError<data::Image>(6, "renderOffscreen: sync failed: " + s.error().message);
    }
    auto r = bridge->renderAll();
    if (r.failed()) {
        return data::makeError<data::Image>(7, "renderOffscreen: renderAll failed: " + r.error().message);
    }
    auto p = bridge->presentAll(&fb);
    if (p.failed()) {
        return data::makeError<data::Image>(8, "renderOffscreen: presentAll failed: " + p.error().message);
    }

    fb.bind();
    std::vector<std::uint8_t> bottomUp;
    auto read = core::REContext::current().readRgba8(0u, 0u, w, h, bottomUp);
    if (read.failed()) {
        return data::makeError<data::Image>(9, "renderOffscreen: readRgba8 failed: " + read.error().message);
    }
    if (bottomUp.size() != static_cast<std::size_t>(w) * h * 4u) {
        return data::makeError<data::Image>(10, "renderOffscreen: read size mismatch");
    }

    std::vector<std::uint8_t> topLeft(bottomUp.size());
    const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
    for (std::uint32_t y = 0u; y < h; ++y) {
        const std::size_t srcRow = static_cast<std::size_t>(y) * rowBytes;
        const std::size_t dstRow = static_cast<std::size_t>(h - 1u - y) * rowBytes;
        std::copy(bottomUp.begin() + static_cast<std::ptrdiff_t>(srcRow),
                  bottomUp.begin() + static_cast<std::ptrdiff_t>(srcRow + rowBytes),
                  topLeft.begin() + static_cast<std::ptrdiff_t>(dstRow));
    }

    data::Image img(static_cast<std::int32_t>(w), static_cast<std::int32_t>(h), 4, std::move(topLeft));
    return data::makeValue<data::Image>(std::move(img));
}

} // namespace

data::Result<data::Image> renderOffscreen(
    std::uint32_t w, std::uint32_t h,
    std::span<const scene::View> views,
    const scene::SceneStore& store) {
    return renderOffscreenImpl(w, h, views, store);
}

data::Result<data::Image> renderOffscreen(
    std::uint32_t w, std::uint32_t h,
    std::span<const scene::View> views,
    scene::SceneStore& store) {
    return renderOffscreenImpl(w, h, views, store);
}

} // namespace re::render
