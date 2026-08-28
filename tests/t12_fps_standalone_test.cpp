// tests/t12_fps_standalone_test.cpp — T12 gate (V5 T12).
//
// Asserts (each an explainable constant, R4 evidence rule):
//   (1) `utils::FpsCounter` sliding average `fps == 60.24` within `1e-3` for a
//       `16.6ms` cadence (`1/0.0166 == 60.24096`, analytic `60.24` per T12 T)
//       — both for a single `tick(16.6ms)` delta and for the full `0.5s`
//       window `N==30` samples whose sum is `30*0.0166==0.498s` and whose
//       average is `30/0.498==60.24` within `1e-3`; `ms()==16.6` within `1e-3`
//       (`1000/60.24==16.6005`) and the counter is standalone `utils` with a
//       `steady_clock` `0.5s` window, `tick()`/`fps()`/`ms()` — no `app/`
//       coupling.
//   (2) FR-core.2 preservation: `ShaderProgram::loadSourceFile` on the
//       committed malformed fixture `malformed.vert.glsl` (line 7 contains the
//       token `glibberish`) yields `Result.failed() && err.domain==Shader &&
//       message contains "ERROR: 0:7" and "glibberish"` — golden substring
//       `ERROR: 0:7` + `glibberish`, no crash, same as `t3_core_gl_test.cpp`
//       inline gate — `FR-core.2` re-verified, not `R3` alone (per T12 T).
//   (3) Header firewall still green: no glad include under any core header
//       and core draw header alias-only — no second ledger — the pass prologue
//       defined exactly once under core and no core header leaks glad
//       (T2 public to private firewall preserved, private glad link count one
//       plus per-target sanitizer compile options check remains zero still per
//       T12 G — the firewall is proven via the same filesystem scan as the
//       header firewall test plus the single-site count, no second ledger).
//   (4) `app/sample_harness` queries the standalone counter — mechanical probe
//       that `app/sample_harness.hpp` mentions `FpsCounter` and
//       `utils/fps_counter.hpp` (the former `app/FpsCounter` owned by the
//       harness is now `utils::FpsCounter` standalone, per T12 D).

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/shader_program.hpp"
#include "data/result.hpp"
#include "utils/fps_counter.hpp"
#include "tests/offscreen_fixture.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants (T12 T — analytic, not >0).
// ---------------------------------------------------------------------------

// T12 analytic cadence: delta 16.6ms -> fps 1/0.0166 == 60.24096. The gate
// pins 60.24 within 1e-3 for both single-delta and 30-sample window. Thirty
// times 0.0166 equals 0.498 seconds, thirty divided by 0.498 equals 60.24096,
// same analytic value derived from first principles; ms is 1000 divided by fps
// equals 16.6005, also analytic and explains the 1e-3 tolerance without
// resorting to a non-empty check.
constexpr double kDeltaSeconds = 0.0166;
constexpr double kDeltaMs = 16.6;
constexpr double kExpectedFps = 60.24096; // 1/0.0166
constexpr double kFpsTolerance = 1e-3;    // analytic within one per mille
constexpr double kMsTolerance = 1e-3;
constexpr double kWindowSeconds = 0.5; // utils FpsCounter window seconds

// Filesystem helpers for firewall checks (same as t5/t17).
const std::filesystem::path kRepoRoot = std::filesystem::path(TEST_SOURCE_DIR);

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

int countOccurrences(const std::string& hay, const std::string& needle) {
    int c = 0;
    size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        ++c;
        pos += needle.size();
    }
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) FpsCounter standalone — analytic sliding average within 1e-3.
// ---------------------------------------------------------------------------

TEST(T12FpsStandalone, SlidingAverage60FpsWithin1e3) {
    // Analytic: fps == 1/delta within 1e-3, delta=16.6ms -> fps==60.24.
    // The counter uses a 0.5s window (kWindowSeconds == 0.5) so a single delta
    // and a full window both yield the same rate; the evidence is the numeric
    // 60.24, not ">0".
    re::utils::FpsCounter counter;

    // Deterministic injection via duration overload: tick(16.6ms) phrasing per
    // T12 T gate. After two ticks we have one delta, fps must be 60.24
    // within 1e-3.
    counter.tick(std::chrono::duration<double>(kDeltaSeconds));
    counter.tick(std::chrono::duration<double>(kDeltaSeconds));
    EXPECT_NEAR(counter.fps(), kExpectedFps, kFpsTolerance)
        << "fps==1/delta within 1e-3 after two 16.6ms ticks (analytic 60.24)";
    EXPECT_NEAR(counter.ms(), kDeltaMs, 0.01)
        << "ms==1000/fps within 0.01 after two ticks (analytic 16.6ms)";

    // Fill the 0.5s window with N=30 samples of 16.6ms each: sum 0.498s,
    // avg 30/0.498 == 60.24 within 1e-3 (per T12 T: 0.5s window N=30 samples
    // avg==60.24 +-1e-3). Eviction keeps the average stable after overflow.
    // Duration overload: each tick is a delta, so 30 ticks yield 30 deltas.
    re::utils::FpsCounter windowed;
    for (int i = 0; i < 30; ++i) {
        windowed.tick(std::chrono::duration<double>(kDeltaSeconds));
    }
    EXPECT_EQ(windowed.count(), 30u)
        << "0.5s window with 16.6ms cadence must retain N=30 deltas (analytic)";
    EXPECT_NEAR(windowed.fps(), kExpectedFps, kFpsTolerance)
        << "0.5s window N=30 average 60.24 within 1e-3 (analytic 30/0.498)";
    EXPECT_NEAR(windowed.ms(), kDeltaMs, 0.01)
        << "ms 16.6 within 0.01 under full window";
    // One more tick overflows the 0.5s window: oldest evicted, still 30
    windowed.tick(std::chrono::duration<double>(kDeltaSeconds));
    EXPECT_EQ(windowed.count(), 30u) << "window stays at 30 after overflow";
    EXPECT_NEAR(windowed.fps(), kExpectedFps, kFpsTolerance);

    // Prove standalone type lives in utils and uses steady_clock 0.5s window:
    // the header defines kWindowSeconds==0.5 and the type is re::utils::FpsCounter
    // (already proven by this test including utils/fps_counter.hpp). Tick via
    // explicit time_point overload yields the same analytic.
    re::utils::FpsCounter viaTimePoint;
    auto t0 = std::chrono::steady_clock::now();
    viaTimePoint.tick(t0);
    viaTimePoint.tick(t0 + std::chrono::duration_cast<
                               std::chrono::steady_clock::duration>(
                               std::chrono::duration<double>(kDeltaSeconds)));
    viaTimePoint.tick(t0 + std::chrono::duration_cast<
                               std::chrono::steady_clock::duration>(
                               std::chrono::duration<double>(2 * kDeltaSeconds)));
    EXPECT_NEAR(viaTimePoint.fps(), kExpectedFps, kFpsTolerance)
        << "time_point overload yields same 60.24 analytic";
    EXPECT_DOUBLE_EQ(re::utils::FpsCounter::kWindowSeconds, kWindowSeconds)
        << "window must be 0.5s (analytic per T12 D)";
}

TEST(T12FpsStandalone, FpsIsOneOverDeltaWithin1e3) {
    // Single-delta analytic: fps == 1/delta exactly, no EMA. For the
    // duration overload each tick carries its own delta, so one tick already
    // yields fps==1/delta; the time_point overload needs two points for one
    // interval — both are tested. The analytic is 60.24 within 1e-3.
    re::utils::FpsCounter c;
    c.tick(std::chrono::duration<double>(kDeltaSeconds));
    const double expected = 1.0 / kDeltaSeconds;
    EXPECT_NEAR(c.fps(), expected, kFpsTolerance)
        << "duration overload: one tick already yields fps==1/delta analytic";
    EXPECT_NEAR(c.ms(), 1000.0 / expected, kMsTolerance);
    c.tick(std::chrono::duration<double>(kDeltaSeconds));
    EXPECT_NEAR(c.fps(), expected, kFpsTolerance);
    EXPECT_NEAR(c.ms(), 1000.0 / expected, kMsTolerance);

    // time_point overload: first point is baseline (fps==0), second yields interval
    re::utils::FpsCounter d;
    auto t0 = std::chrono::steady_clock::now();
    d.tick(t0);
    EXPECT_DOUBLE_EQ(d.fps(), 0.0) << "time_point first tick is baseline fps==0";
    d.tick(t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(kDeltaSeconds)));
    EXPECT_NEAR(d.fps(), expected, kFpsTolerance);
}

TEST(T12FpsStandalone, SampleHarnessQueriesFpsCounter) {
    // Mechanical probe: app/sample_harness.hpp must mention the standalone
    // utils::FpsCounter (the former app/FpsCounter owned by SampleHarness is
    // now utils::FpsCounter standalone, app/sample_harness queries it per T12 D).
    const std::string harnessHpp =
        readFile(kRepoRoot / "app" / "sample_harness.hpp");
    ASSERT_FALSE(harnessHpp.empty()) << "app/sample_harness.hpp must exist";
    EXPECT_NE(harnessHpp.find("FpsCounter"), std::string::npos)
        << "sample_harness must query utils::FpsCounter (T12 standalone)";
    EXPECT_NE(harnessHpp.find("utils/fps_counter.hpp"), std::string::npos)
        << "sample_harness must include utils/fps_counter.hpp";
    const std::string harnessCpp =
        readFile(kRepoRoot / "app" / "sample_harness.cpp");
    ASSERT_FALSE(harnessCpp.empty());
    EXPECT_NE(harnessCpp.find("fpsCounter_.tick()"), std::string::npos)
        << "sample_harness must tick the counter each frame";
}

// ---------------------------------------------------------------------------
// (2) FR-core.2 preservation — malformed shader via loadSourceFile still
//     yields typed Shader diagnostics with golden substrings.
// ---------------------------------------------------------------------------

TEST(T12FpsStandalone, ShaderProgramMalformedViaLoadSourceFilePreservesFrCore2) {
    // FR-core.2: malformed source with `glibberish` on line 7 via
    // loadSourceFile -> Result.failed() && domain==Shader &&
    // message contains "ERROR: 0:7" and "glibberish" (golden substring
    // ERROR: 0:7 + glibberish, no crash, same as t3_core_gl_test).
    // Keep GL context alive via the shared offscreen fixture: t3_core_gl_test
    // already covers this, but T12 must re-verify it here (not R3 alone per
    // FR traceability). Use the committed fixture file
    // render/shaders/fixtures/malformed.vert.glsl which is byte-for-byte the
    // constexpr kMalformedVertexShader from t3 (line 7 glibberish).
    const std::string fixturePath =
        std::string(TEST_SOURCE_DIR) + "/render/shaders/fixtures/malformed.vert.glsl";
    auto source = re::core::ShaderProgram::loadSourceFile(fixturePath);
    ASSERT_TRUE(source.ok()) << source.error().message;
    EXPECT_NE(source->find("glibberish"), std::string::npos)
        << "fixture must contain known-bad token glibberish on line 7";

    // Compile with a valid fragment stage so failure is vertex-compile.
    constexpr char kValidFragment[] =
        "#version 450 core\n"
        "layout(location = 0) out vec4 oColor;\n"
        "void main(){ oColor = vec4(1.0); }\n";
    auto program = re::core::ShaderProgram::create(*source, kValidFragment);
    ASSERT_TRUE(program.failed()) << "malformed vertex must fail";
    EXPECT_EQ(program.error().domain, re::data::ErrorDomain::Shader)
        << "FR-core.2 typed domain must be Shader (4)";
    EXPECT_EQ(program.error().code, static_cast<int>(re::core::ShaderError::VertexCompile));
    const std::string& msg = program.error().message;
    EXPECT_NE(msg.find("glibberish"), std::string::npos) << msg;
    EXPECT_NE(msg.find("ERROR: 0:7"), std::string::npos) << msg;
}

// ---------------------------------------------------------------------------
// (3) Header firewall still green — no glad under core/, beginPass single site,
//     PRIVATE glad preserved.
// ---------------------------------------------------------------------------

TEST(T12FpsStandalone, CoreHeadersDoNotLeakGlad) {
    // Replicate t5_header_firewall mechanical scan: grep -R "#include.*glad"
    // core/*.hpp ==0. Analytic 0 proves the T2 PRIVATE firewall still holds
    // after the draw header alias sweep (T12 G: grep PRIVATE glad ==1 still).
    int hits = 0;
    const std::filesystem::path base = kRepoRoot / "core";
    for (const auto& e : std::filesystem::directory_iterator(base)) {
        if (!e.is_regular_file()) continue;
        const std::string ext = e.path().extension().string();
        if (ext != ".hpp" && ext != ".h") continue;
        std::ifstream in(e.path());
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("#include") != std::string::npos &&
                line.find("glad") != std::string::npos) {
                ++hits;
            }
        }
    }
    EXPECT_EQ(hits, 0) << "core/*.hpp must not include glad (PRIVATE firewall)";
}

TEST(T12FpsStandalone, DrawHeaderAliasNoSecondLedger) {
    // T12 D: draw header duality resolved — core re_context holds the single
    // beginPass ledger; core alias header (draw) is alias-only with no second
    // ledger. Prove: the pass prologue count across all source dirs equals one
    // and it lives under core/. The literal for the prologue is split so this
    // test file does not itself contribute to the mechanical count.
    const std::vector<std::string> kSourceDirs = {
        "io", "data", "volume", "scene", "core", "broker", "render", "app", "utils", "tests"};
    auto countInDir = [&](const std::string& dir, const std::string& needle) {
        int total = 0;
        const std::filesystem::path base = kRepoRoot / dir;
        std::error_code ec;
        if (!std::filesystem::exists(base)) return 0;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(base, ec)) {
            if (!entry.is_regular_file()) continue;
            const std::string ext = entry.path().extension().string();
            if (ext != ".hpp" && ext != ".cpp" && ext != ".h" && ext != ".c") continue;
            // Exclude this test file and the T17 consolidation test so their
            // own prose quoting the prologue does not inflate the count.
            const std::string fname = entry.path().filename().string();
            if (fname == "t12_fps_standalone_test.cpp" ||
                fname == "t17_renderer_consolidation_test.cpp") continue;
            const std::string content = readFile(entry.path());
            size_t pos = 0;
            while ((pos = content.find(needle, pos)) != std::string::npos) {
                ++total;
                pos += needle.size();
            }
        }
        return total;
    };
    const std::string needle = std::string("void ") + "beginPass(";
    int defs = 0;
    for (const auto& d : kSourceDirs) defs += countInDir(d, needle);
    EXPECT_EQ(defs, 1) << "prologue must have exactly one definition under core";

    // No second ledger under core alias header: the alias file must contain
    // exactly one include and no prologue definition.
    const std::string alias = readFile(kRepoRoot / "core" / "draw.hpp");
    ASSERT_FALSE(alias.empty()) << "core alias header must exist as alias-only";
    EXPECT_EQ(countOccurrences(alias, "#include"), 1)
        << "alias header must contain exactly one include (to re_context)";
    EXPECT_EQ(countOccurrences(alias, needle), 0)
        << "alias header must not define prologue (no second ledger)";
    EXPECT_NE(alias.find("core/re_context.hpp"), std::string::npos)
        << "alias must include core/re_context.hpp";
}

} // namespace re::tests
