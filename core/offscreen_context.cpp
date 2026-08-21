// core/offscreen_context.cpp — offscreen GL 4.6 core context implementation.

#include "core/offscreen_context.hpp"

#include <cstdint>

// glad2 GL 4.6 core loader (generated; raw GL entry points live under core/).
#include <glad/gl.h>

// GLFW is the primary context-creation backend.
#include <GLFW/glfw3.h>

// EGL surfaceless is the no-display fallback.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <spdlog/spdlog.h>

#include <utility>

namespace re::core {

// EGL surfaceless platform identifier (Mesa extension).
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

namespace {

constexpr int kMajor = 4;
constexpr int kMinor = 6;

/// Load GL entry points and query the context's version/profile via
/// glGetIntegerv (the reliable path; not the glGetString(GL_VERSION) text).
data::Result<void> loadAndProbeGl(GLADloadfunc getProcAddr, int* outMajor,
                                  int* outMinor,
                                  std::uint32_t* outProfileMask) {
    if (getProcAddr == nullptr) {
        return data::makeError<void>(
            1, "offscreen context: no GL proc-address function");
    }
    if (gladLoadGL(getProcAddr) == 0) {
        return data::makeError<void>(
            2, "offscreen context: glad failed to load GL entry points");
    }

    std::int32_t major = 0;
    std::int32_t minor = 0;
    std::int32_t profile = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);

    *outMajor = static_cast<int>(major);
    *outMinor = static_cast<int>(minor);
    *outProfileMask = static_cast<std::uint32_t>(profile);
    return data::Result<void>(data::value);
}

} // namespace

OffscreenContext::OffscreenContext(ContextBackend backend) noexcept
    : backend_(backend) {}

bool OffscreenContext::isCoreProfile() const noexcept {
    return (profileMask_ &
            static_cast<std::uint32_t>(GL_CONTEXT_CORE_PROFILE_BIT)) != 0u;
}

OffscreenContext::OffscreenContext(OffscreenContext&& other) noexcept
    : window_(other.window_),
      eglDisplay_(other.eglDisplay_),
      eglContext_(other.eglContext_),
      backend_(other.backend_),
      major_(other.major_),
      minor_(other.minor_),
      profileMask_(other.profileMask_) {
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
        major_ = other.major_;
        minor_ = other.minor_;
        profileMask_ = other.profileMask_;

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

    auto load = loadAndProbeGl(&glfwGetProcAddress, &ctx.major_, &ctx.minor_,
                               &ctx.profileMask_);
    if (load.failed()) {
        spdlog::error("offscreen context: {}", load.error().message);
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        ctx.window_ = nullptr;
        return data::makeError<OffscreenContext>(3, load.error().message);
    }

    spdlog::info("offscreen context: GLFW hidden window, GL {}.{} core",
                 ctx.major_, ctx.minor_);
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

    auto load = loadAndProbeGl(reinterpret_cast<GLADloadfunc>(
                                   reinterpret_cast<void*>(&eglGetProcAddress)),
                               &ctx.major_, &ctx.minor_, &ctx.profileMask_);
    if (load.failed()) {
        spdlog::error("offscreen context: {}", load.error().message);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context);
        eglTerminate(display);
        ctx.eglDisplay_ = nullptr;
        ctx.eglContext_ = nullptr;
        return data::makeError<OffscreenContext>(10, load.error().message);
    }

    spdlog::info("offscreen context: EGL surfaceless, GL {}.{} core",
                 ctx.major_, ctx.minor_);
    return data::makeValue<OffscreenContext>(std::move(ctx));
}

} // namespace re::core
