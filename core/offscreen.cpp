// core/offscreen.cpp — public offscreen core facade (SPEC §3/§8, V5 T4).
//
// This translation unit exists so `re_core` has a compilation unit for the
// public `core/offscreen.hpp` facade. The actual offscreen context
// implementation stays in `utils/offscreen_context.*` (no file move); this
// facade re-exports it as `core::OffscreenContext` (alias in the header) and
// delegates raw GL loading to `core::loadCoreGl` and readback to
// `REContext::readRgba8` (the sole raw readback site in `core/re_context.cpp`
// per `no_production_readback`). The file intentionally contains no window
// header include — the offscreen path is window-free (audit `render_no_window`).

#include "core/offscreen.hpp"

// No additional implementation — the alias in the header is sufficient.
// This file ensures `core/offscreen.hpp` is part of the `re_core` build and
// that the documentation map (`core/offscreen.hpp` + `render/offscreen.hpp`)
// is satisfied without introducing a `core → utils` link cycle (the alias is
// header-only; `render/offscreen.cpp` is the module that actually instantiates
// `utils::OffscreenContext` and therefore links `re_utils`).
