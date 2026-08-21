#pragma once

// core/window.hpp — visible GLFW window + GL 4.6 core context (SPEC §3, T12).
//
// The sample harness (app/, T12) needs a *visible* window and a GL 4.6 core
// context to render the mesh/plane/volume samples into. The offscreen
// fixture (core/offscreen_context.hpp) deliberately creates a hidden window,
// so this module provides the visible-window counterpart. Like OffscreenContext
// it is a core/ component: it is the SOLE owner of the raw context-creation and
// GL-loader (glad) calls for the interactive sample path (guardrail
// gpu_api_ownership). app/ and the samples consume it only through this
// wrapper.
//
// The created context is made current on the constructing thread and stays
// current for the lifetime of the object; render/, app/ draw through core/
// wrappers and the core::Draw API.

#include <cstdint>
#include <string>

#include "data/result.hpp"

struct GLFWwindow;

namespace re::core {

/// RAII-managed visible OpenGL 4.6 core window (GLFW).
///
/// Creating a Window makes its GL 4.6 core context current and loads the GL
/// entry points with glad (so the core/ wrappers work). It owns the GLFW
/// window and calls glfwTerminate on destruction (safe to construct only one at
/// a time, matching the SPEC §1 "single window" non-goal).
class Window {
   public:
    /// Create a visible window of `width` x `height` pixels with a GL 4.6 core
    /// context, and load GL entry points. Returns a typed error if GLFW
    /// initialisation, window creation, or GL loading fails.
    static data::Result<Window> create(int width, int height,
                                       const std::string& title);

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    ~Window();

    /// The underlying GLFW window handle (for the ImGui GLFW backend and for
    /// making the context current).
    GLFWwindow* handle() const noexcept {
        return window_;
    }

    /// The window's client area width in pixels.
    int width() const noexcept {
        return width_;
    }

    /// The window's client area height in pixels.
    int height() const noexcept {
        return height_;
    }

    /// The context's GL major version (specifically GL_MAJOR_VERSION).
    int majorVersion() const noexcept {
        return major_;
    }

    /// The context's GL minor version (specifically GL_MINOR_VERSION).
    int minorVersion() const noexcept {
        return minor_;
    }

    /// True if the window's close flag is set (the user pressed close, or the
    /// run loop has been told to stop).
    bool shouldClose() const noexcept;

    /// Process pending window/input events (glfwPollEvents).
    void pollEvents() noexcept;

    /// Swap the window's front and back buffers (present the rendered frame).
    void swapBuffers() noexcept;

    /// Request that the run loop stop (sets the GLFW close flag).
    void requestClose() noexcept;

    /// Make this window's GL context current on the calling thread.
    void makeContextCurrent() const noexcept;

   private:
    explicit Window(GLFWwindow* window, int width, int height) noexcept;

    void release() noexcept;

    GLFWwindow* window_{nullptr};
    int width_{0};
    int height_{0};
    int major_{0};
    int minor_{0};
};

} // namespace re::core
