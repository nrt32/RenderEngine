// tests/t1_scaffolding_test.cpp — T1 gate tests.
//
// Asserts the build & test scaffolding requirements from TASKS.md T1:
//   (1) the suite builds and runs green under the sanitizers (verified by the
//       harness itself — this binary compiles and runs under ASan+UBSan);
//   (2) a trivial explainable constant passes (2+2==4);
//   (3) the offscreen fixture creates a GL 4.6 core context — the version and
//       profile are probed via glGetIntegerv inside core/ (NOT the unreliable
//       glGetString text) and surfaced through the utils::OffscreenContext
//       wrapper (V2.1 moved the context to utils/; the raw probe anchor
//       core::loadCoreGl stays under core/); no GL errors are reported;
//   (4) the gate environment is correctly sourced (R15): AUDIT_SOURCE_DIRS and
//       LOOP_BUILD_TEST_CMD must be set by `source tools/env.sh`;
//   (5) audit passes with our source-dir override (enforced by the runner gate;
//       this test asserts the env that drives it).
//
// Per the GL-ownership guardrail, this file uses ONLY core/ wrappers — no raw
// glXxx calls.

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>

#include "core/gl_error.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/offscreen_context.hpp"

namespace re::tests {

// ---------------------------------------------------------------------------
// (1) Trivial explainable constant.
// ---------------------------------------------------------------------------
TEST(T1Scaffolding, TrivialArithmeticConstant) {
    // Analytic constant: 2 + 2 == 4.
    EXPECT_EQ(2 + 2, 4);
}

// ---------------------------------------------------------------------------
// (3) Offscreen GL 4.6 core context.
// ---------------------------------------------------------------------------
TEST(T1Scaffolding, OffscreenContextIsGl46Core) {
    utils::OffscreenContext* ctx = OffscreenEnvironment::context();
    ASSERT_NE(ctx, nullptr);

    // The version/profile are probed inside core/ via the integer queries (the
    // reliable path) and surfaced through the wrapper — not the version-string
    // text.
    EXPECT_EQ(ctx->majorVersion(), 4);
    EXPECT_EQ(ctx->minorVersion(), 6);
    EXPECT_TRUE(ctx->isCoreProfile());

    // No GL errors after context setup (consumed via the core/ wrapper).
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (4) Gate environment correctly sourced (R15).
// ---------------------------------------------------------------------------
TEST(T1Scaffolding, GateEnvironmentSourced) {
    // AUDIT_SOURCE_DIRS must equal the project's non-default layout (SPEC §8),
    // including the utils/ module added in V2.1 and the scene/broker modules
    // added in V3 (SPEC §3).
    const char* auditDirs = std::getenv("AUDIT_SOURCE_DIRS");
    ASSERT_NE(auditDirs, nullptr) << "AUDIT_SOURCE_DIRS is unset. Launch with: "
                                     "source tools/env.sh (SPEC §8, R15).";
    EXPECT_STREQ(auditDirs, "io data volume scene core broker render app utils tests");

    // LOOP_BUILD_TEST_CMD must be set (the runner uses it to run the gate).
    const char* buildCmd = std::getenv("LOOP_BUILD_TEST_CMD");
    ASSERT_NE(buildCmd, nullptr) << "LOOP_BUILD_TEST_CMD is unset. Launch "
                                    "with: source tools/env.sh (SPEC §8, R15).";
    EXPECT_GT(std::strlen(buildCmd), 0u);
}

} // namespace re::tests