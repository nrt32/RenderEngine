#pragma once

// utils/offscreen_context.hpp — offscreen OpenGL 4.6 core context for headless
// unit tests (SPEC §2, T1; moved to utils/ by V2.1, SPEC §9; extended by
// V2.2 to select the no-display backend per-OS).
//
// utils/ is the test-support + windowing home: GLFW/EGL context creation and
// pixel readback are not core rendering. utils/ is NOT the owner of raw GL
// calls — the raw-GL anchors stay under core/ (guardrails gpu_api_ownership /
// no_production_readback):
//   - core::loadCoreGl  loads GL entry points (glad) and probes the
//     version/profile via glGetIntegerv;
//   - core::readRgba8   is the raw pixel-readback anchor (SPEC §6).
// render/, app/ and tests/ consume GL only through core/ wrappers; this class
// is the offscreen-context facade they share.

#include <cstdint>
#include <memory>

#include "core/load_core_gl.hpp"
#include "data/result.hpp"

struct GLFWwindow;

namespace re::core {
class GlfwRuntime;
}

namespace re::utils {

/// Backend that actually created the context (for diagnostics / tests).
///
/// The no-display fallback is selected deterministically per-OS (SPEC §9 V2.2):
/// - Linux: `Egl` (EGL surfaceless via `EGL_PLATFORM_SURFACELESS_MESA`, Mesa).
/// - Windows: `Wgl` with an `Egl`-based ANGLE-EGL alternative where available.
/// - macOS: `Cgl` (CGL / Core OpenGL).
/// `Glfw` is the always-tried primary (hidden window) on every OS; the
/// no-display backend is only used when GLFW cannot create a context (no
/// display server).
enum class ContextBackend {
    Glfw, ///< Hidden GLFW window + GL 4.6 core context (primary on every OS).
    Egl,  ///< EGL-surfaceless (Linux Mesa) or ANGLE-EGL (Windows) fallback.
    Wgl,  ///< WGL no-display fallback (Windows).
    Cgl,  ///< CGL no-display fallback (macOS).
};

/// A RAII-managed offscreen OpenGL 4.6 core context.
///
/// The context is current on the constructing thread for the lifetime of the
/// object. Creating a new OffscreenContext while another is alive is supported
/// only if the GL implementation allows it; the expected usage is one context
/// per test fixture.
///
/// GL entry points are loaded and the version/profile probed by the core/
/// raw-GL anchor core::loadCoreGl (guardrail gpu_api_ownership); the probe
/// values are surfaced through this wrapper, not the GL_VERSION string text.
class OffscreenContext {
   public:
    /// Create and make current an offscreen GL 4.6 core context.
    static data::Result<OffscreenContext> create();

    OffscreenContext(const OffscreenContext&) = delete;
    OffscreenContext& operator=(const OffscreenContext&) = delete;

    OffscreenContext(OffscreenContext&& other) noexcept;
    OffscreenContext& operator=(OffscreenContext&& other) noexcept;

    ~OffscreenContext();

    /// The backend that produced this context.
    ContextBackend backend() const noexcept {
        return backend_;
    }

    /// The context's GL major version (specifically GL_MAJOR_VERSION).
    int majorVersion() const noexcept {
        return info_.major;
    }

    /// The context's GL minor version (specifically GL_MINOR_VERSION).
    int minorVersion() const noexcept {
        return info_.minor;
    }

    /// The GL_CONTEXT_PROFILE_MASK value.
    std::uint32_t profileMask() const noexcept {
        return info_.profileMask;
    }

    /// True if the context's profile mask has the core-profile bit set.
    bool isCoreProfile() const noexcept {
        return info_.isCoreProfile();
    }

    /// The no-display backend this platform would use when GLFW cannot create
    /// a context. Deterministic per-OS (SPEC §9 V2.2): EGL-surfaceless on Linux,
    /// ANGLE-EGL/WGL on Windows, CGL on macOS. Exposed for testing and
    /// diagnostics so the selection can be asserted without needing a display.
    static constexpr ContextBackend platformNoDisplayBackend() noexcept {
#if defined(__APPLE__)
        return ContextBackend::Cgl;
#elif defined(_WIN32) || defined(_WIN64)
        // Windows prefers ANGLE-EGL where available; WGL is the secondary
        // no-display path. The deterministic primary is reported as Wgl so the
        // enum distinguishes it from the Linux EGL-surfaceless path.
        return ContextBackend::Wgl;
#else
        // Linux (and other Unix): Mesa EGL surfaceless.
        return ContextBackend::Egl;
#endif
    }

    /// Human-readable name for a ContextBackend (for logs / diagnostics).
    static const char* backendName(ContextBackend backend) noexcept;

    /// Make this context current on the calling thread and set
    /// REContext::current() to its per-GL-context mirror (T2). Each
    /// GLFWwindow* maps to its own REContextState (viewport etc.); EGL
    /// surfaceless uses the per-thread fallback. Worker threads with private
    /// contexts get private mirrors with no lock.
    void makeCurrent() const noexcept;

    /// Access underlying GLFW window handle (for REContext switching tests).
    /// Null when backend is not Glfw.
    GLFWwindow* glfwHandle() const noexcept { return window_; }

   private:
    explicit OffscreenContext(ContextBackend backend) noexcept;

    /// Destroy the underlying GLFW window / EGL display+context, if any.
    /// Idempotent: members are nulled after release, so a second call is a
    /// no-op.
    void release() noexcept;

    static data::Result<OffscreenContext> createGlfw();
    static data::Result<OffscreenContext> createEgl();
    static data::Result<OffscreenContext> createWgl();
    static data::Result<OffscreenContext> createCgl();
    static data::Result<OffscreenContext> createPlatformFallback();

    GLFWwindow* window_{nullptr};
    void* eglDisplay_{nullptr};
    void* eglContext_{nullptr};
    ContextBackend backend_{ContextBackend::Glfw};
    core::GlContextInfo info_{};
    std::shared_ptr<core::GlfwRuntime> glfwRuntime_;
};

} // namespace re::utils
