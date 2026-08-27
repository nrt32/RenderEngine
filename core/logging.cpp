// core/logging.cpp — spdlog initialization.

#include "core/logging.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace re::core {

std::shared_ptr<spdlog::logger> initLogging() {
    if (auto existing = spdlog::get("renderengine")) {
        return existing;
    }
    auto logger = spdlog::stdout_color_mt("renderengine");
    // The engine is single-threaded by design (one render thread drives GL),
    // so a plain timestamp+level pattern keeps logs deterministic and
    // diff-able between gate runs — no thread ids, no nondeterministic
    // interleaving.
    logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    // VG12 logging level knob: default trace for gate verbosity; callers may
    // override via spdlog::set_level(spdlog::level::info) or via
    // SPDLOG_LEVEL env (off|trace|debug|info|warn|error). The knob is
    // documented here so levels are not hardcoded per call site.
    logger->set_level(spdlog::level::trace);
    spdlog::set_default_logger(logger);
    return logger;
}

} // namespace re::core
