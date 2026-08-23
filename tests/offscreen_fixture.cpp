// tests/offscreen_fixture.cpp — offscreen GL fixture setup/teardown.

#include "tests/offscreen_fixture.hpp"

#include "core/logging.hpp"
#include "render/asset_registry.hpp"

namespace re::tests {

namespace {
utils::OffscreenContext* g_context = nullptr;

// Register the environment with GoogleTest before main() runs (gtest_main
// instantiates it). SetUp()/TearDown() wrap the whole test program.
::testing::Environment* const kEnvironment =
    ::testing::AddGlobalTestEnvironment(new OffscreenEnvironment());
} // namespace

void OffscreenEnvironment::SetUp() {
    core::initLogging();
    auto ctx = utils::OffscreenContext::create();
    ASSERT_TRUE(ctx.ok()) << "failed to create offscreen GL context: "
                          << ctx.error().message;
    g_context = new utils::OffscreenContext(std::move(*ctx));
}

void OffscreenEnvironment::TearDown() {
    // Destroy the process-wide shared asset registry (the default store the
    // volume/plane renderers use) while the GL context is still current, so
    // its GPU textures are deleted with valid GL state instead of during
    // static destruction after context death. Every test's local registries
    // and renderers are already gone by the time TearDown runs.
    render::AssetRegistry::resetShared();
    delete g_context;
    g_context = nullptr;
}

utils::OffscreenContext* OffscreenEnvironment::context() {
    return g_context;
}

} // namespace re::tests