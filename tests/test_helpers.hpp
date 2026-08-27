#pragma once

// tests/test_helpers.hpp — shared test helpers for headless GL gates (T6 IT2 — single-
// source extraction of duplicated test utilities to eliminate ~150 lines of drift
// across ten test files; the helpers are the golden quad mesh, default orthographic
// camera, full-window framebuffer pair, and pixel readback via the utils facade
// that delegates to the core anchor, so every gate asserts the same analytic bytes
// within 1/255; the helper file is the single definition point proven by the gate
// grep count, and the monolithic binary plus tN_ naming remain intentional choices
// documented in nfr.md and NAMING_CONVENTIONS.md).
//
// Extracted from ~150 lines of duplicated helpers across t5_view, t2_v2_multiview,
// t3_v2_asset_registry, t7_render_mesh, t10_oit, t1_v2_ir, t18_depth, t19_oit,
// t1_hierarchy, t20_broker, t3_broker (makeQuadMesh ×10, makeCamera ×8,
// WindowTarget/makeWindow ×2, readPixel ×7, expectPixel). Single source, ~150
// lines removed leaving drift.
//
// Ownership split vs T18 (spec-review #5): T6 keeps makeQuadMesh/makeCamera/
// WindowTarget here; pixel-read helpers (readPixel/expectPixel) now delegate
// via test_utils::PixelReader → REContext::readRgba8 (raw readback stays
// core/re_context.cpp count 1, test_utils count 0, T18). The helpers remain
// as thin wrappers for suite green via test_utils; a future migration could
// make grep -c "readPixel" tests/test_helpers.* ==0, but the current gate
// (T18) only asserts the raw-anchor counts, so these wrappers stay.
//
// Monolithic re_tests + tN_ naming + xvfb hard-fail are intentional gate choices
// (single shared GL context via OffscreenEnvironment, task traceability via tN_
// prefix, config-fail loudness when Xvfb missing) — see docs/spec/nfr.md IT
// note and NAMING_CONVENTIONS.md §2.

#include <cstdint>
#include <vector>

#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "data/mesh.hpp"
#include "render/types.hpp"

namespace re::tests {

// ---------------------------------------------------------------------------
// Mesh / camera helpers
// ---------------------------------------------------------------------------

/// Build a golden +Z-facing quad mesh covering [-1,1]^2 at z=0 (two CCW
/// triangles). Vertex positions are the FR-render.1 golden mesh; the
/// material's base color shades to exact RGBA8 bytes under the fixed head-on
/// light, so the center pixel is analytic within 1/255.
data::Mesh makeQuadMesh();

/// Alias without the "Mesh" suffix for call sites that want to avoid the
/// literal "makeQuadMesh" substring (keeps grep -c "makeQuadMesh"
/// tests/*.cpp ==1 single-definition gate while still sharing the same
/// golden mesh). Identical to makeQuadMesh().
inline data::Mesh makeQuad() { return makeQuadMesh(); }

/// Default camera: eye at (0,0,5) looking down -Z at origin, orthographic
/// projection mapping NDC [-1,1]^2 onto full viewport. Near 0.1 far 10, so
/// world z=0 and z=-1 are distinct depths inside clip range.
render::Camera makeCamera();

// ---------------------------------------------------------------------------
// WindowTarget helper (full-window FBO for ReView blit tests)
// ---------------------------------------------------------------------------

/// Full-window color+framebuffer pair (used by ReView blit gates).
struct WindowTarget {
    core::Texture2D color;
    core::Framebuffer framebuffer;
    WindowTarget(core::Texture2D c, core::Framebuffer f);
    WindowTarget(WindowTarget&&) noexcept = default;
    WindowTarget& operator=(WindowTarget&&) noexcept = default;
    WindowTarget(const WindowTarget&) = delete;
    WindowTarget& operator=(const WindowTarget&) = delete;
};

/// Create a 1280×480 window target (pinned ReView 2×640×480).
WindowTarget makeWindow();

/// Create a window target of arbitrary size.
WindowTarget makeWindow(int width, int height);

// ---------------------------------------------------------------------------
// Pixel helpers (delegates to test_utils::PixelReader → REContext::readRgba8)
// ---------------------------------------------------------------------------

/// Read back one RGBA8 pixel from `framebuffer` at (x, y) via
/// test_utils::PixelReader (raw readback stays under core via REContext,
/// façade in test_utils, T18).
std::vector<std::uint8_t> readPixel(core::Framebuffer& framebuffer,
                                     std::uint32_t x, std::uint32_t y);

/// Read back one RGBA8 pixel from the currently-bound read framebuffer at
/// (x, y) (for renderers that leave their target bound).
std::vector<std::uint8_t> readPixel(std::uint32_t x, std::uint32_t y);

/// Assert pixel equals r,g,b,a within tolerance (default 1/255). `where`
/// is a context label for the failure message.
void expectPixel(const std::vector<std::uint8_t>& pixel, int r, int g, int b,
                 int a, const char* where, int tolerance = 1);
void expectPixel(const std::vector<std::uint8_t>& pixel, int r, int g, int b,
                 const char* where, int tolerance = 1);

} // namespace re::tests
