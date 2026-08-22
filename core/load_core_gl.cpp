// core/load_core_gl.cpp — GL entry-point loading + version/profile probe.
//
// Raw-GL anchor (guardrail gpu_api_ownership): the raw gladLoadGL call and
// the glGetIntegerv probe live ONLY here, under core/. utils::OffscreenContext
// and core::Window call this function rather than touching raw GL themselves.

#include "core/load_core_gl.hpp"

#include <glad/gl.h>

namespace re::core {

bool GlContextInfo::isCoreProfile() const noexcept {
    return (profileMask &
            static_cast<std::uint32_t>(GL_CONTEXT_CORE_PROFILE_BIT)) != 0u;
}

data::Result<GlContextInfo> loadCoreGl(GlLoadProc getProcAddr) {
    if (getProcAddr == nullptr) {
        return data::makeError<GlContextInfo>(
            1, "loadCoreGl: no GL proc-address function");
    }
    if (gladLoadGL(getProcAddr) == 0) {
        return data::makeError<GlContextInfo>(
            2, "loadCoreGl: glad failed to load GL entry points");
    }

    std::int32_t major = 0;
    std::int32_t minor = 0;
    std::int32_t profile = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);

    GlContextInfo info;
    info.major = static_cast<int>(major);
    info.minor = static_cast<int>(minor);
    info.profileMask = static_cast<std::uint32_t>(profile);
    return data::makeValue<GlContextInfo>(info);
}

} // namespace re::core