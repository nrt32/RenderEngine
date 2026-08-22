#pragma once

// utils/offscreen_context.hpp — offscreen OpenGL 4.6 core context for headless
// unit tests (SPEC §2, T1; moved to utils/ by V2.1, SPEC §9).
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

#include "core/load_core_gl.hpp"
#include "data/result.hpp"

struct GLFWwindow;

namespace re::utils {

/// Backend that actually created the context (for diagnostics / tests).
enum class ContextBackend {
    Glfw, ///< Hidden GLFW window + GL 4.6 core context.
    Egl,  ///< EGL-surfaceless GL 4.6 core context (no display available).
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

   private:
    explicit OffscreenContext(ContextBackend backend) noexcept;

    /// Destroy the underlying GLFW window / EGL display+context, if any.
    /// Idempotent: members are nulled after release, so a second call is a
    /// no-op.
    void release() noexcept;

    static data::Result<OffscreenContext> createGlfw();
    static data::Result<OffscreenContext> createEgl();

    GLFWwindow* window_{nullptr};
    void* eglDisplay_{nullptr};
    void* eglContext_{nullptr};
    ContextBackend backend_{ContextBackend::Glfw};
    core::GlContextInfo info_{};
};

} // namespace re::utils
