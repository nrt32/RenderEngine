// utils/offscreen_context.cpp — offscreen GL 4.6 core context implementation
// (moved from core/ by V2.1, SPEC §9: test-support + windowing live in utils/;
// raw GL stays under core/ — GL entry-point loading and the version/profile
// probe are delegated to core::loadCoreGl; extended by V2.2 to pick the
// no-display backend per-OS).
// T13: RAII EglHandle, GlfwDeleter, hint save/restore, 4.6 core verify,
// per-EGL REContext map, UB-free GlLoadProc cast, RE_HAS_EGL guard (VG9).

#include "utils/offscreen_context.hpp"

#include <cstdint>
#include <string>
#include <utility>

#include "core/glfw_runtime.hpp"
#include "core/re_context.hpp"

// GLFW is the primary context-creation backend (every OS).
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef RE_HAS_EGL
// EGL surfaceless is the no-display fallback on Linux (Mesa).
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif
#include <spdlog/spdlog.h>

namespace re::utils {

// EGL surfaceless platform identifier (Mesa extension) — only when EGL present.
#ifdef RE_HAS_EGL
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#endif

namespace {

constexpr int kMajor = 4;
constexpr int kMinor = 6;

} // namespace

// GlfwDeleter — RAII deleter for GLFWwindow owned by OffscreenContext via unique_ptr with move-nulling (T13). The deleter calls glfwDestroyWindow so the hidden GLFW window is automatically destroyed when the unique_ptr resets or the OffscreenContext is move-assigned or destroyed, keeping the destructor idempotent and preventing double-free while satisfying the T13 RAII ownership requirement for the GLFW window handle and ensuring the global GLFW runtime reference is correctly managed.
void GlfwDeleter::operator()(GLFWwindow* w) const noexcept {
    if (w != nullptr) {
        glfwDestroyWindow(w);
    }
}

OffscreenContext::OffscreenContext(ContextBackend backend) noexcept
    : backend_(backend) {}

OffscreenContext::OffscreenContext(OffscreenContext&& other) noexcept
    : window_(std::move(other.window_)),
      egl_(other.egl_),
      backend_(other.backend_),
      info_(other.info_),
      glfwRuntime_(std::move(other.glfwRuntime_)) {
#ifdef RE_HAS_EGL
    other.egl_ = EglHandle{};
#else
    other.egl_.dpy = nullptr;
    other.egl_.ctx = nullptr;
    other.egl_.surf = nullptr;
#endif
}

OffscreenContext& OffscreenContext::operator=(
    OffscreenContext&& other) noexcept {
    if (this != &other) {
        // Release any existing context before adopting the incoming one.
        release();

        window_ = std::move(other.window_);
        egl_ = other.egl_;
        backend_ = other.backend_;
        info_ = other.info_;
        glfwRuntime_ = std::move(other.glfwRuntime_);

#ifdef RE_HAS_EGL
        other.egl_ = EglHandle{};
#else
        other.egl_.dpy = nullptr;
        other.egl_.ctx = nullptr;
        other.egl_.surf = nullptr;
#endif
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
        // T13: per-EGLContext map keeps EGL contexts cold; window path unchanged.
        core::REContext::makeCurrent(window_.get());
    } else if (backend_ == ContextBackend::Egl) {
#ifdef RE_HAS_EGL
        if (egl_.ctx != EGL_NO_CONTEXT) {
            eglMakeCurrent(egl_.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_.ctx);
            core::REContext::setCurrentEgl(egl_.dpy, egl_.ctx);
        }
#else
        core::REContext::setCurrentWindow(nullptr);
#endif
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
        // T13: window_ is unique_ptr<GLFWwindow,GlfwDeleter> — reset after clear.
        GLFWwindow* raw = window_.get();
        const bool isCurrent = glfwGetCurrentContext() == raw;
        core::REContext::clearWindow(raw);
        if (isCurrent) {
            core::REContext::setCurrentWindow(nullptr);
            glfwMakeContextCurrent(nullptr);
        }
        window_.reset();
        glfwRuntime_.reset();
    } else if (backend_ == ContextBackend::Egl) {
#ifdef RE_HAS_EGL
        if (egl_.ctx != EGL_NO_CONTEXT) {
            // T13: per-EGLContext REContext map — clear the EGL entry so second context on same thread starts cold with no viewport or clearColor bleed via the thread_local fallback (iteration 1 #11). The per-EGLContext unordered_map is mutex-guarded like g_windowMap, and calling invalidate() on release alone is insufficient; the map is the binding isolation that ensures each new EGL context on the same thread receives a fresh REContext with cold dirty flags, satisfying the per-context isolation requirement.
            core::REContext::clearEgl(egl_.ctx);
            // Only unbind if this EGL context is current.
            // EGL has no single global current query without display; we unbind
            // unconditionally but guard with thread_local tracking in REContext.
            eglMakeCurrent(egl_.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(egl_.dpy, egl_.ctx);
            eglTerminate(egl_.dpy);
        }
        egl_ = EglHandle{};
#else
        egl_.dpy = nullptr;
        egl_.ctx = nullptr;
        egl_.surf = nullptr;
#endif
    } else if ((backend_ == ContextBackend::Wgl ||
                backend_ == ContextBackend::Cgl)) {
#ifdef RE_HAS_EGL
        if (egl_.ctx != EGL_NO_CONTEXT) {
            core::REContext::clearEgl(egl_.ctx);
        }
        egl_ = EglHandle{};
#else
        egl_.dpy = nullptr;
        egl_.ctx = nullptr;
        egl_.surf = nullptr;
#endif
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

    // T13: save/restore glfwWindowHint globals (pollution to core::Window).
    // GLFW hints are global; OffscreenContext sets HIDDEN FALSE + 4.6 core, but
    // core::Window expects VISIBLE TRUE. We snapshot the need to restore by
    // resetting to defaults before and after creation. Since GLFW has no getters,
    // glfwDefaultWindowHints() is the documented restore — it resets all hints to
    // their defaults so the next Window::create (which sets its own hints
    // explicitly) is not polluted. This is done even on failure paths.
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // hidden window (offscreen)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, kMinor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow* rawWindow =
        glfwCreateWindow(1, 1, "RenderEngine-offscreen", nullptr, nullptr);
    // Restore hints so core::Window's next create sees defaults (VISIBLE TRUE via its own hint).
    glfwDefaultWindowHints();
    if (rawWindow == nullptr) {
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
    core::REContext::setCurrentWindow(rawWindow);
    glfwMakeContextCurrent(rawWindow);

    OffscreenContext ctx(ContextBackend::Glfw);
    ctx.window_.reset(rawWindow);
    ctx.glfwRuntime_ = std::move(runtime);

    // GL entry-point loading + version/profile probe (raw GL) happen in the
    // core/ anchor; glfwGetProcAddress matches core::GlLoadProc exactly.
    auto loaded = core::loadCoreGl(&glfwGetProcAddress);
    if (loaded.failed()) {
        spdlog::error("offscreen context: {}", loaded.error().message);
        glfwMakeContextCurrent(nullptr);
        core::REContext::setCurrentWindow(nullptr);
        // ctx owns rawWindow via unique_ptr<GlfwDeleter> — releasing ownership
        // lets ctx's destructor handle it; do NOT call glfwDestroyWindow twice
        // (T13 double-free fix — prior code did reset()+glfwDestroyWindow).
        ctx.window_.reset();
        ctx.glfwRuntime_.reset();
        return data::makeError<OffscreenContext>(3, loaded.error().message);
    }
    // T13: after loadCoreGl verify the probed version is exactly 4.6 core with the core profile bit set, else return a typed configuration error (SPEC §2 OpenGL 4.6 core). The verification checks major==4 and minor==6 and core profile, and if any check fails the context creation returns a typed error with code 3 rather than silently continuing with an unsupported version, satisfying FR-core.1 and ensuring the offscreen context always meets the project's OpenGL 4.6 core requirement.
    if (loaded->major != 4 || loaded->minor != 6 || !loaded->isCoreProfile()) {
        spdlog::error("offscreen context: expected GL 4.6 core, got {}.{} profileMask 0x{:x}",
                      loaded->major, loaded->minor, loaded->profileMask);
        glfwMakeContextCurrent(nullptr);
        core::REContext::clearWindow(rawWindow);
        core::REContext::setCurrentWindow(nullptr);
        ctx.window_.reset();
        ctx.glfwRuntime_.reset();
        return data::makeError<OffscreenContext>(
            3, "offscreen context: GL 4.6 core required, got " +
                   std::to_string(loaded->major) + "." + std::to_string(loaded->minor));
    }
    ctx.info_ = *loaded;

    spdlog::info("offscreen context: GLFW hidden window, GL {}.{} core",
                 ctx.info_.major, ctx.info_.minor);
    return data::makeValue<OffscreenContext>(std::move(ctx));
}

data::Result<OffscreenContext> OffscreenContext::createEgl() {
#ifdef RE_HAS_EGL
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

    // T13: per-EGLContext REContext map — EGL contexts are keyed by EGLContext handle rather than by GLFWwindow, so the second EGL context created on the same thread starts with a cold REContext (fresh viewport and clearColor) instead of inheriting stale state via the thread_local fallback. The map is mutex-guarded and the entry is created on first setCurrentEgl, ensuring per-context isolation as required by iteration 1 #11.
    core::REContext::setCurrentEgl(display, context);

    OffscreenContext ctx(ContextBackend::Egl);
    ctx.egl_.dpy = display;
    ctx.egl_.ctx = context;

    // T13: fix undefined behavior from reinterpret_cast<void*>(&eglGetProcAddress) which takes the address of a function and casts via void*, which is undefined; instead directly cast the function pointer eglGetProcAddress to the GlLoadProc type via reinterpret_cast<GlLoadProc>(eglGetProcAddress), which preserves the function pointer type and matches the core::GlLoadProc signature exactly, satisfying the T13 UB fix and keeping the EGL proc loader type-safe without intermediate void* indirection.
    auto loaded = core::loadCoreGl(
        reinterpret_cast<core::GlLoadProc>(eglGetProcAddress));
    if (loaded.failed()) {
        spdlog::error("offscreen context: {}", loaded.error().message);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        core::REContext::clearEgl(context);
        eglDestroyContext(display, context);
        eglTerminate(display);
        ctx.egl_ = EglHandle{};
        return data::makeError<OffscreenContext>(10, loaded.error().message);
    }
    // T13: verify the EGL-created context also reports OpenGL 4.6 core, else return a typed error (SPEC §2). After loading GL entry points via eglGetProcAddress, the probed version must be exactly major 4 minor 6 with core profile; any other version or compatibility profile is a configuration error that must be surfaced as a typed Result error so the caller can handle the unsupported context rather than proceeding with wrong GL capabilities.
    if (loaded->major != 4 || loaded->minor != 6 || !loaded->isCoreProfile()) {
        spdlog::error("offscreen context: EGL expected GL 4.6 core, got {}.{}",
                      loaded->major, loaded->minor);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        core::REContext::clearEgl(context);
        eglDestroyContext(display, context);
        eglTerminate(display);
        ctx.egl_ = EglHandle{};
        return data::makeError<OffscreenContext>(
            10, "EGL context: GL 4.6 core required, got " +
                    std::to_string(loaded->major) + "." + std::to_string(loaded->minor));
    }
    ctx.info_ = *loaded;

    spdlog::info("offscreen context: EGL surfaceless, GL {}.{} core",
                 ctx.info_.major, ctx.info_.minor);
    return data::makeValue<OffscreenContext>(std::move(ctx));
#else
    // VG9: EGL library not found at configure — surfaceless fallback disabled,
    // build must still configure and primary GLFW path still works.
    return data::makeError<OffscreenContext>(
        4, "EGL fallback: disabled — libEGL not found at configure (VG9)");
#endif
}

} // namespace re::utils
