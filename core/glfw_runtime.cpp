// core/glfw_runtime.cpp — refcounted GLFW lifecycle implementation (T15).
//
// Two owners — the visible Window (samples) and the hidden OffscreenContext
// (tests) — previously shared one process-global GLFW init/terminate pair with
// mismatched policies (Window always terminated, OffscreenContext never did).
// This caused double-terminate or leak depending on creation/destruction order.
// T15 introduces GlfwRuntime: a process-global mutex + integer reference count
// with shared_ptr token semantics (0->1 initializes, 1->0 terminates), held by
// both owners so teardown order is irrelevant and the single termination call
// lives only here.

#include "core/glfw_runtime.hpp"

#include <mutex>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace re::core {

namespace {

std::mutex& runtimeMutex() {
    static std::mutex m;
    return m;
}

int& runtimeRefs() {
    static int r = 0;
    return r;
}

} // namespace

int GlfwRuntime::refCount() noexcept {
    std::lock_guard<std::mutex> lock(runtimeMutex());
    return runtimeRefs();
}

std::shared_ptr<GlfwRuntime> GlfwRuntime::acquire() {
    std::lock_guard<std::mutex> lock(runtimeMutex());
    if (runtimeRefs() == 0) {
        if (glfwInit() != GLFW_TRUE) {
            return nullptr;
        }
    }
    ++runtimeRefs();
    // Use raw new so the shared_ptr control block destruction invokes
    // ~GlfwRuntime, which handles the 1->0 shutdown. No custom deleter
    // needed; the destructor is the sole shutdown site.
    return std::shared_ptr<GlfwRuntime>(new GlfwRuntime());
}

GlfwRuntime::~GlfwRuntime() noexcept {
    std::lock_guard<std::mutex> lock(runtimeMutex());
    --runtimeRefs();
    if (runtimeRefs() == 0) {
        glfwTerminate();
    }
}

} // namespace re::core
