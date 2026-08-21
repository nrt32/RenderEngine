#pragma once

// core/offscreen_context.hpp — offscreen OpenGL 4.6 core context for headless
// unit tests (SPEC S2, T1). This is a core/ component: it is the SOLE owner of
// raw context-creation / GL-loader calls in the project. render/, app/ and
// tests/ consume it only through this wrapper.
//
// Strategy:
//   1. GLFW primary: create a hidden (never shown) window and a GL 4.6 core
//      context, then load GL entry points with glad.
//   2. EGL-surfaceless fallback (no display): create a surfaceless EGL display
//      and a GL 4.6 core context, then load GL entry points with glad from the
//      EGL proc-address function.

#include <cstdint>

#include "data/result.hpp"

struct GLFWwindow;

namespace re::core {

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
        return major_;
    }

    /// The context's GL minor version (specifically GL_MINOR_VERSION).
    int minorVersion() const noexcept {
        return minor_;
    }

    /// The GL_CONTEXT_PROFILE_MASK value.
    std::uint32_t profileMask() const noexcept {
        return profileMask_;
    }

    /// True if the context's profile mask has the core-profile bit set.
    bool isCoreProfile() const noexcept;

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
    int major_{0};
    int minor_{0};
    std::uint32_t profileMask_{0};
};

} // namespace re::core
