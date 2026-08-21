#pragma once

// tests/offscreen_fixture.hpp — shared fixture for headless GL unit tests.
//
// Creates a single offscreen GL 4.6 core context (core::OffscreenContext) and
// keeps it alive for the duration of the test program. Tests consume GL only
// through core/ wrappers; the context itself is a core/ component.

#include <gtest/gtest.h>

#include "core/offscreen_context.hpp"

namespace re::tests {

/// Initializes a single offscreen GL context before the suite and tears it down
/// after. Logging (spdlog) is initialized here too (SPEC S5).
class OffscreenEnvironment : public ::testing::Environment {
   public:
    void SetUp() override;
    void TearDown() override;

    /// The shared offscreen context (valid during the whole test program).
    static core::OffscreenContext* context();
};

} // namespace re::tests
