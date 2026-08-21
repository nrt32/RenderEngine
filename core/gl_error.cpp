// core/gl_error.cpp — GL error-state wrapper.

#include "core/gl_error.hpp"

#include <glad/gl.h>

namespace re::core {

std::uint32_t queryGlError() {
    return static_cast<std::uint32_t>(glGetError());
}

bool hasPendingGlError() {
    return glGetError() != GL_NO_ERROR;
}

} // namespace re::core
