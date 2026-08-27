#pragma once

// core/draw.hpp — deprecated shim (T2 rename DrawContext → REContext).
//
// This header is kept for backward compatibility during the V3 transition.
// New code includes <core/re_context.hpp> and uses re::core::REContext.
// Old tests and renderers that still include "core/draw.hpp" will forward to
// re_context. The canonical definition lives in core/re_context.hpp (T2).

#include "core/re_context.hpp"

// No additional content — REContext and the free-function API (setViewport,
// setClearColor, enableDepthTest, etc.) plus the DrawContext/DrawSpyCounts
// aliases are provided by re_context.hpp.
