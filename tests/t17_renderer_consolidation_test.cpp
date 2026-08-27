// tests/t17_renderer_consolidation_test.cpp — T17 gate (renderer
// consolidation): one pass prologue, one quad, one hash, one geometryFor, no
// glad leak under render/.
//
// Asserts (each an explainable constant):
//   (1) "#include <glad/gl.h>" occurrences under render/ == 0. The GL loader
//       include is confined to core/ (guardrail gpu_api_ownership / the Sr.
//       review "single internal implementation" rule); renderers consume GL
//       only through core/ RAII objects and the core::Draw API.
//   (2) "GL_TRIANGLES" token occurrences under render/ == 0. Raw GL constants
//       live under core/ only; transform-feedback capture names its primitive
//       through core::PrimitiveMode (core-owned enum). Even comment prose was
//       reworded so this grep stays mechanical.
//   (3) Definition count of `kScreenQuadVerts` across all source dirs == 1
//       (render/screen_quad.cpp) — before consolidation the NDC vertex table
//       was defined twice (volume_renderer.cpp + linked_list_oit.cpp); the
//       gate floor is ≤1 and ==1 proves the shared provider exists. The
//       former third twin `kSliceQuadVerts` (volume_slice_renderer.cpp) is
//       gone: 0 hits.
//   (4) kQuadTriangleIndices == {0,1,2, 0,2,3} — analytic: corner order 0..3
//       of a quad covered by the two counter-clockwise triangles (0,1,2) and
//       (0,2,3). One header definition shared by ScreenQuad and PlaneRenderer.
//   (5) Pass-prologue single site: exactly 1 `void beginPass(` definition in
//       the tree (core/draw.hpp's DrawContext::beginPass); zero
//       `core::setClearColor(`/`core::clearColor()` calls under render/
//       (clearing happens ONLY inside beginPass — the OIT composite sequence
//       deliberately never clears, it blends over opaque contents); each
//       of the SIX direct-render entry points (mesh/plane/volume/slice/
//       contour/volume_slice render()) calls ctx.beginPass exactly once; and
//       View::render — the composition owner whose single clear replaces
//       per-layer clears — issues ctx.beginPass too instead of a private
//       six-call copy.
//   (6) geometryFor deduplicated: zero member definitions of
//       `<Renderer>::geometryFor(` under render/ (was 3 identical copies:
//       Mesh/Slice/ContourRenderer); exactly 1 definition of
//       resolveMeshGeometry (render/asset_registry.cpp), the ONE helper over
//       AssetRegistry.
//   (7) Content-hash unity: data::computeContentHash(Mesh|VolumeDataset|Image)
//       equals scene::computeContentHash on the same fixtures (the re::scene
//       functions are thin forwarders to the single GL-free definition in
//       data/content_hash.hpp — one algorithm behind both layers' asset
//       identity); identical bytes → identical hash across distinct
//       allocations; a one-byte content change flips the hash for these fixed
//       inputs (FNV-1a folds every byte into the accumulator).
//
// Evidence rule (R4): every expected value above is derived from the task's
// duplication inventory (2 vert-table twins + 1 slice twin → 1; 3 geometryFor
// copies → 1; 6 direct-render entry points; the closed index pattern; the
// forwarder identity) — no "non-empty/>0" assertions.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "data/content_hash.hpp"
#include "data/image.hpp"
#include "data/mesh.hpp"
#include "data/volume_dataset.hpp"
#include "render/screen_quad.hpp"
#include "scene/asset_id.hpp"

namespace re::tests {
namespace {

const std::filesystem::path kRepoRoot = std::filesystem::path(TEST_SOURCE_DIR);
const std::vector<std::string> kSourceDirs = {"io",     "data",  "volume",
                                              "scene",  "core",  "broker",
                                              "render", "app",   "utils",
                                              "tests"};

bool isSourceFile(const std::filesystem::path& p) {
    const std::string ext = p.extension().string();
    return ext == ".hpp" || ext == ".cpp" || ext == ".h" || ext == ".c";
}

/// All source files (recursive) under repo-relative `dir`. This test file
/// itself is EXCLUDED so its documentation comments quoting the audited
/// patterns cannot inflate the counts.
std::vector<std::filesystem::path> sourceFiles(const std::string& dir) {
    std::vector<std::filesystem::path> out;
    const std::filesystem::path base = kRepoRoot / dir;
    std::error_code ec;
    if (!std::filesystem::exists(base)) {
        return out;
    }
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(base, ec)) {
        if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
            continue;
        }
        if (entry.path().filename() == "t17_renderer_consolidation_test.cpp") {
            continue;
        }
        out.push_back(entry.path());
    }
    return out;
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

int countInDir(const std::string& dir, const std::string& needle) {
    int total = 0;
    for (const auto& f : sourceFiles(dir)) {
        total += countOccurrences(readFile(f), needle);
    }
    return total;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) No glad include under render/
// ---------------------------------------------------------------------------

TEST(T17RendererConsolidation, NoGladIncludeUnderRender) {
    EXPECT_EQ(countInDir("render", "#include <glad/gl.h>"), 0)
        << "render/ must never include glad directly — GL entry points are "
           "consumed through core/ wrappers (gpu_api_ownership)";
}

// ---------------------------------------------------------------------------
// (2) No raw GL primitive constants under render/
// ---------------------------------------------------------------------------

TEST(T17RendererConsolidation, NoRawGlPrimitiveConstantsUnderRender) {
    EXPECT_EQ(countInDir("render", "GL_TRIANGLES"), 0)
        << "GL primitive constants belong to core/ (e.g. core::PrimitiveMode "
           "for TransformFeedback::begin); render/ spells none of them";
}

// ---------------------------------------------------------------------------
// (3) One NDC vertex-table definition; the twins are gone
// ---------------------------------------------------------------------------

TEST(T17RendererConsolidation, OneScreenQuadVertsDefinition) {
    int defs = 0;
    for (const auto& dir : kSourceDirs) {
        defs += countInDir(dir, "constexpr std::array<float, 8> kScreenQuadVerts");
    }
    EXPECT_EQ(defs, 1)
        << "the NDC full-screen quad vertex table must have EXACTLY ONE "
           "definition (render/screen_quad.cpp); before T17 it was defined "
           "twice (volume_renderer.cpp + linked_list_oit.cpp)";
}

TEST(T17RendererConsolidation, SliceQuadVertsTwinRemoved) {
    EXPECT_EQ(countInDir("render", "kSliceQuadVerts"), 0)
        << "VolumeSliceRenderer now uses the shared ScreenQuad provider; the "
           "byte-identical local twin must stay deleted";
}

// ---------------------------------------------------------------------------
// (4) The one quad index pattern (analytic: two CCW triangles over corners)
// ---------------------------------------------------------------------------

TEST(T17RendererConsolidation, QuadIndexPatternIsTheTwoTriangleFan) {
    // Corner order 0..3 around the quad boundary: triangles (0,1,2) and
    // (0,2,3) tile the quad sharing the 0→2 diagonal — 6 indices total,
    // matching the golden pattern every quad VAO has always uploaded.
    constexpr std::array<std::uint32_t, 6> kGolden = {0u, 1u, 2u, 0u, 2u, 3u};
    static_assert(render::kQuadTriangleIndices.size() == kGolden.size(),
                  "one quad = two triangles = six indices");
    for (std::size_t i = 0; i < kGolden.size(); ++i) {
        EXPECT_EQ(render::kQuadTriangleIndices[i], kGolden[i])
            << "shared quad index pattern changed at position " << i;
    }
}

TEST(T17RendererConsolidation, QuadIndexPatternDefinedExactlyOnce) {
    EXPECT_EQ(countInDir("render",
                         "constexpr std::array<std::uint32_t, 6> "
                         "kQuadTriangleIndices"),
              1)
        << "the two-triangle index pattern must be defined once "
           "(render/screen_quad.hpp) and consumed by every quad VAO build";
}

// ---------------------------------------------------------------------------
// (5) Pass prologue: one definition site, zero renderer-local repeats
// ---------------------------------------------------------------------------

TEST(T17RendererConsolidation, BeginPassDefinedExactlyOnceInTree) {
    int defs = 0;
    for (const auto& dir : kSourceDirs) {
        defs += countInDir(dir, "void beginPass(");
    }
    EXPECT_EQ(defs, 1)
        << "the bind-target+viewport+clear+disable-depth/blend pass prologue "
           "must exist at exactly ONE site: core::DrawContext::beginPass in "
           "core/draw.hpp (before T17 the six calls were pasted into four "
           "technique renderers + contour + volume-slice)";
}

TEST(T17RendererConsolidation, RenderersDoNotRepeatClearPrologue) {
    EXPECT_EQ(countInDir("render", "core::setClearColor("), 0)
        << "clear-color installation happens only inside beginPass";
    EXPECT_EQ(countInDir("render", "core::clearColor()"), 0)
        << "color clearing happens only inside beginPass — the OIT composite "
           "deliberately never clears (it blends OVER opaque contents)";
}

TEST(T17RendererConsolidation, EveryDirectRenderEntryBeginsItsPassViaBeginPass) {
    // Exactly six renderers own a direct render(target) entry point today;
    // each migrated to the single prologue helper.
    const std::vector<std::string> kDirectRenderers = {
        "mesh_renderer.cpp",         "plane_renderer.cpp",
        "volume_renderer.cpp",       "slice_renderer.cpp",
        "contour_renderer.cpp",      "volume_slice_renderer.cpp",
    };
    ASSERT_EQ(kDirectRenderers.size(), 6u)
        << "six technique renderers expose a direct render() entry point; "
           "update this list when that changes";
    for (const auto& file : kDirectRenderers) {
        const std::string content = readFile(kRepoRoot / "render" / file);
        ASSERT_FALSE(content.empty()) << file << " must exist";
        EXPECT_EQ(countOccurrences(content, "ctx.beginPass("), 1)
            << file << " begins its pass at exactly ONE site via the shared "
                       "DrawContext::beginPass prologue (one render(target) "
                       "entry point, one prologue call)";
    }
}

TEST(T17RendererConsolidation, ViewCompositionBeginsItsPassViaBeginPass) {
    // View::render owns THE frame clear (layers never clear); it must issue
    // the shared beginPass prologue rather than keep a private six-call copy.
    const std::string content = readFile(kRepoRoot / "render" / "view.cpp");
    ASSERT_FALSE(content.empty()) << "render/view.cpp must exist";
    EXPECT_EQ(countOccurrences(content, "ctx.beginPass("), 1)
        << "View::render begins its pass via the ONE shared prologue";
    EXPECT_EQ(countOccurrences(content, "ctx.setClearColor("), 0)
        << "clear-color installation happens only inside beginPass";
    EXPECT_EQ(countOccurrences(content, "ctx.clearColor()"), 0)
        << "color clearing happens only inside beginPass";
}

// ---------------------------------------------------------------------------
// (6) geometryFor deduplicated into ONE registry helper
// ---------------------------------------------------------------------------

TEST(T17RendererConsolidation, PerRendererGeometryForCopiesRemoved) {
    // The former member-function copies looked like:
    //   data::Result<MeshGeometry*> XRenderer::geometryFor(...)
    // Their bodies were byte-identical apart from the renderer name.
    int memberDefs = 0;
    memberDefs += countInDir("render", "::geometryFor(");
    EXPECT_EQ(memberDefs, 0)
        << "no renderer may carry its own geometryFor copy; resolution goes "
           "through resolveMeshGeometry(registry, handle, name)";
}

TEST(T17RendererConsolidation, ResolveMeshGeometryDefinedExactlyOnce) {
    const std::string definition =
        readFile(kRepoRoot / "render" / "asset_registry.cpp");
    const std::string declaration =
        readFile(kRepoRoot / "render" / "asset_registry.hpp");
    ASSERT_FALSE(definition.empty());
    ASSERT_FALSE(declaration.empty());
    EXPECT_EQ(
        countOccurrences(definition,
                         "data::Result<MeshGeometry*> resolveMeshGeometry("),
        1)
        << "exactly ONE shared mesh-geometry resolver implementation exists, "
           "over the AssetRegistry (render/asset_registry.cpp)";
    EXPECT_EQ(
        countOccurrences(declaration,
                         "data::Result<MeshGeometry*> resolveMeshGeometry("),
        1)
        << "declared exactly once, beside the registry it resolves through";
}

// ---------------------------------------------------------------------------
// (7) Content-hash unity: one algorithm behind scene AND render identity
// ---------------------------------------------------------------------------

TEST(T17RendererConsolidation, DataAndSceneHashesAgreeOnMesh) {
    // A single triangle; the bytes are arbitrary but fixed so the equality is
    // reproducible. Two distinct allocations with identical bytes MUST hash
    // identically (hash of stable bytes, not pointer).
    const data::Mesh meshA = data::Mesh::fromTriangles(
        {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f),
         glm::vec3(0.0f, 1.0f, 0.0f)},
        {0u, 1u, 2u});
    const data::Mesh meshB = data::Mesh::fromTriangles(
        {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f),
         glm::vec3(0.0f, 1.0f, 0.0f)},
        {0u, 1u, 2u});

    EXPECT_EQ(data::computeContentHash(meshA), scene::computeContentHash(meshA))
        << "scene::computeContentHash forwards to the single data/ "
           "definition — both layers must agree on a mesh's identity";
    EXPECT_EQ(data::computeContentHash(meshA), data::computeContentHash(meshB))
        << "identical vertex/index bytes → identical hash (pointer-independent)";

    const data::Mesh meshC = data::Mesh::fromTriangles(
        {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(2.0f, 0.0f, 0.0f),
         glm::vec3(0.0f, 1.0f, 0.0f)},
        {0u, 1u, 2u});
    EXPECT_NE(data::computeContentHash(meshA), data::computeContentHash(meshC))
        << "FNV-1a folds every position byte, so changing a coordinate "
           "(here x: 1→2) flips the hash for these fixed inputs";
}

TEST(T17RendererConsolidation, DataAndSceneHashesAgreeOnVolumeAndImage) {
    const data::VolumeDataset volA(
        2u, 2u, 1u, {0.0f, 0.25f, 0.5f, 0.75f});
    const data::VolumeDataset volB(
        2u, 2u, 1u, {0.0f, 0.25f, 0.5f, 0.75f});
    const data::VolumeDataset volC(
        2u, 2u, 1u, {0.0f, 0.25f, 0.5f, 0.125f});

    EXPECT_EQ(data::computeContentHash(volA),
              scene::computeContentHash(volA))
        << "volume identity shares the single data/ hash definition";
    EXPECT_EQ(data::computeContentHash(volA), data::computeContentHash(volB));
    EXPECT_NE(data::computeContentHash(volA), data::computeContentHash(volC));

    const data::Image imgA(2, 1, 3, {10u, 20u, 30u, 40u, 50u, 60u});
    const data::Image imgB(2, 1, 3, {10u, 20u, 30u, 40u, 50u, 60u});
    const data::Image imgC(2, 1, 3, {10u, 20u, 30u, 41u, 50u, 60u});

    EXPECT_EQ(data::computeContentHash(imgA), scene::computeContentHash(imgA))
        << "image identity shares the single data/ hash definition";
    EXPECT_EQ(data::computeContentHash(imgA), data::computeContentHash(imgB));
    EXPECT_NE(data::computeContentHash(imgA), data::computeContentHash(imgC));
}

TEST(T17RendererConsolidation, RegistryNoLongerCarriesLocalByteHashes) {
    // The mesh/volume/image FNV twins were deleted from asset_registry.cpp;
    // only the RE-side PhongMaterial VALUE hash remains local by design
    // (identity defined on the material value crossing into render/, §12.4).
    // T7: lookupVolume/lookupImage lazy paths deleted (hashed at register time,
    // never per frame) — only register paths remain.
    const std::string registry =
        readFile(kRepoRoot / "render" / "asset_registry.cpp");
    ASSERT_FALSE(registry.empty());
    EXPECT_EQ(countOccurrences(registry, "uint64_t meshContentHash"), 0);
    EXPECT_EQ(countOccurrences(registry, "uint64_t volumeContentHash"), 0);
    EXPECT_EQ(countOccurrences(registry, "uint64_t imageContentHash"), 0);
    EXPECT_EQ(countOccurrences(registry, "data::computeContentHash("), 3)
        << "mesh register + volume register + image register "
           "resolve their content hashes through the ONE shared definition (T7 lookup deleted)";
}

} // namespace re::tests
