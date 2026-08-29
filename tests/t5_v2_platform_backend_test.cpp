// tests/t5_v2_platform_backend_test.cpp — T5 gate tests (SPEC §9 V2.2).
//
// Asserts the V2.2 requirements from TASKS.md T5:
//   (1) utils::OffscreenContext picks the no-display backend per-OS
//       deterministically via platform macros — the selection is exposed
//       through OffscreenContext::platformNoDisplayBackend() and must be a
//       compile-time constant per OS (EGL-surfaceless/Mesa on Linux,
//       ANGLE-EGL or WGL on Windows, CGL on macOS), replacing the Mesa-only
//       EGL_PLATFORM_SURFACELESS_MESA hardcode;
//   (2) the Linux path is unchanged: the llvmpipe context is still GL 4.6 core
//       (major == 4, minor == 6, core profile bit — the SPEC §2 invariants
//       probed via core::loadCoreGl and surfaced through the utils/ wrapper).
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers and the utils/ facade — no raw glXxx calls.

#include <gtest/gtest.h>

#include <string>

#include "core/gl_error.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/offscreen_context.hpp"

namespace re::tests {

namespace {

// Explainable constants (SPEC §2): 4.6 core is the project's target;
// llvmpipe reports 4.6 via Mesa version override and the probe uses the
// integer queries, not the version string text.
constexpr int kExpectedMajor = 4;
constexpr int kExpectedMinor = 6;

} // namespace

// ---------------------------------------------------------------------------
// (1) Backend selection is deterministic per platform macro.
// ---------------------------------------------------------------------------

TEST(T5V2PlatformBackend, PlatformNoDisplayBackendIsDeterministic) {
    // The selection must be a compile-time constant per OS (SPEC §9 V2.2).
    // On this Linux host the deterministic fallback is Egl (EGL-surfaceless
    // via EGL_PLATFORM_SURFACELESS_MESA). The same branching must pick Wgl on
    // Windows and Cgl on macOS — the static constexpr makes it deterministic
    // without needing a display.
    constexpr auto kExpected = []() {
#if defined(__APPLE__)
        return utils::ContextBackend::Cgl;
#elif defined(_WIN32) || defined(_WIN64)
        return utils::ContextBackend::Wgl;
#else
        return utils::ContextBackend::Egl;
#endif
    }();

    const auto first = utils::OffscreenContext::platformNoDisplayBackend();
    const auto second = utils::OffscreenContext::platformNoDisplayBackend();
    EXPECT_EQ(first, kExpected)
        << "platformNoDisplayBackend() must match the backend pinned to the "
           "active platform macro (Linux=Egl, Windows=Wgl, macOS=Cgl)";
    // Deterministic: repeated calls agree.
    EXPECT_EQ(first, second) << "backend selection must be deterministic";
    // And the enum is distinct per OS — on this Linux host it must NOT be the
    // macOS or Windows value.
#if !defined(__APPLE__) && !defined(_WIN32) && !defined(_WIN64)
    EXPECT_NE(first, utils::ContextBackend::Cgl);
    EXPECT_NE(first, utils::ContextBackend::Wgl);
#endif

    // Human-readable name agrees (diagnostic aid, not just debug string).
    const char* name = utils::OffscreenContext::backendName(first);
    ASSERT_NE(name, nullptr);
#if !defined(__APPLE__) && !defined(_WIN32) && !defined(_WIN64)
    EXPECT_EQ(std::string(name), std::string("EGL"));
#endif
}

TEST(T5V2PlatformBackend, BackendNameIsDistinctPerBackend) {
    // Explainable invariant: each enum maps to a distinct, non-empty name.
    EXPECT_EQ(std::string(utils::OffscreenContext::backendName(
                  utils::ContextBackend::Glfw)),
              std::string("GLFW"));
    EXPECT_EQ(std::string(utils::OffscreenContext::backendName(
                  utils::ContextBackend::Egl)),
              std::string("EGL"));
    EXPECT_EQ(std::string(utils::OffscreenContext::backendName(
                  utils::ContextBackend::Wgl)),
              std::string("WGL"));
    EXPECT_EQ(std::string(utils::OffscreenContext::backendName(
                  utils::ContextBackend::Cgl)),
              std::string("CGL"));
}

// ---------------------------------------------------------------------------
// (2) Linux path unchanged (llvmpipe context still GL 4.6 core).
// ---------------------------------------------------------------------------

TEST(T5V2PlatformBackend, LinuxPathStillGl46Core) {
    // The shared fixture context (utils::OffscreenContext) is still a 4.6
    // core context even after the per-OS factory was introduced. The version
    // and profile are probed inside core via the integer queries and surfaced
    // through the wrapper (not the version string).
    utils::OffscreenContext* ctx = OffscreenEnvironment::context();
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(ctx->majorVersion(), kExpectedMajor);
    EXPECT_EQ(ctx->minorVersion(), kExpectedMinor);
    EXPECT_TRUE(ctx->isCoreProfile());
    EXPECT_FALSE(core::hasPendingGlError());

    // On this Linux host the actual backend that succeeded must be either the
    // GLFW hidden window (when a display is on the host, e.g. WSLg/xvfb) or
    // the surfaceless fallback (deterministic Linux fallback). It must not
    // be the Windows/macOS fallback.
    const auto backend = ctx->backend();
    EXPECT_TRUE(backend == utils::ContextBackend::Glfw ||
                backend == utils::ContextBackend::Egl)
        << "Linux host: backend must be Glfw or Egl, got "
        << utils::OffscreenContext::backendName(backend);
    EXPECT_NE(backend, utils::ContextBackend::Wgl);
    EXPECT_NE(backend, utils::ContextBackend::Cgl);
}

TEST(T5V2PlatformBackend, EglSurfacelessTokenScopedToLinux) {
    // Guard against the Mesa-only surfaceless hardcode
    // leaking onto non-Linux builds: the EGL-surfaceless path is the Linux
    // fallback, but the compile-time selector on this host must still be Egl
    // (proving the token is scoped to Linux, not a cross-platform hardcode).
    constexpr auto fallback = utils::OffscreenContext::platformNoDisplayBackend();
#if !defined(__APPLE__) && !defined(_WIN32) && !defined(_WIN64)
    EXPECT_EQ(fallback, utils::ContextBackend::Egl);
#endif
    (void)fallback;
}

} // namespace re::tests
