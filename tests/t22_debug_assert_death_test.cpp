// tests/t22_debug_assert_death_test.cpp — T22 gate, death-test half.
//
// Asserts the debug-build trap on failed `data::Result` dereference (T22
// work item: "add debug-only assertion (assert/abort macro, not exceptions)
// in operator* failure path"). The assert text ("called on a failed Result")
// is part of the misuse diagnostic and is matched here; release builds keep
// the documented UB, so the assertions are compiled only when assert() is
// live.
//
// WHY a dedicated test binary: gtest death tests fork(). The shared
// re_tests binary runs inside the offscreen GL fixture whose llvmpipe
// context spawns dozens of worker threads — forking that state costs ~10 s
// per assertion and is documented-unsafe (fork among threads holding
// allocator locks). assert()/abort() needs only data/result.hpp, so this
// target links no GL fixture and stays single-threaded: each death
// assertion is fast and deterministic. The expected values are explainable:
// the abort comes from exactly one assert site per accessor, carrying the
// pinned message substring.

#include <gtest/gtest.h>

#include <string>

#include "data/result.hpp"

namespace re::tests {

#if !defined(NDEBUG)

TEST(T22DeathTest, OperatorStarOnFailedResultAborts) {
    const auto r = data::makeError<int>(42, "no value behind this error");
    ASSERT_TRUE(r.failed());
    EXPECT_DEATH((void)*r, "called on a failed Result");
}

TEST(T22DeathTest, ConstOperatorStarOnFailedResultAborts) {
    const auto r = data::makeError<std::string>(1, "no payload");
    ASSERT_TRUE(r.failed());
    const auto& cr = r;
    EXPECT_DEATH((void)*cr, "called on a failed Result");
}

TEST(T22DeathTest, OperatorArrowOnFailedResultAbortsInsteadOfReturningNull) {
    // operator-> used to silently return nullptr on failure (review finding
    // B8), deferring the crash to an unexplained null deref at the call
    // site; it now trips the same debug trap as operator*.
    const auto r = data::makeError<int>(42, "no value behind this error");
    ASSERT_TRUE(r.failed());
    EXPECT_DEATH((void)r.operator->(), "called on a failed Result");
}

#else

TEST(T22DeathTest, DebugAssertIsCompiledOutUnderNdebug) {
    GTEST_SKIP() << "assert() is compiled out under NDEBUG; the documented "
                    "contract is UB in release builds";
}

#endif

} // namespace re::tests
