#pragma once

// core/load_core_gl.hpp — raw-GL anchor for loading GL entry points.
//
// Guardrail gpu_api_ownership: the raw gladLoadGL call and the glGetIntegerv
// version/profile probe live ONLY here, under core/. Context/window creators
// (utils::OffscreenContext, core::Window) call core::loadCoreGl instead of
// touching raw GL themselves.

#include <cstdint>

#include "data/result.hpp"

namespace re::core {

/// A GL proc-address getter: `void (*)(void) (*)(const char*)`, the exact
/// shape of glad's GLADloadfunc (and of GLFW's glfwGetProcAddress / EGL's
/// eglGetProcAddress).
using GlVoidFn = void (*)(void);
using GlLoadProc = GlVoidFn (*)(const char*);

/// The GL version/profile of a context, probed via glGetIntegerv (the
/// reliable path; not the glGetString text).
struct GlContextInfo {
    /// GL_MAJOR_VERSION.
    int major{};
    /// GL_MINOR_VERSION.
    int minor{};
    /// GL_CONTEXT_PROFILE_MASK.
    std::uint32_t profileMask{};

    /// True if the profile mask has the core-profile bit set.
    bool isCoreProfile() const noexcept;
};

/// Load GL entry points (glad) and probe the current context's version and
/// profile via glGetIntegerv.
///
/// Returns a typed error if `getProcAddr` is null or glad fails to load the
/// entry points. The probe values come from the integer queries, never the
/// GL_VERSION string text.
data::Result<GlContextInfo> loadCoreGl(GlLoadProc getProcAddr);

} // namespace re::core