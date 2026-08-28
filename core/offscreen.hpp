#pragma once

// core/offscreen.hpp — public offscreen facade (SPEC §3/§8, V5 T4).
//
// V5 T4 promotes `utils::OffscreenContext` (`utils/offscreen_context.hpp`) +
// `core::loadCoreGl` + `REContext::current().readRgba8` to a public
// `core/offscreen.hpp` + `render/offscreen.hpp` API: `Result<Image>
// renderOffscreen(uint32_t w, uint32_t h, span<View> views, SceneStore& store)`
// creates a hidden context, owns the `GlfwRuntime` ref, creates per-view FBOs,
// calls the window-free `T3:renderViews` helper plus `ViewSynchronizer` +
// `ViewCompositor` without a `Window`, and reads back via `REContext`.
//
// This header is the low-level core facade. It owns the hidden GL context
// lifecycle (via `utils::OffscreenContext` which delegates raw GL loading to
// `core::loadCoreGl`; guardrail `gpu_api_ownership` stays `core|` — no file move
// from `utils/offscreen_context.*`). Higher layers (`render/offscreen.hpp`)
// delegate to this core helper so the raw `GlfwRuntime` ownership and the raw
// readback anchor (`REContext::readRgba8` in `core/re_context.cpp` — the sole raw
// readback site per `no_production_readback`) remain under `core/`.
// `render/offscreen.hpp` never includes the window header (audit `render_no_window`).
//
// The facade is window-free: no core window header include in this path.
// Window remains for interactive samples; the offscreen path reuses the same broker
// composition (`AppContext` → `ViewBridge` → `ViewSynchronizer` + `ViewCompositor`)
// that the interactive `T3:renderViews` helper uses, so offscreen vs window
// pixels are byte-identical within 1/255 (one LSB, the evidence anchor).

// Re-export the utils offscreen context as the public core facade (no file move).
// The raw GL anchors stay under `core/` (`core::loadCoreGl` for entry-point
// loading + version/profile probe, `REContext::readRgba8` for readback), while
// the context-creation logic stays in `utils/offscreen_context.*` which
// delegates raw loading to `core::loadCoreGl`. This header is the public
// `core/offscreen.hpp` entry point that `render/offscreen.hpp` and tests
// include without touching the core window header — the window-free contract.

#include "utils/offscreen_context.hpp"

namespace re::core {

// Public alias — `core::OffscreenContext` is the same type as
// `utils::OffscreenContext`. The alias exists so `core/offscreen.hpp` is the
// public façade while `utils/offscreen_context.*` remains the single
// implementation (no file move, guardrail `gpu_api_ownership` `core|`).
using OffscreenContext = utils::OffscreenContext;

} // namespace re::core

// The scene-level `renderOffscreen` lives in `render/offscreen.hpp` and
// delegates to this core alias + `REContext::readRgba8` (the sole raw
// readback site in `core/re_context.cpp` per `no_production_readback`).
// Keeping the alias here makes `core/offscreen.hpp` the low-level public entry
// point and `render/offscreen.hpp` the high-level scene entry point, matching
// the `T4` documentation map.
