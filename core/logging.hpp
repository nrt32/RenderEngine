#pragma once

// core/logging.hpp — centralized spdlog initialization (SPEC §5, "Logging").
//
// RenderEngine uses spdlog exclusively for diagnostics (no raw printf/cout).
// This component configures a console sink with a deterministic,
// single-threaded pattern. It is a core/ component but is GL-free; it exists
// here so that all modules share one logging setup.

#include <spdlog/spdlog.h>

namespace re::core {

/// Configure the global spdlog logger with a console sink.
///
/// Safe to call more than once (re-initializes the default logger). Returns the
/// shared default logger for convenience.
std::shared_ptr<spdlog::logger> initLogging();

} // namespace re::core
