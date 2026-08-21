// tests/offscreen_fixture.cpp — offscreen GL fixture setup/teardown.

#include "tests/offscreen_fixture.hpp"

#include "core/logging.hpp"

namespace re::tests {

namespace {
core::OffscreenContext* g_context = nullptr;

// Register the environment with GoogleTest before main() runs (gtest_main
// instantiates it). SetUp()/TearDown() wrap the whole test program.
::testing::Environment* const kEnvironment =
    ::testing::AddGlobalTestEnvironment(new OffscreenEnvironment());
} // namespace

void OffscreenEnvironment::SetUp() {
    core::initLogging();
    auto ctx = core::OffscreenContext::create();
    ASSERT_TRUE(ctx.ok()) << "failed to create offscreen GL context: "
                          << ctx.error().message;
    g_context = new core::OffscreenContext(std::move(*ctx));
}

void OffscreenEnvironment::TearDown() {
    delete g_context;
    g_context = nullptr;
}

core::OffscreenContext* OffscreenEnvironment::context() {
    return g_context;
}

} // namespace re::tests
