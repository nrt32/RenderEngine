// core/draw.cpp — deprecated shim (T2 rename DrawContext → REContext).
//
// Canonical implementation now lives in core/re_context.cpp. This file is kept
// as an empty translation unit so historic includes still resolve, but it no
// longer defines any GL logic (guardrail gpu_api_ownership — raw GL only under
// core/re_context.cpp).

#include "core/re_context.hpp"

namespace re::core {
// Intentionally empty — all logic in re_context.cpp.
}
