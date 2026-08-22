// utils/offscreen_context.cpp — offscreen GL 4.6 core context implementation
// (moved from core/ by V2.1, SPEC §9: test-support + windowing live in utils/;
// raw GL stays under core/ — GL entry-point loading and the version/profile
// probe are delegated to core::loadCoreGl).

#include "utils/offscreen_context.hpp"

#include <cstdint>

// GLFW is the primary context-creation backend.
#include <GLFW/glfw3.h>

// EGL surfaceless is the no-display fallback.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <spdlog/spdlog.h>

#include <utility>

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
      info_(other.info_) {
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

        other.window_ = nullptr;
        other.eglDisplay_ = nullptr;
        other.eglContext_ = nullptr;
    }
    return *this;
}

OffscreenContext::~OffscreenContext() {
    release();
}

void OffscreenContext::release() noexcept {
    if (backend_ == ContextBackend::Glfw && window_ != nullptr) {
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window_);
        window_ = nullptr;
    } else if (backend_ == ContextBackend::Egl && eglContext_ != nullptr) {
        eglMakeCurrent(static_cast<EGLDisplay>(eglDisplay_), EGL_NO_SURFACE,
                       EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(static_cast<EGLDisplay>(eglDisplay_),
                          static_cast<EGLContext>(eglContext_));
        eglTerminate(static_cast<EGLDisplay>(eglDisplay_));
        eglDisplay_ = nullptr;
        eglContext_ = nullptr;
    }
}

data::Result<OffscreenContext> OffscreenContext::create() {
    // Prefer GLFW; fall back to EGL-surfaceless on failure (e.g. no display).
    auto glfwResult = createGlfw();
    if (glfwResult.ok()) {
        return glfwResult;
    }
    spdlog::warn(
        "offscreen context: GLFW backend failed ({}); trying EGL-surfaceless",
        glfwResult.error().message);
    return createEgl();
}

data::Result<OffscreenContext> OffscreenContext::createGlfw() {
    if (glfwInit() != GLFW_TRUE) {
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
        glfwTerminate();
        return data::makeError<OffscreenContext>(
            2, std::string("glfwCreateWindow failed: ") +
                   (desc != nullptr ? desc : "unknown"));
    }

    glfwMakeContextCurrent(window);

    OffscreenContext ctx(ContextBackend::Glfw);
    ctx.window_ = window;

    // GL entry-point loading + version/profile probe (raw GL) happen in the
    // core/ anchor; glfwGetProcAddress matches core::GlLoadProc exactly.
    auto loaded = core::loadCoreGl(&glfwGetProcAddress);
    if (loaded.failed()) {
        spdlog::error("offscreen context: {}", loaded.error().message);
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        ctx.window_ = nullptr;
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
