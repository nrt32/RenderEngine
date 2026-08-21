// tests/t12_samples_test.cpp — T12 gate tests (FR-app.1, partial, SPEC §4).
//
// Asserts that each of the mesh/plane/volume samples (built by T12 from the
// shared app::SampleHarness) runs under Xvfb, opens a window with a GL 4.6 core
// context, and exits cleanly (exit code 0, no sanitizer reports) within a
// timeout:
//   (1) exit code == 0  — the sample rendered `RE_SAMPLE_MAX_FRAMES` frames
//       without a frame failure (the harness returns 1 on any render error) and
//       shut down cleanly (FR-app.1: "exit code 0");
//   (2) the sample's log contains "GL 4.6 core" — core::Window logged the
//       window creation + context probe (glGetIntegerv GL_MAJOR_VERSION==4,
//       GL_MINOR_VERSION==6) only after glfwCreateWindow succeeded, so the
//       window demonstrably opened (FR-app.1: "opens a window", SPEC §2/§8
//       target GL 4.6 core);
//   (3) the log contains no sanitizer error signatures ("AddressSanitizer",
//       "runtime error:", "LeakSanitizer") — no ASan/UBSan reports
//       (FR-app.1: "no sanitizer reports"). The sample subprocess runs with
//       ASAN_OPTIONS=detect_leaks=0: the leak gate stays with the unit-test
//       suite (llvmpipe, stable attribution, SPEC §8); address/UB detection is
//       still active and any ASan/UBSan abort makes the exit code non-zero.
//
// The samples are spawned via `timeout ... xvfb-run -a <bin>` (SPEC §8: "the
// sample smoke gates (T12/T13) run under WSLg when present, otherwise under
// xvfb"); `timeout` makes a hang fail the gate with exit code 124 instead of
// blocking forever.
//
// Per the readback/GL-ownership guardrails this test spawns subprocesses only —
// it touches no GL and no raw glXxx calls.

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants (FR-app.1 / SPEC §2, §8).
// ---------------------------------------------------------------------------

// The three T12 samples, in build order (binaries produced by app/).
constexpr const char* kSampleNames[] = {"mesh", "plane", "volume"};
constexpr const char* kSampleBins[] = {RE_SAMPLE_MESH_BIN, RE_SAMPLE_PLANE_BIN,
                                       RE_SAMPLE_VOLUME_BIN};
constexpr int kSampleCount = 3;

// The sample renders this many frames before exiting (small: the gate only
// needs the window opened and a clean exit).
constexpr int kMaxFrames = 20;

// The gate timeout per sample in seconds: long enough for the llvmpipe volume
// ray-cast frames, short enough that a hang fails loudly (exit 124).
constexpr int kTimeoutSeconds = 120;

// The golden substring core::Window logs after a successful window creation +
// GL 4.6 core context probe (core/window.cpp: "window: %dx%d GL %d.%d core").
constexpr const char* kWindowOpenedMarker = "GL 4.6 core";

// Sanitizer error signatures that must NOT appear in the sample's output
// (FR-app.1: "no sanitizer reports").
constexpr const char* kSanitizerSignatures[] = {
    "AddressSanitizer", "UndefinedBehaviorSanitizer",
    "runtime error:", "LeakSanitizer"};

// ---------------------------------------------------------------------------
// Test helpers.
// ---------------------------------------------------------------------------

/// True if `path` exists and is a regular file.
bool fileExists(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

/// Read the whole file at `path` into `out`. Returns false on read failure.
bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

/// Run one sample under Xvfb with the gate environment; returns its exit code
/// (or -1 if the process was killed by a signal / could not be spawned) and
/// appends its captured stdout+stderr to `output`.
int runSampleUnderXvfb(const std::string& bin, const std::string& name,
                       std::string& output) {
    const std::string logFile =
        std::string(RE_TEST_BIN_DIR) + "/t12_" + name + "_sample.log";
    const std::string cmd =
        "timeout " + std::to_string(kTimeoutSeconds) + " env " +
        "RE_SAMPLE_MAX_FRAMES=" + std::to_string(kMaxFrames) + " " +
        "ASAN_OPTIONS=detect_leaks=0 " +
        "GALLIUM_DRIVER=llvmpipe MESA_GL_VERSION_OVERRIDE=4.6 " +
        "xvfb-run -a '" + bin + "' > '" + logFile + "' 2>&1";

    const int rc = std::system(cmd.c_str());
    readFile(logFile, output);
    if (rc == -1) {
        return -1; // system() failed to spawn the shell
    }
    if (WIFEXITED(rc)) {
        return WEXITSTATUS(rc);
    }
    return -1; // killed by a signal (e.g. ASan abort raises SIGABRT)
}

} // namespace

// ---------------------------------------------------------------------------
// FR-app.1 (partial) — each sample runs, opens a window, exits cleanly.
// ---------------------------------------------------------------------------

TEST(T12Samples, MeshPlaneVolumeSamplesRunOpenWindowExitClean) {
    for (int i = 0; i < kSampleCount; ++i) {
        const std::string name = kSampleNames[i];
        const std::string bin = kSampleBins[i];

        SCOPED_TRACE("sample '" + name + "' binary '" + bin + "'");

        // The gate requires the sample binary to exist: a missing binary is a
        // build/config failure, surfaced loudly (never silently skipped).
        ASSERT_TRUE(fileExists(bin)) << "sample binary missing";

        std::string output;
        const int exitCode = runSampleUnderXvfb(bin, name, output);

        // (1) Clean exit within the timeout: exit code 0. A hang yields 124
        //     (timeout), a sanitizer abort yields a signal (-1).
        EXPECT_EQ(exitCode, 0)
            << "sample did not exit cleanly; captured output:\n"
            << output;

        // (2) The window opened: core::Window logged the GL 4.6 core probe
        //     after glfwCreateWindow + gladLoadGL succeeded.
        EXPECT_NE(output.find(kWindowOpenedMarker), std::string::npos)
            << "sample did not open a GL 4.6 core window; captured output:\n"
            << output;

        // (3) No sanitizer reports (FR-app.1).
        for (const char* signature : kSanitizerSignatures) {
            EXPECT_EQ(output.find(signature), std::string::npos)
                << "sample reported a sanitizer error ('" << signature
                << "'); captured output:\n"
                << output;
        }
    }
}

} // namespace re::tests