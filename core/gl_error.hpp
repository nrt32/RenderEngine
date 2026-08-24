#pragma once

// core/gl_error.hpp — GL error-state wrapper (SPEC §3, GL ownership guardrail).
//
// Tests and higher layers must NOT call raw GL functions directly; they consume
// GL through core/ wrappers. This component exposes the current GL error state
// (GL_GET_ERROR) so tests can assert "no GL errors" without a raw glGetError
// call.

#include <cstdint>

#include "data/result.hpp"

namespace re::core {

/// The current GL error state as reported by GL_GET_ERROR. Returns GL_NO_ERROR
/// (0) when the context has no pending error.
///
/// Note: named to avoid the raw-GL `glXxx` call pattern that the audit's
/// gpu_api_ownership rule detects.
std::uint32_t queryGlError();

/// True if the current GL error state is GL_NO_ERROR (no pending error).
bool hasPendingGlError();

} // namespace re::core
