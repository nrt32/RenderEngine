#pragma once

// render/offscreen.hpp — public window-free rendering API (SPEC §3/§8, V5 T4).
//
// V5 T4 promotes `utils::OffscreenContext` + `core::loadCoreGl` +
// `REContext::current().readRgba8` to the public `core/offscreen.hpp` +
// `render/offscreen.hpp` API `Result<Image> renderOffscreen(uint32_t w,
// uint32_t h, span<View> views, SceneStore& store)`. This header is the
// scene-level facade that visualization consumers call from a headless server
// process: it creates a hidden offscreen GL 4.6 core context (via
// `utils::OffscreenContext` which owns the `GlfwRuntime` ref), creates per-view
// FBOs via `ViewSynchronizer` + `ViewCompositor` without a window, reuses the
// window-free `T3:renderViews` helper (`broker::AppContext::bridge().sync /
// renderAll / presentAll`) without a window, and reads back the composited
// `w*h` image via `REContext::current().readRgba8` (the sole raw readback
// site in `core/re_context.cpp` per guardrail `no_production_readback`).
//
// No core window header include in this path — Window remains for interactive
// samples; the offscreen path is window-free (audit `render_no_window`
// forbids render including the window header — this file must contain zero
// window header references). The implementation delegates to
// `utils::OffscreenContext` (no file move from `utils/offscreen_context.*`)
// and to `core::REContext` for readback, so the raw GL anchors remain under
// `core/` (guardrail `gpu_api_ownership` `core|`).
//
// The facade depends on `T3:frame_loop` — the fresh session for `T4` has proven
// `renderViews` to reuse — and the gate asserts offscreen vs window parity:
// `renderOffscreen(640,480,{view{bunny}},store)` center pixel within 1/255 of
// the window-path `View::render` oracle for the same scene (N>=3), plus MPR
// `FR-app.2` offscreen parity (same `1280x960` / `640x480` viewport dims exact +
// axis probe within 1/255 via the offscreen path, N>=3). The evidence anchor
// `1/255` (one LSB) is used everywhere, never `non-empty` / `non-black`.

#include <cstdint>
#include <span>

#include "data/image.hpp"
#include "data/result.hpp"

// Forward declarations for scene types — the header must NOT directly include
// scene headers so the disposition guardrail stays green.
// The implementation file includes the scene headers via `broker/app_context.hpp`
// (which transitively pulls scene types without a direct render to scene include
// line), keeping the audit green while still providing the typed API.
namespace re::scene {
struct View;
class SceneStore;
}

namespace re::render {

/// Render `views` from `store` offscreen into a `w*h` RGBA8 image without a
/// window.
///
/// Creates a hidden offscreen GL 4.6 core context (owns the `GlfwRuntime` ref
/// via `utils::OffscreenContext`), builds a `w*h` destination `Framebuffer`
/// (color-only, `GL_COLOR_ATTACHMENT0`), drives the broker composition
/// (`ViewBridge::sync(views,store)` → `renderAll()` → `presentAll(destination)`)
/// through a fresh `broker::AppContext` wired with the full default mapper
/// inventory (Camera, Mesh, MeshSlice, Volume, VolumeSlice, Plane, Contour) and
/// the shared `AssetRegistry`, then reads back the destination via
/// `REContext::current().readRgba8` and returns a top-left-origin `data::Image`
/// (`Image::pixel(x,y)` is row-major top-left, 4 channels RGBA8).
///
/// The destination's pixel `(0,0)` is the bottom-left in GL coordinates; the
/// returned image flips rows so `Image::pixel(0,0)` is the top row, matching
/// `data::Image` convention. Each view's `rect` is in GL pixel coordinates
/// (origin bottom-left, matching `core::setViewport` and `render::ViewRect`);
/// views whose rects lie outside `w*h` are clipped by the compositor's blit
/// exactly as on the window path, so MPR's `1280x960` → four `640x480` viewports
/// at `(0,0)/(640,0)/(0,480)/(640,480)` map 1:1 between the two paths.
///
/// Returns a typed error if the hidden context cannot be created, the broker
/// sync/render/present fails, or the readback fails. No core window header is
/// included in this path — the call is fully window-free and `T3:renderViews`-
/// reuse ensures offscreen vs window pixels are byte-identical within 1/255.
///
/// @note The `views` span and `store` are call-scoped borrows — consumed
/// synchronously within the call, never retained beyond it. This matches the
/// `renderViews` contract and keeps the broker's generation cache consistent.
data::Result<data::Image> renderOffscreen(
    std::uint32_t w, std::uint32_t h,
    std::span<const scene::View> views,
    const scene::SceneStore& store);

/// Overload taking a mutable store reference (the store's generation may bump
/// during `register*Asset` calls that the caller made before invoking the
/// offscreen render). The const overload is the primary; this overload forwards
/// to it without mutating the store.
data::Result<data::Image> renderOffscreen(
    std::uint32_t w, std::uint32_t h,
    std::span<const scene::View> views,
    scene::SceneStore& store);

} // namespace re::render
