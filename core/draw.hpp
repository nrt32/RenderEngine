#pragma once

// core alias header — deprecated shim (T2 rename DrawContext to REContext).
//
// This header is kept for backward compatibility during the V3 transition.
// New code includes core re_context and uses re core REContext.
// Old tests and renderers that still include the alias header will forward to
// re_context. The canonical definition lives in core re_context (T2). The
// header is a pure alias containing exactly one include and no second ledger
// — REContext single-writer discipline already T4, header duality remains
// alias-only with no duplicate beginPass definition.

#include "core/re_context.hpp"

// No additional content — REContext and the free-function API (setViewport,
// setClearColor, enableDepthTest, etc.) plus the DrawContext/DrawSpyCounts
// aliases are provided by re_context.hpp.
