#pragma once

// core/glfw_runtime.hpp — refcounted GLFW process-global lifecycle (T15).
//
// Two owners share one process-global GLFW init/terminate pair with mismatched
// policies: the visible Window (always terminated) and the hidden
// OffscreenContext used by tests (never terminated). Both now hold a
// shared_ptr token from GlfwRuntime::acquire(). The first acquisition
// initializes the library, the last token destruction shuts it down. The
// design is explicitly scoped to the single-process, single-init model
// required by GLFW: a process-global mutex protects the integer reference
// count, acquire returns a shared_ptr token whose deleter drives the
// 0->1 and 1->0 transitions. Order-independent teardown (Window and
// OffscreenContext created/destroyed in either order) leaves the count at
// zero with no double init or missed termination. This replaces the prior
// raw init/terminate calls in window.cpp and offscreen_context.cpp, so
// outside glfw_runtime.* no translation unit contains a termination call.

#include <memory>

namespace re::core {

/// Refcounted owner of the process-global GLFW lifecycle.
///
/// Acquire a token via `acquire()`. The first token initializes GLFW, the
/// last token's destruction shuts it down. Tokens are `shared_ptr` so
/// `Window` and `OffscreenContext` can each hold one and teardown in any
/// order. Thread-safe via an internal mutex.
class GlfwRuntime {
   public:
    GlfwRuntime(const GlfwRuntime&) = delete;
    GlfwRuntime& operator=(const GlfwRuntime&) = delete;
    GlfwRuntime(GlfwRuntime&&) = delete;
    GlfwRuntime& operator=(GlfwRuntime&&) = delete;

    /// Acquire a reference. On 0->1 the library is initialized. Returns
    /// `nullptr` if initialization fails (typed error is produced by the
    /// caller). Each successful call increments the count by one; the
    /// returned `shared_ptr` holds one reference and drives 1->0 shutdown
    /// when destroyed.
    static std::shared_ptr<GlfwRuntime> acquire();

    /// Current reference count (for tests; 0 when no Window or
    /// OffscreenContext is alive). Thread-safe.
    static int refCount() noexcept;

    /// Destructor drives the 1->0 transition when the last token is
    /// destroyed. Public so `shared_ptr` can delete it; do not construct
    /// directly — use `acquire()`.
    ~GlfwRuntime() noexcept;

   private:
    GlfwRuntime() noexcept = default;
};

} // namespace re::core
