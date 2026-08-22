#pragma once

/// @file core/glsl_version.hpp
/// @brief GLSL profile macro (SPEC §9 V2.7, V2 T8).
///
/// Decouples the shader language level from the llvmpipe ceiling:
///   450 = portable floor (tests/CI, llvmpipe caps at GLSL 4.50)
///   460 = hardware floor (native d3d12 / desktop GL with full 4.6)
/// Single `#version` concern now that shaders live in files (T7).
///
/// The macro is the single source of truth for the `#version` line that heads
/// every `.glsl` file under `render/shaders/`. In the gate/CI environment
/// (llvmpipe) it expands to `#version 450 core`; on hardware the same macro
/// expands to `#version 460 core` when `RE_FORCE_GLSL_460` (or a direct
/// `-DRE_GLSL_VERSION=460`) is supplied. A GL 4.6 core context accepts GLSL
/// 4.50 shaders, so the portable 450 floor compiles on both paths (SPEC §8).
///
/// Usage:
///   @code
///   #include "core/glsl_version.hpp"
///   static_assert(RE_GLSL_VERSION == 450, "gate expects portable floor");
///   std::string vs = std::string(RE_GLSL_VERSION_LINE) + "\n ... rest ...";
///   auto prog = core::ShaderProgram::create(vs, fs); // compiles on llvmpipe
///   @endcode
///
/// Every `.glsl` file under `render/shaders/` starts with the line produced by
/// `RE_GLSL_VERSION_LINE` (verified by the T8 gate); a version bump updates
/// the macro and the shader files' first lines in lockstep, so the macro
/// remains the single authoritative `#version` concern.

/// The GLSL language version as an integer (450 or 460).
/// Override with `-DRE_GLSL_VERSION=460` or `-DRE_FORCE_GLSL_460` for the
/// hardware floor. Default is the portable 450 floor.
#ifndef RE_GLSL_VERSION
#ifdef RE_FORCE_GLSL_460
#define RE_GLSL_VERSION 460
#else
#define RE_GLSL_VERSION 450
#endif
#endif

// Stringification helpers so the `#version` line stays in sync with the integer.
#define RE_GLSL_DETAIL_STR(x) #x
#define RE_GLSL_DETAIL_XSTR(x) RE_GLSL_DETAIL_STR(x)

/// The full `#version` line, e.g. `"#version 450 core"` or `"#version 460 core"`.
/// This is the single `#version` concern (SPEC §9 V2.7).
#define RE_GLSL_VERSION_LINE "#version " RE_GLSL_DETAIL_XSTR(RE_GLSL_VERSION) " core"

/// The version as a string literal, e.g. `"450"` or `"460"`.
#define RE_GLSL_VERSION_STRING RE_GLSL_DETAIL_XSTR(RE_GLSL_VERSION)

// Compile-time guard: only the two supported floors are valid.
static_assert(RE_GLSL_VERSION == 450 || RE_GLSL_VERSION == 460,
              "RE_GLSL_VERSION must be 450 (portable llvmpipe floor) or 460 (hardware floor)");
