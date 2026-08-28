#pragma once

// core/caps.hpp — GPU capability probe (SPEC §5, §7, T11 No cap streaming).
//
// T11 introduces No cap streaming for volumes: any dims via core::Caps tiled/
// downsampled streaming. The product loader has no ≤128³ cap; the committed
// sample_ct.nrrd is just example 128×128×70 ≤128³. render/volume_renderer.cpp
// checks maxTexture3DSize via core::Caps {uint32_t maxTexture3DSize; bool
// ssboAtomics;} cached core::caps() (core/caps.cpp calls glGetIntegerv(
// GL_MAX_3D_TEXTURE_SIZE) / glGetString once until RHI lands, TODO(RHI) →
// IRHIContext::capabilities() after T10 core/rhi/ per docs/spec/nfr.md:25
// modules.md:34) and either downsamples or tiles Texture3D (tiled 1/255 within
// reference, not BudgetExceeded for >128³ alone — BudgetExceeded only when
// core::Caps probe fails). Depends on: core::Caps wrapper (no IRHIContext yet,
// IRHIContext is (stretch) T10; T11 uses TODO(RHI) adapter). This header is
// GL-call-free (no <glad/gl.h> leak per T5 firewall); the probe lives in
// caps.cpp where glad is included privately. Single ledger via REContext
// elsewhere; caps is read-only after init. Provides both caps and
// maxTexture3D size for volumes and ssboAtomics for OIT (T11b).

#include <cstdint>

namespace re::core {

/// GPU capabilities queried once at first use (cached, thread-safe on first
/// call). Field set pinned per SPEC §5 NFR RHI capability contract:
/// maxTexture3DSize from GL_MAX_3D_TEXTURE_SIZE, ssboAtomics from
/// GL_ATOMIC_COUNTER_BUFFER / GL version 4.2+ (SSBO atomics). Until RHI lands
/// (TODO(RHI) → IRHIContext::capabilities()), these are direct GL probes.
/// The cached value survives across frames until process exit; reset via
/// resetCaps() in tests.
struct Caps {
    /// Maximum 3D texture size per GL_MAX_3D_TEXTURE_SIZE (e.g. 256+ on
    /// llvmpipe/Mesa, 2048+ on hardware). 0 means probe failed (no GL context
    /// or glGetIntegerv not loaded) — callers treat 0 as BudgetExceeded probe-fail.
    std::uint32_t maxTexture3DSize{0u};
    /// True if SSBO atomics / atomic-counter buffers are available (GL 4.2+
    /// or ARB_shader_storage_buffer_object + ARB_shader_atomic_counters).
    /// Probed via glGetString(GL_VERSION) containing "4." and via
    /// GL_ATOMIC_COUNTER_BUFFER presence (until RHI, heuristic).
    bool ssboAtomics{false};
};

/// Return the cached caps, probing GL on first call via glGetIntegerv(
/// GL_MAX_3D_TEXTURE_SIZE) and glGetString. If no GL context is current or
/// entry points are not loaded, returns Caps{0, false} (probe fail — caller
/// surfaces BudgetExceeded). Thread-safe after first call (call_once).
/// TODO(RHI): migrate probe to IRHIContext::capabilities() after T10
/// core/rhi/ lands (see docs/spec/nfr.md:25, modules.md:34).
const Caps& caps() noexcept;

/// Invalidate the cached probe (test helper): next caps() re-probes.
/// Forcing a re-probe is needed in tests that create a fresh offscreen
/// context (e.g. T11 256³ tiled: force caps to re-read after context creation).
void resetCaps() noexcept;

/// Override caps for testing (inject a mock Caps without GL probe). The
/// injected value is returned by subsequent caps() calls until resetCaps().
/// @note lifetime: non-owning — caller keeps Caps value; test must reset after.
void injectCaps(const Caps& c) noexcept;

} // namespace re::core
