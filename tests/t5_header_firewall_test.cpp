// tests/t5_header_firewall_test.cpp — T5 gate: GL header firewall.
//
// Asserts (T5 D/T):
//  (1) No "<glad" include in any core/*.hpp — the public headers are
//      GL-call-free again after moving REContext inline GL calls/constants
//      out of core/re_context.hpp into core/re_context.cpp and dropping
//      <glad/gl.h> from the public header. Privatized re_core glad/glfw
//      linkage (PUBLIC -> PRIVATE) is proven by the fact that downstream
//      still builds without transitive leak.
//  (2) Minimal TU including core/re_context.hpp does NOT leak glad —
//      GL constants (GL_COLOR_BUFFER_BIT, GL_TRIANGLES, GL_ONE) are absent
//      after including only the public header, proving no transitive
//      <glad/gl.h> include. Downstream (render/, app/, tests/) builds
//      without needing glad's include path.
//
// Explainable constants: count 0 (not >0) for glad includes; boolean false
// for leaked GL defines (analytic: absence, not non-empty).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Include the public header whose firewall we assert — this inclusion itself
// must NOT pull <glad/gl.h>. The leak check below proves it.
#include "core/re_context.hpp"

// After including only core/re_context.hpp, none of the GL constants that
// glad defines should be present. If glad leaked, these would be defined.
#ifdef GL_COLOR_BUFFER_BIT
constexpr bool kGladLeakedColorBuffer = true;
#else
constexpr bool kGladLeakedColorBuffer = false;
#endif

#ifdef GL_TRIANGLES
constexpr bool kGladLeakedTriangles = true;
#else
constexpr bool kGladLeakedTriangles = false;
#endif

#ifdef GL_ONE
constexpr bool kGladLeakedOne = true;
#else
constexpr bool kGladLeakedOne = false;
#endif

#ifdef GL_BLEND
constexpr bool kGladLeakedBlend = true;
#else
constexpr bool kGladLeakedBlend = false;
#endif

namespace re::tests {
namespace {

const std::filesystem::path kRepoRoot = std::filesystem::path(TEST_SOURCE_DIR);

bool isHeader(const std::filesystem::path& p) {
    const std::string ext = p.extension().string();
    return ext == ".hpp" || ext == ".h";
}

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

int countGladInCoreHeaders() {
    int total = 0;
    const std::filesystem::path base = kRepoRoot / "core";
    if (!std::filesystem::exists(base)) return -1;
    for (const auto& entry : std::filesystem::directory_iterator(base)) {
        if (!entry.is_regular_file()) continue;
        if (!isHeader(entry.path())) continue;
        // Exclude this test file's own directory? It's under tests/, not core/, so fine.
        // But also skip count if file is a cpp — only headers per gate spec.
        const std::string content = readFile(entry.path());
        // The gate spec is: grep -R "#include.*glad" core/*.hpp ==0
        // We count occurrences of "#include" containing "glad" in header files.
        // Simple: count lines containing both tokens; use substring "#include" and "glad".
        // More precise: look for "#include" then "glad" on same line.
        // For mechanical parity, count occurrences of "glad" inside files that also have "#include".
        // Simpler: count "#include" + "glad" co-occurring.
        // We replicate grep: "#include.*glad" — i.e., "#include" followed by "glad" on same line.
        // Split content by lines.
        std::string line;
        std::istringstream iss(content);
        while (std::getline(iss, line)) {
            if (line.find("#include") != std::string::npos && line.find("glad") != std::string::npos) {
                ++total;
            }
        }
    }
    return total;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) No glad include in any core public header.
// ---------------------------------------------------------------------------

TEST(T5HeaderFirewall, NoGladIncludeInCoreHeaders) {
    const int hits = countGladInCoreHeaders();
    EXPECT_EQ(hits, 0) << "core/*.hpp must not include <glad/gl.h> — "
                       << "header firewall T5: REContext bodies moved to .cpp, "
                       << "glad is PRIVATE to re_core (found " << hits << " hits)";
}

// ---------------------------------------------------------------------------
// (2) Minimal TU including core/re_context.hpp does NOT leak glad.
// ---------------------------------------------------------------------------

TEST(T5HeaderFirewall, ReContextHeaderDoesNotLeakGlad) {
    // Analytic: each leaked constant must be absent (false, not >0). If any
    // leaked, the header still transitively includes glad.
    EXPECT_FALSE(kGladLeakedColorBuffer) << "core/re_context.hpp must not leak GL_COLOR_BUFFER_BIT — "
                                         << "glad header still transitively included";
    EXPECT_FALSE(kGladLeakedTriangles) << "must not leak GL_TRIANGLES";
    EXPECT_FALSE(kGladLeakedOne) << "must not leak GL_ONE";
    EXPECT_FALSE(kGladLeakedBlend) << "must not leak GL_BLEND";
}

TEST(T5HeaderFirewall, ReContextStillUsableWithoutGlad) {
    // Prove downstream still builds without transitive glad: we already
    // included core/re_context.hpp and can name REContext types.
    re::core::REContext ctx;
    // Use a non-GL method that is header-inline (no GL call) to prove the
    // type is complete without needing glad symbols at compile time.
    auto counts = ctx.getSpyCounts();
    EXPECT_EQ(counts.viewport, 0) << "fresh REContext spy must be 0 (analytic, not >0)";
    EXPECT_EQ(counts.clearColor, 0);
    // Also test free-function declarations are visible (linkage proven by build).
    (void)ctx;
}

// ---------------------------------------------------------------------------
// (3) All core headers remain glad-free (comprehensive scan via grep logic).
// ---------------------------------------------------------------------------

TEST(T5HeaderFirewall, AllCoreHeadersGladFreeComprehensive) {
    // Duplicate mechanical gate using substring counts for extra evidence:
    // render_no_glad already forbids render including glad, but T5 explicitly
    // checks core/*.hpp. Use the same helper as t17's countInDir but restricted
    // to core headers.
    int totalIncludes = 0;
    const std::filesystem::path base = kRepoRoot / "core";
    for (const auto& entry : std::filesystem::directory_iterator(base)) {
        if (!entry.is_regular_file()) continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".hpp" && ext != ".h") continue;
        const std::string content = readFile(entry.path());
        totalIncludes += countOccurrences(content, "#include");
        // Also ensure no raw GL constant in header that would imply glad needed
        // — but only check the include, not constants (constants could be named
        // in comments). The mechanical gate is include count.
    }
    // Analytic: total #include occurrences in core/*.hpp is exactly 59
    // (re_context.hpp 7 + texture2d.hpp 2 + load_core_gl.hpp 2 + logging.hpp 1
    // + transform_feedback.hpp 4 + gl_error.hpp 2 + framebuffer.hpp 3
    // + vertex_array.hpp 3 + window.hpp 5 + draw.hpp 1 + vertex_buffer.hpp 3
    // + read_pixels.hpp 4 + shader_program.hpp 11 + storage_buffer.hpp 4
    // + texture3d.hpp 2 + glsl_version.hpp 1 + element_buffer.hpp 4 = 59).
    // This exact count proves the scan enumerated files, not a vacuous >0.
    EXPECT_EQ(totalIncludes, 59) << "core headers include count must be 59 (analytic sum, proves scan)";
    EXPECT_EQ(countGladInCoreHeaders(), 0);
}

} // namespace re::tests
