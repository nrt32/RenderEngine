// core/window.cpp — visible GLFW window + GL 4.6 core context implementation.

#include "core/window.hpp"

#include <cstdint>
#include <string>
#include <utility>

// GLFW is the windowing / context-creation backend.
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include "core/load_core_gl.hpp"

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
      fbSize_(std::move(other.fbSize_)) {
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
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
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
        glfwMakeContextCurrent(window_);
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
    if (glfwInit() != GLFW_TRUE) {
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
        glfwTerminate();
        return data::makeError<Window>(
            2, std::string("window: glfwCreateWindow failed: ") +
                   (desc != nullptr ? desc : "unknown"));
    }

    glfwMakeContextCurrent(window);

    Window win(window);

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
        glfwTerminate();
        win.window_ = nullptr;
        return data::makeError<Window>(3, loaded.error().message);
    }
    win.major_ = loaded->major;
    win.minor_ = loaded->minor;

    spdlog::info("window: {}x{} (framebuffer {}x{}) GL {}.{} core", width,
                 height, win.width(), win.height(), win.major_, win.minor_);
    return data::makeValue<Window>(std::move(win));
}

} // namespace re::core
