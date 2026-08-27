// utils/offscreen_context.cpp — offscreen GL 4.6 core context implementation
// (moved from core/ by V2.1, SPEC §9: test-support + windowing live in utils/;
// raw GL stays under core/ — GL entry-point loading and the version/profile
// probe are delegated to core::loadCoreGl; extended by V2.2 to pick the
// no-display backend per-OS).

#include "utils/offscreen_context.hpp"

#include <cstdint>
#include <string>
#include <utility>

#include "core/glfw_runtime.hpp"
#include "core/re_context.hpp"

// GLFW is the primary context-creation backend (every OS).
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// EGL surfaceless is the no-display fallback on Linux (Mesa).
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <spdlog/spdlog.h>

namespace re::utils {

// EGL surfaceless platform identifier (Mesa extension).
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

namespace {

constexpr int kMajor = 4;
constexpr int kMinor = 6;

} // namespace

OffscreenContext::OffscreenContext(ContextBackend backend) noexcept
    : backend_(backend) {}

OffscreenContext::OffscreenContext(OffscreenContext&& other) noexcept
    : window_(other.window_),
      eglDisplay_(other.eglDisplay_),
      eglContext_(other.eglContext_),
      backend_(other.backend_),
      info_(other.info_),
      glfwRuntime_(std::move(other.glfwRuntime_)) {
    other.window_ = nullptr;
    other.eglDisplay_ = nullptr;
    other.eglContext_ = nullptr;
}

OffscreenContext& OffscreenContext::operator=(
    OffscreenContext&& other) noexcept {
    if (this != &other) {
        // Release any existing context before adopting the incoming one.
        release();

        window_ = other.window_;
        eglDisplay_ = other.eglDisplay_;
        eglContext_ = other.eglContext_;
        backend_ = other.backend_;
        info_ = other.info_;
        glfwRuntime_ = std::move(other.glfwRuntime_);

        other.window_ = nullptr;
        other.eglDisplay_ = nullptr;
        other.eglContext_ = nullptr;
    }
    return *this;
}

OffscreenContext::~OffscreenContext() {
    release();
}

const char* OffscreenContext::backendName(ContextBackend backend) noexcept {
    switch (backend) {
        case ContextBackend::Glfw:
            return "GLFW";
        case ContextBackend::Egl:
            return "EGL";
        case ContextBackend::Wgl:
            return "WGL";
        case ContextBackend::Cgl:
            return "CGL";
        default:
            return "unknown";
    }
}

void OffscreenContext::makeCurrent() const noexcept {
    if (backend_ == ContextBackend::Glfw && window_ != nullptr) {
        // T2: global per-GL-context REContext — thread_local current follows the
        // GLFW window that is made current (GLFWwindow handle → REContextState
        // mirror holding viewport, clearColor, depthTest, blend, blendFunc, cull,
        // FBO/VAO/program/image units). Each window owns its own state; worker
        // threads with private contexts get private mirrors with no lock; shared
        // resources out-of-scope (GL share groups) — explicit invalidation at
        // boundaries, no auto-guess.
        core::REContext::makeCurrent(window_);
    } else if (backend_ == ContextBackend::Egl && eglContext_ != nullptr) {
        eglMakeCurrent(static_cast<EGLDisplay>(eglDisplay_), EGL_NO_SURFACE,
                       EGL_NO_SURFACE, static_cast<EGLContext>(eglContext_));
        core::REContext::setCurrentWindow(nullptr);
    }
}

void OffscreenContext::release() noexcept {
    if (backend_ == ContextBackend::Glfw && window_ != nullptr) {
        // T2: clear per-GL-context REContext mirror for this window before destroy.
        // Each GLFWwindow* maps to its own REContextState (viewport etc.); clearing
        // prevents stale entries and ensures a later window at same address is cold.
        // Only clear the current GL context and REContext thread_local if this
        // window is the one currently bound — otherwise we would unbind the
        // fixture's context that another test restored before this destructor ran.
        // T15: GLFW lifecycle is refcounted via GlfwRuntime — the token is
        // released here so the last holder shuts down, order-independent.
        const bool isCurrent = glfwGetCurrentContext() == window_;
        core::REContext::clearWindow(window_);
        if (isCurrent) {
            core::REContext::setCurrentWindow(nullptr);
            glfwMakeContextCurrent(nullptr);
        }
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwRuntime_.reset();
    } else if (backend_ == ContextBackend::Egl && eglContext_ != nullptr) {
        eglMakeCurrent(static_cast<EGLDisplay>(eglDisplay_), EGL_NO_SURFACE,
                       EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(static_cast<EGLDisplay>(eglDisplay_),
                          static_cast<EGLContext>(eglContext_));
        eglTerminate(static_cast<EGLDisplay>(eglDisplay_));
        eglDisplay_ = nullptr;
        eglContext_ = nullptr;
    } else if ((backend_ == ContextBackend::Wgl ||
                backend_ == ContextBackend::Cgl) &&
               eglContext_ != nullptr) {
        // WGL/CGL stubs on this host allocate no native handle; clear the
        // placeholder so move semantics remain idempotent.
        eglDisplay_ = nullptr;
        eglContext_ = nullptr;
    }
}

data::Result<OffscreenContext> OffscreenContext::create() {
    // Prefer GLFW (hidden window) on every OS; the no-display fallback is
    // chosen deterministically per-OS (SPEC §9 V2.2) and must not hardcode the
    // Mesa-only `EGL_PLATFORM_SURFACELESS_MESA` path on other platforms.
    auto glfwResult = createGlfw();
    if (glfwResult.ok()) {
        return glfwResult;
    }
    spdlog::warn("offscreen context: GLFW backend failed ({}); trying {} fallback",
                 glfwResult.error().message,
                 backendName(platformNoDisplayBackend()));
    return createPlatformFallback();
}

data::Result<OffscreenContext> OffscreenContext::createPlatformFallback() {
#if defined(__APPLE__)
    spdlog::warn("offscreen context: no-display fallback is CGL (macOS)");
    return createCgl();
#elif defined(_WIN32) || defined(_WIN64)
    spdlog::warn(
        "offscreen context: no-display fallback is ANGLE-EGL/WGL (Windows)");
    // Prefer ANGLE-EGL (still EGL) where the runtime is present; fall back to
    // WGL. On this (Linux) host both will report not-available, but the
    // branching proves the selection is per-OS.
    auto angle = createEgl();
    if (angle.ok()) {
        return angle;
    }
    return createWgl();
#else
    spdlog::warn(
        "offscreen context: no-display fallback is EGL-surfaceless (Linux/Mesa)");
    return createEgl();
#endif
}

data::Result<OffscreenContext> OffscreenContext::createWgl() {
    return data::makeError<OffscreenContext>(
        11, "WGL fallback: not available on this host (Windows-only)");
}

data::Result<OffscreenContext> OffscreenContext::createCgl() {
    return data::makeError<OffscreenContext>(
        12, "CGL fallback: not available on this host (macOS-only)");
}

data::Result<OffscreenContext> OffscreenContext::createGlfw() {
    auto runtime = core::GlfwRuntime::acquire();
    if (runtime == nullptr) {
        return data::makeError<OffscreenContext>(1, "glfwInit failed");
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // hidden window (offscreen)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, kMinor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow* window =
        glfwCreateWindow(1, 1, "RenderEngine-offscreen", nullptr, nullptr);
    if (window == nullptr) {
        const char* desc = nullptr;
        glfwGetError(&desc);
        return data::makeError<OffscreenContext>(
            2, std::string("glfwCreateWindow failed: ") +
                   (desc != nullptr ? desc : "unknown"));
    }

    // T2: global per-GL-context REContext — set thread_local current to this
    // window's mirror before any GL call. Each GLFWwindow* owns its own state
    // (viewport, clearColor, depthTest, blend, blendFunc, cull, FBO/VAO/program/
    // image units) via REContext::current() thread_local mapping; worker threads
    // with private contexts get private mirrors with no lock.
    core::REContext::setCurrentWindow(window);
    glfwMakeContextCurrent(window);

    OffscreenContext ctx(ContextBackend::Glfw);
    ctx.window_ = window;
    ctx.glfwRuntime_ = std::move(runtime);

    // GL entry-point loading + version/profile probe (raw GL) happen in the
    // core/ anchor; glfwGetProcAddress matches core::GlLoadProc exactly.
    auto loaded = core::loadCoreGl(&glfwGetProcAddress);
    if (loaded.failed()) {
        spdlog::error("offscreen context: {}", loaded.error().message);
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window);
        ctx.window_ = nullptr;
        ctx.glfwRuntime_.reset();
        return data::makeError<OffscreenContext>(3, loaded.error().message);
    }
    ctx.info_ = *loaded;

    spdlog::info("offscreen context: GLFW hidden window, GL {}.{} core",
                 ctx.info_.major, ctx.info_.minor);
    return data::makeValue<OffscreenContext>(std::move(ctx));
}

data::Result<OffscreenContext> OffscreenContext::createEgl() {
    PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (getPlatformDisplay == nullptr) {
        return data::makeError<OffscreenContext>(
            4, "EGL fallback: eglGetPlatformDisplayEXT unavailable");
    }

    EGLDisplay display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                            EGL_DEFAULT_DISPLAY, nullptr);
    if (display == EGL_NO_DISPLAY) {
        return data::makeError<OffscreenContext>(
            5, "EGL fallback: no surfaceless display");
    }

    if (eglInitialize(display, nullptr, nullptr) != EGL_TRUE) {
        return data::makeError<OffscreenContext>(
            6, "EGL fallback: eglInitialize failed");
    }

    const EGLint configAttribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                    EGL_NONE};
    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (eglChooseConfig(display, configAttribs, &config, 1, &numConfigs) !=
            EGL_TRUE ||
        numConfigs < 1) {
        eglTerminate(display);
        return data::makeError<OffscreenContext>(
            7, "EGL fallback: no matching config");
    }

    const EGLint contextAttribs[] = {EGL_CONTEXT_MAJOR_VERSION,
                                     kMajor,
                                     EGL_CONTEXT_MINOR_VERSION,
                                     kMinor,
                                     EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                     EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                     EGL_NONE};
    EGLContext context =
        eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        eglTerminate(display);
        return data::makeError<OffscreenContext>(
            8, "EGL fallback: context creation failed");
    }

    if (eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context) !=
        EGL_TRUE) {
        eglDestroyContext(display, context);
        eglTerminate(display);
        return data::makeError<OffscreenContext>(
            9, "EGL fallback: eglMakeCurrent failed");
    }

    // T2: EGL surfaceless has no GLFWwindow — use per-thread fallback REContext.
    // The global per-GL-context contract still holds: each EGL context is a
    // distinct GL context with its own mirror on this thread (thread_local).
    core::REContext::setCurrentWindow(nullptr);

    OffscreenContext ctx(ContextBackend::Egl);
    ctx.eglDisplay_ = display;
    ctx.eglContext_ = context;

    // eglGetProcAddress has the same shape as core::GlLoadProc; keep the
    // explicit conversion for clarity.
    auto loaded = core::loadCoreGl(
        reinterpret_cast<core::GlLoadProc>(
            reinterpret_cast<void*>(&eglGetProcAddress)));
    if (loaded.failed()) {
        spdlog::error("offscreen context: {}", loaded.error().message);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context);
        eglTerminate(display);
        ctx.eglDisplay_ = nullptr;
        ctx.eglContext_ = nullptr;
        return data::makeError<OffscreenContext>(10, loaded.error().message);
    }
    ctx.info_ = *loaded;

    spdlog::info("offscreen context: EGL surfaceless, GL {}.{} core",
                 ctx.info_.major, ctx.info_.minor);
    return data::makeValue<OffscreenContext>(std::move(ctx));
}

} // namespace re::utils
