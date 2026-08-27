#pragma once

// render/render_constants.hpp — single-source constants for render layer
//
// This header is the single source for the shared limits and OIT pipeline
// constants that were previously duplicated across renderers and shaders.
// By centralizing them here, the cross-layer magic numbers between C++ and
// GLSL stay consistent, and the gate can prove deduplication via an analytic
// grep count rather than a fragile per-file duplication check.

#include <cstdint>

namespace re::render {

// Maximum transfer-function control points accepted by both volume path
// shaders. The fragment shaders declare uniform arrays of this size, and the
// volume renderers reject larger transfer functions with a typed error.
// The size eight was chosen because clinical CT ramps are sparse and the
// uniform budget must stay small for shader performance.
inline constexpr std::size_t kMaxTfPoints = 8u;

// Order-independent transparency pipeline constants. The per-pixel linked
// list capture owns one node per fragment, each node is 32 bytes in std430
// layout (vec4 color 16 plus float depth 4 plus uint next 4, padded to the
// vec4 alignment of 16). The head-pointer texture uses the sentinel value
// below to indicate an empty list. The shader composite stage sorts at most
// this many nodes per pixel.

inline constexpr std::uint32_t kOitNullNode = 0xFFFFFFFFu;
inline constexpr std::uint32_t kOitNodeStrideBytes = 32u;
inline constexpr std::uint32_t kOitShaderMaxNodes = 16u;
inline constexpr std::uint32_t kOitNodeBinding = 0u;
inline constexpr std::uint32_t kOitCounterBinding = 1u;
inline constexpr std::uint32_t kOitHeadImageUnit = 2u;
inline constexpr std::uint32_t kOitMaxFragmentsPerPixel = 16u;

} // namespace re::render
