// core/window.cpp — visible GLFW window + GL 4.6 core context implementation.

#include "core/window.hpp"

#include <cstdint>
#include <string>
#include <utility>

#include "core/glfw_runtime.hpp"
#include "core/load_core_gl.hpp"
#include "core/re_context.hpp"

// GLFW is the windowing / context-creation backend.
// Define GLFW_INCLUDE_NONE so GLFW does not include GL/gl.h (conflicts with glad).
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

namespace re::core {

namespace {

constexpr int kMajor = 4;
constexpr int kMinor = 6;

} // namespace

Window::Window(GLFWwindow* window) noexcept : window_(window) {}

Window::Window(Window&& other) noexcept
    : window_(other.window_),
      major_(other.major_),
      minor_(other.minor_),
      fbSize_(std::move(other.fbSize_)),
      glfwRuntime_(std::move(other.glfwRuntime_)) {
    other.window_ = nullptr;
    other.major_ = 0;
    other.minor_ = 0;
    // fbSize_ was MOVED out of `other`: the source reads as a moved-from
    // Window (width/height 0, no pending resize) while this instance now
    // co-owns the same state block — which is exactly what keeps the GLFW
    // callback registration valid across moves (the user pointer targets the
    // state block, whose address never changes).
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        release();
        window_ = other.window_;
        major_ = other.major_;
        minor_ = other.minor_;
        fbSize_ = std::move(other.fbSize_);
        glfwRuntime_ = std::move(other.glfwRuntime_);
        other.window_ = nullptr;
        other.major_ = 0;
        other.minor_ = 0;
    }
    return *this;
}

Window::~Window() {
    release();
}

void Window::release() noexcept {
    if (window_ != nullptr) {
        // T2: clear per-GL-context REContext mirror before destroying the window.
        // Each GLFWwindow* maps to its own REContextState (viewport, clearColor,
        // depthTest, blend, blendFunc, cull, FBO/VAO/program/image units); clearing
        // here prevents stale map entries and ensures a later window with the same
        // address gets a cold cache (no cross-context bleed). Worker threads with
        // private contexts get private mirrors via thread_local — no lock on the
        // per-frame path. Only clear the current GL context and REContext if this
        // window is the one currently bound (otherwise we would unbind a different
        // window's context that was restored before this destructor ran).
        // T15: the process-global GLFW lifecycle is refcounted via
        // core::GlfwRuntime — the first holder initializes, the last token
        // destruction shuts down, so teardown order of Window and
        // OffscreenContext is irrelevant and no direct termination call lives
        // here.
        const bool isCurrent = glfwGetCurrentContext() == window_;
        REContext::clearWindow(window_);
        if (isCurrent) {
            glfwMakeContextCurrent(nullptr);
            // Also clear the thread_local current if it pointed at this window's state
            // (fallback will be used until next makeCurrent).
            REContext::setCurrentWindow(nullptr);
        }
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwRuntime_.reset();
    }
}

bool Window::shouldClose() const noexcept {
    return window_ == nullptr || glfwWindowShouldClose(window_) == GLFW_TRUE;
}

void Window::pollEvents() noexcept {
    if (window_ != nullptr) {
        glfwPollEvents();
    }
}

void Window::swapBuffers() noexcept {
    if (window_ != nullptr) {
        glfwSwapBuffers(window_);
    }
}

void Window::requestClose() noexcept {
    if (window_ != nullptr) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

void Window::makeContextCurrent() const noexcept {
    if (window_ != nullptr) {
        // T2: global per-GL-context REContext — thread_local current() follows
        // the GLFW window that is made current. The mapping GLFWwindow* →
        // REContextState keeps viewport, clearColor, depthTest, blend, blendFunc,
        // cull, FBO/VAO/program/image units per context. No auto-guess; the
        // caller must make the context current before issuing draw calls, and
        // invalidation at boundaries (SampleHarness post-ImGui, invalidate())
        // is explicit.
        REContext::makeCurrent(window_);
    }
}

bool Window::consumeFramebufferResized() noexcept {
    return fbSize_ ? fbSize_->consumeResized() : false;
}

void Window::onFramebufferSize_(GLFWwindow* window, int newWidth,
                                int newHeight) noexcept {
    // The user pointer targets the shared state block (not the Window
    // object), so this trampoline stays correct for the whole life of the
    // GLFWwindow regardless of how the C++ Window wrapper moves. A null user
    // pointer can only mean a foreign/legacy window — ignore silently rather
    // than guess at foreign state.
    auto* state =
        static_cast<FramebufferSizeState*>(glfwGetWindowUserPointer(window));
    if (state != nullptr) {
        state->apply(newWidth, newHeight);
    }
}

data::Result<Window> Window::create(int width, int height,
                                    const std::string& title) {
    auto runtime = GlfwRuntime::acquire();
    if (runtime == nullptr) {
        return data::makeError<Window>(1, "window: glfwInit failed");
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE); // visible window for samples
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, kMinor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window =
        glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (window == nullptr) {
        const char* desc = nullptr;
        glfwGetError(&desc);
        return data::makeError<Window>(
            2, std::string("window: glfwCreateWindow failed: ") +
                   (desc != nullptr ? desc : "unknown"));
    }

    // T2: set REContext current to this window's mirror before loading GL.
    // Each GLFWwindow* owns its own state (viewport etc.) via the
    // REContext::current() thread_local mapping; worker threads get private
    // mirrors with no lock. loadCoreGl() will also sync via glfwGetCurrentContext
    // once glad is loaded, but we seed it here so pre-load GL calls are tracked.
    REContext::setCurrentWindow(window);
    glfwMakeContextCurrent(window);

    Window win(window);
    win.glfwRuntime_ = std::move(runtime);

    // Framebuffer-size bookkeeping (T23): seed the state with the REAL
    // physical pixel size (the framebuffer size, not the window coordinate
    // size — they differ under display scaling), then register the callback
    // so every later event overwrites the stored size and latches the dirty
    // flag the harness consumes. The user pointer targets the shared state
    // block, so the registration survives Window moves by construction.
    win.fbSize_ = std::make_shared<FramebufferSizeState>();
    int fbWidth = width;
    int fbHeight = height;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    if (fbWidth > 0 && fbHeight > 0) {
        *win.fbSize_ = FramebufferSizeState{fbWidth, fbHeight, false};
    } else {
        *win.fbSize_ = FramebufferSizeState{width, height, false};
    }
    glfwSetWindowUserPointer(window, win.fbSize_.get());
    glfwSetFramebufferSizeCallback(window, &Window::onFramebufferSize_);

    // GL entry-point loading + version probe (raw GL) happen in the core/
    // anchor; glfwGetProcAddress matches core::GlLoadProc exactly.
    auto loaded = core::loadCoreGl(&glfwGetProcAddress);
    if (loaded.failed()) {
        spdlog::error("window: {}", loaded.error().message);
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window);
        win.window_ = nullptr;
        win.glfwRuntime_.reset();
        return data::makeError<Window>(3, loaded.error().message);
    }
    win.major_ = loaded->major;
    win.minor_ = loaded->minor;

    spdlog::info("window: {}x{} (framebuffer {}x{}) GL {}.{} core", width,
                 height, win.width(), win.height(), win.major_, win.minor_);
    return data::makeValue<Window>(std::move(win));
}

} // namespace re::core
