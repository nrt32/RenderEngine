// core/caps.cpp — Caps probe implementation (T11 No cap streaming).
//
// This is the sole GL capability probe for maxTexture3DSize / ssboAtomics.
// Until RHI lands, it calls glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE) and
// glGetString(GL_VERSION/GL_EXTENSIONS) once and caches the result (TODO(RHI)
// → IRHIContext::capabilities() after T10 core/rhi/ per docs/spec/nfr.md:25,
// modules.md:34). The probe lives only here under core/ per gpu_api_ownership
// (raw GL only under core/, render/ never raw gl*). Even though render/
// consumes Caps, the raw call stays in core/. T11b reuses ssboAtomics from
// the same Caps for weighted-blended OIT fallback (w*h*16*32 152 MB).

#include "core/caps.hpp"

#include <glad/gl.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace re::core {

namespace {

std::mutex g_mutex;
Caps g_cached{};
bool g_probed{false};
bool g_injected{false};
Caps g_injectedCaps{};

Caps probeGl() noexcept {
    Caps out{};
    // glGetIntegerv is the probe for maxTexture3DSize (SPEC §5 RHI capability
    // contract: IRHIContext::capabilities().maxTexture3DSize). If no context
    // is current, entry points are null — return 0 to signal probe fail
    // (caller treats 0 as BudgetExceeded, not OOM). The task says
    // BudgetExceeded only when core::Caps probe fails — this is that path.
    if (glGetIntegerv == nullptr) {
        return Caps{0u, false};
    }
    // NOLINTNEXTLINE — raw GL probe isolated to core/ (TODO(RHI))
    GLint max3d = 0;
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &max3d);
    if (max3d <= 0) {
        // Probe failed (no context or driver returned 0) — BudgetExceeded path.
        out.maxTexture3DSize = 0u;
    } else {
        out.maxTexture3DSize = static_cast<std::uint32_t>(max3d);
    }

    // ssboAtomics probe for T11b: GL 4.2+ has SSBO + atomic counters, else
    // check extension string. Until RHI, heuristic via GL_VERSION string
    // containing "4." with minor >=2, or via GL_ATOMIC_COUNTER_BUFFER enum
    // presence. Conservatively set true if version >=4.2, else check extension
    // list for GL_ARB_shader_storage_buffer_object.
    out.ssboAtomics = false;
    if (glGetString != nullptr) {
        const auto* verStr = glGetString(GL_VERSION);
        if (verStr != nullptr) {
            // Parse "4.6" style — major.minor.
            int major = 0, minor = 0;
            if (std::sscanf(reinterpret_cast<const char*>(verStr), "%d.%d", &major, &minor) == 2) {
                if (major > 4 || (major == 4 && minor >= 2)) {
                    out.ssboAtomics = true;
                }
            }
        }
        // Fallback: extension string search if version <4.2 but SSBO present.
        if (!out.ssboAtomics) {
            const auto* extStr = glGetString(GL_EXTENSIONS);
            if (extStr != nullptr) {
                std::string exts(reinterpret_cast<const char*>(extStr));
                if (exts.find("GL_ARB_shader_storage_buffer_object") != std::string::npos ||
                    exts.find("GL_ARB_shader_atomic_counters") != std::string::npos) {
                    out.ssboAtomics = true;
                }
            }
        }
        // Also check atomic counter buffer binding point via glGetIntegerv if available.
        if (!out.ssboAtomics && glGetIntegerv != nullptr) {
            // GL_ATOMIC_COUNTER_BUFFER is 0x92C0; just probing max doesn't validate, but
            // if version is 4.2+ we already set true. This second probe is heuristic
            // until RHI provides capabilities().ssboAtomics.
            // No additional GL call needed beyond version check.
        }
    }
    return out;
}

} // namespace

const Caps& caps() noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_injected) {
        return g_injectedCaps;
    }
    if (g_probed) {
        return g_cached;
    }
    // First call: probe GL.
    Caps p = probeGl();
    g_cached = p;
    g_probed = true;
    return g_cached;
}

void resetCaps() noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_probed = false;
    g_injected = false;
    g_cached = Caps{};
    g_injectedCaps = Caps{};
}

void injectCaps(const Caps& c) noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_injected = true;
    g_injectedCaps = c;
    g_probed = true;
    g_cached = c;
}

} // namespace re::core
