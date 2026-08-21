// core/window.cpp — visible GLFW window + GL 4.6 core context implementation.

#include "core/window.hpp"

#include <cstdint>
#include <string>
#include <utility>

// glad2 GL 4.6 core loader (generated; raw GL entry points live under core/).
#include <glad/gl.h>

// GLFW is the windowing / context-creation backend.
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

namespace re::core {

namespace {

constexpr int kMajor = 4;
constexpr int kMinor = 6;

/// Load GL entry points and query the context's version via glGetIntegerv
/// (the reliable path, matching core/offscreen_context.cpp).
data::Result<void> loadAndProbeGl(GLADloadfunc getProcAddr, int* outMajor,
                                  int* outMinor) {
    if (getProcAddr == nullptr) {
        return data::makeError<void>(1, "window: no GL proc-address function");
    }
    if (gladLoadGL(getProcAddr) == 0) {
        return data::makeError<void>(
            2, "window: glad failed to load GL entry points");
    }

    std::int32_t major = 0;
    std::int32_t minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    *outMajor = static_cast<int>(major);
    *outMinor = static_cast<int>(minor);
    return data::Result<void>(data::value);
}

} // namespace

Window::Window(GLFWwindow* window, int width, int height) noexcept
    : window_(window), width_(width), height_(height) {}

Window::Window(Window&& other) noexcept
    : window_(other.window_),
      width_(other.width_),
      height_(other.height_),
      major_(other.major_),
      minor_(other.minor_) {
    other.window_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
    other.major_ = 0;
    other.minor_ = 0;
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        release();
        window_ = other.window_;
        width_ = other.width_;
        height_ = other.height_;
        major_ = other.major_;
        minor_ = other.minor_;
        other.window_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
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

    Window win(window, width, height);

    auto load = loadAndProbeGl(&glfwGetProcAddress, &win.major_, &win.minor_);
    if (load.failed()) {
        spdlog::error("window: {}", load.error().message);
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        win.window_ = nullptr;
        return data::makeError<Window>(3, load.error().message);
    }

    spdlog::info("window: {}x{} GL {}.{} core", width, height, win.major_,
                 win.minor_);
    return data::makeValue<Window>(std::move(win));
}

} // namespace re::core
