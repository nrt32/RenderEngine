#pragma once

// tests/offscreen_fixture.hpp — shared fixture for headless GL unit tests.
//
// Creates a single offscreen GL 4.6 core context (utils::OffscreenContext) and
// keeps it alive for the duration of the test program. Tests consume GL only
// through core/ wrappers; the context itself lives in utils/ (SPEC §9 V2.1).

#include <gtest/gtest.h>

#include "utils/offscreen_context.hpp"

namespace re::tests {

/// Initializes a single offscreen GL context before the suite and tears it
/// down after, so every GL-touching test runs headless (no window server
/// needed) against one shared, deterministic context. Logging (spdlog) is
/// initialized here too so test diagnostics go through the same sink as the
/// engine.
class OffscreenEnvironment : public ::testing::Environment {
   public:
    void SetUp() override;
    void TearDown() override;

    /// The shared offscreen context (valid during the whole test program).
    static utils::OffscreenContext* context();
};

} // namespace re::tests
