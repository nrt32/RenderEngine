#pragma once

// core/window.hpp — visible GLFW window + GL 4.6 core context (SPEC §3, T12).
//
// The sample harness (app/, T12) needs a *visible* window and a GL 4.6 core
// context to render the mesh/plane/volume samples into. The offscreen
// fixture (utils/offscreen_context.hpp) deliberately creates a hidden window,
// so this module provides the visible-window counterpart. It is a core/
// component: it is the SOLE owner of the raw context-creation calls for the
// interactive sample path, with the raw GL-loader anchor (core::loadCoreGl)
// shared with utils/ (guardrail gpu_api_ownership). app/ and the samples
// consume it only through this wrapper.
//
// The created context is made current on the constructing thread and stays
// current for the lifetime of the object; render/, app/ draw through core/
// wrappers and the core::Draw API.
//
// Framebuffer-size events (T23): the window registers the GLFW
// framebuffer-size callback at creation. Every event overwrites the stored
// physical pixel size and latches a dirty flag the sample harness consumes
// once per frame, so samples re-derive camera aspect from the LIVE size
// instead of compile-time constants (a window resize reframes geometry rather
// than stretching it). The bookkeeping lives in a small shared state block so
// the registration survives Window moves by construction.

#include <cstdint>
#include <memory>
#include <string>

#include "data/result.hpp"

struct GLFWwindow;

namespace re::core {

/// Framebuffer-size bookkeeping shared between the GLFW event callback and
/// its owning Window: the latest physical pixel size plus a dirty latch.
///
/// This is the whole state machine behind the resize path, kept as a plain
/// value type so it is unit-testable without a display: `apply` mirrors one
/// GLFW event exactly (overwrite both dims + set the flag), `consumeResized`
/// is the harness's return-and-clear read (one delivery per event batch; a
/// later event latches again). A fresh state starts at 0x0 with no pending
/// resize — those are the explainable initial constants.
struct FramebufferSizeState {
    /// Latest framebuffer width in physical pixels (0 before the first event).
    int width{0};
    /// Latest framebuffer height in physical pixels (0 before the first event).
    int height{0};
    /// True while an apply() landed since the last consumeResized().
    bool resized{false};

    /// Record one framebuffer-size event: overwrite the stored pixel size and
    /// latch the dirty flag (idempotent per distinct size, re-latching after
    /// every consume — exactly GLFW's per-event semantics).
    void apply(int newWidth, int newHeight) noexcept {
        width = newWidth;
        height = newHeight;
        resized = true;
    }

    /// Return-and-clear the dirty latch: true exactly once per event batch,
    /// false again until the next apply() (the "no duplicate delivery"
    /// invariant the harness run loop relies on).
    bool consumeResized() noexcept {
        const bool pending = resized;
        resized = false;
        return pending;
    }
};

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

    /// The window's client area width in pixels (live: updated by every
    /// framebuffer-size event, so between polls it is the CURRENT size).
    int width() const noexcept {
        return fbSize_ ? fbSize_->width : 0;
    }

    /// The window's client area height in pixels (live: updated by every
    /// framebuffer-size event, so between polls it is the CURRENT size).
    int height() const noexcept {
        return fbSize_ ? fbSize_->height : 0;
    }

    /// True iff a framebuffer-size event arrived since the last call, clearing
    /// the latch — the one-shot delivery check the sample harness runs once per
    /// frame after pollEvents. Always false on a moved-from Window.
    bool consumeFramebufferResized() noexcept;

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
    explicit Window(GLFWwindow* window) noexcept;

    /// The static GLFW trampoline: forwards one framebuffer-size event into
    /// the shared state block (retrieved via the window user pointer).
    static void onFramebufferSize_(GLFWwindow* window, int newWidth,
                                   int newHeight) noexcept;

    void release() noexcept;

    GLFWwindow* window_{nullptr};
    int major_{0};
    int minor_{0};
    /// Shared framebuffer-size bookkeeping — the LIVE pixel size behind
    /// width()/height() and the dirty latch behind consumeFramebufferResized().
    /// The GLFW callback's user pointer targets THIS BLOCK (not the Window
    /// object), so its address is stable while the GLFWwindow lives even
    /// though C++ Window instances move — moves just re-seat the shared_ptr,
    /// never dangling the registration.
    std::shared_ptr<FramebufferSizeState> fbSize_;
};

} // namespace re::core
