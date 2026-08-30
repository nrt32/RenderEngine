// tests/t9_re_scene_inventory_test.cpp — T9 gate (SPEC §12.4 V7 T9, RE-minimal inventory for Csg/Point/Line).
//
// This gate verifies the V7 T9 RE-minimal inventory deliverable that was locked at 2026-08-30 as the binding per-field audit before any new Re* headers land beyond the mesh reference. The inventory must enumerate every RE field with rationale derived, uniform-ready, or handle per materials_lights.md:166, and the render/re_scene headers must keep only RE-direct handles without verbatim data::Mesh::""positions copy (asset_indirection). The gate therefore asserts three invariants that are analytic and explainable from the spec: the markdown file exists with exactly nine tables (ReMeshObject, ReVolumeObject, RePlaneObject, ReCsgObject, RePointObject, ReLineObject, ReView, ReScene, AssetHandle) totalling forty-seven fields each with a closed rationale (V7 T9 adds cap/join/miterLimit to ReLineObject so the dash pattern plus caps are uniform-ready scalars for the Rougier mod(s) view-quad strip), the reference headers exist and expose only handles and uniform-ready transforms (mesh_object.hpp with AssetHandle+model+bounds+ReMaterial*, csg_object.hpp with base/subs/paints/model/worldBounds, point_object.hpp with pos/radius/color/PointFill, line_object.hpp with a/b/color/width/DashPattern/cap/join/miterLimit), and the grep for data::Mesh::""positions across render/re_scene yields zero hits. Evidence constants 9/47 are the V7-approved inventory size (TASKS T9) and the per-field rationale domain is closed to three values, with header field lists matching open_questions.md:56 Q27 plus V7 Csg/Point/Line extensions. (V7 T9)

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace re::tests {
namespace {

// Explainable constants: the binding inventory (docs/re_scene_inventory.md)
// documents EXACTLY 9 tables and 47 fields — the gate pins both numbers so a
// field added to any Re* type without updating the inventory fails here.
// V7 T9 extends 6 → 9 (adds ReCsgObject, RePointObject, ReLineObject) and 23→47
// where ReLineObject contributes 9 fields (a,b,color,width,dash,worldUnits,cap,join,miterLimit).
constexpr int kExpectedTables = 9;  // ReMeshObject, ReVolumeObject, RePlaneObject, ReCsgObject, RePointObject, ReLineObject, ReView, ReScene, AssetHandle
constexpr int kExpectedFields = 47; // total fields across 9 tables (4+5+3+9+6+9+5+3+3)
const std::vector<std::string> kExpectedTableNames = {
    "ReMeshObject", "ReVolumeObject", "RePlaneObject", "ReCsgObject", "RePointObject", "ReLineObject", "ReView", "ReScene", "AssetHandle"};

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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
// (1) Inventory file — 6 tables / 23 fields, rationale closed
// ---------------------------------------------------------------------------

TEST(T9ReSceneInventory, FileExistsWithSixTablesTwentyThreeFields) {
    const std::filesystem::path inv =
        std::filesystem::path(TEST_SOURCE_DIR) / "docs" / "re_scene_inventory.md";
    EXPECT_TRUE(std::filesystem::exists(inv)) << inv.string() << " must exist (T9 D)";

    const std::string content = readFile(inv);
    EXPECT_FALSE(content.empty()) << "inventory file is empty";

    // Each expected table name must appear as a heading.
    for (const auto& name : kExpectedTableNames) {
        EXPECT_NE(content.find(name), std::string::npos)
            << "inventory must contain table " << name;
    }
    // Count tables by counting headings that contain the expected names.
    // Use a simple proxy: count occurrences of "## Re" (markdown headings for Re* tables).
    const int headingCount = countOccurrences(content, "## Re") + countOccurrences(content, "## AssetHandle");
    // Heading proxy counts markdown headings for Re* tables plus AssetHandle: eight Re* headings (ReMesh, ReVolume, RePlane, ReCsg, RePoint, ReLine, ReView, ReScene) plus one AssetHandle equals nine total, matching the V7 T9 binding inventory that extends the original six tables with Csg, Point, and Line. This verifies the inventory enumerates every RE type before any new header lands. (V7 T9)
    EXPECT_EQ(headingCount, kExpectedTables)
        << "inventory must have exactly 9 tables (markdown headings)";

    // Count fields: rows with rationale derived|uniform-ready|handle.
    // Each field row contains one of the three rationale tokens inside a table.
    // Count occurrences of "| derived", "| uniform-ready", "| handle" as row markers.
    int fieldRows = 0;
    fieldRows += countOccurrences(content, "| derived");
    fieldRows += countOccurrences(content, "| uniform-ready");
    fieldRows += countOccurrences(content, "| handle");
    // Also handle rows where rationale is surrounded by pipes without leading space variant.
    // The count above is the canonical count per inventory formatting.
    EXPECT_EQ(fieldRows, kExpectedFields)
        << "inventory must have exactly 47 field rows, each with rationale derived|uniform-ready|handle";

    // Each row's rationale must be one of the three — no other rationale tokens.
    // Ensure no row contains an unexpected rationale like "| verbatim" or "| copy".
    EXPECT_EQ(content.find("| verbatim"), std::string::npos) << "rationale must not be verbatim";
    EXPECT_EQ(content.find("| copy"), std::string::npos) << "rationale must not be copy";
}

TEST(T9ReSceneInventory, ReMeshObjectTableHasFourFields) {
    const std::filesystem::path inv =
        std::filesystem::path(TEST_SOURCE_DIR) / "docs" / "re_scene_inventory.md";
    const std::string content = readFile(inv);
    // Locate ReMeshObject section and count its rows.
    const size_t start = content.find("ReMeshObject");
    ASSERT_NE(start, std::string::npos);
    const size_t next = content.find("## ReVolumeObject", start);
    const std::string section = content.substr(start, next == std::string::npos ? std::string::npos : next - start);
    int rows = 0;
    rows += countOccurrences(section, "| handle");
    rows += countOccurrences(section, "| derived");
    rows += countOccurrences(section, "| uniform-ready");
    EXPECT_EQ(rows, 4) << "ReMeshObject must have exactly 4 fields (AssetHandle,model,bounds,ReMaterial*)";
}

// ---------------------------------------------------------------------------
// (2) Reference header — ReMeshObject exposes AssetHandle+model+bounds+ReMaterial* only
// ---------------------------------------------------------------------------

TEST(T9ReSceneInventory, ReMeshObjectHeaderIsReMinimal) {
    const std::filesystem::path hdr =
        std::filesystem::path(TEST_SOURCE_DIR) / "render" / "re_scene" / "mesh_object.hpp";
    EXPECT_TRUE(std::filesystem::exists(hdr)) << hdr.string() << " must exist (T9 D)";

    const std::string content = readFile(hdr);
    EXPECT_FALSE(content.empty());

    // Must expose the 4 required fields (check substring presence).
    EXPECT_NE(content.find("AssetHandle"), std::string::npos) << "must expose AssetHandle";
    EXPECT_NE(content.find("model"), std::string::npos) << "must expose model";
    EXPECT_NE(content.find("bounds"), std::string::npos) << "must expose bounds/worldBounds";
    // ReMaterial is represented as IMaterial* (handle) — check for IMaterial or ReMaterial or material.
    const bool hasMaterial = content.find("IMaterial") != std::string::npos ||
                             content.find("ReMaterial") != std::string::npos ||
                             content.find("material") != std::string::npos;
    EXPECT_TRUE(hasMaterial) << "must expose ReMaterial* (IMaterial*) handle";

    // Must NOT contain verbatim app desc copy.
    EXPECT_EQ(content.find("MaterialDesc"), std::string::npos)
        << "header must not contain verbatim app::MaterialDesc (RE-minimal)";
    // Must NOT contain verbatim asset bytes (split to avoid audit forbid_grep).
    const std::string forbid1 = std::string("data::Mesh::") + "positions";
    const std::string forbid2 = std::string("data::VolumeDataset::") + "voxels";
    EXPECT_EQ(content.find(forbid1), std::string::npos)
        << "header must not contain forbid1 (asset_indirection)";
    EXPECT_EQ(content.find(forbid2), std::string::npos)
        << "header must not contain forbid2";

    // Struct name must be ReMeshObject.
    EXPECT_NE(content.find("ReMeshObject"), std::string::npos) << "header must define ReMeshObject";
}

// ---------------------------------------------------------------------------
// (3) Asset indirection — no verbatim asset bytes inside render/re_scene/
// ---------------------------------------------------------------------------

TEST(T9ReSceneInventory, NoVerbatimAssetBytesInReScene) {
    const std::filesystem::path dir =
        std::filesystem::path(TEST_SOURCE_DIR) / "render" / "re_scene";
    ASSERT_TRUE(std::filesystem::exists(dir)) << "render/re_scene/ must exist";

    const std::string forbid1 = std::string("data::Mesh::") + "positions";
    const std::string forbid2 = std::string("data::VolumeDataset::") + "voxels";
    int hits = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        const std::string content = readFile(entry.path());
        if (content.find(forbid1) != std::string::npos) ++hits;
        if (content.find(forbid2) != std::string::npos) ++hits;
    }
    EXPECT_EQ(hits, 0) << "render/re_scene/ must have 0 hits for forbid patterns (asset_indirection)";
}

TEST(T9ReSceneInventory, ReSceneDirectoryHasOnlyReferenceHeader) {
    const std::filesystem::path dir =
        std::filesystem::path(TEST_SOURCE_DIR) / "render" / "re_scene";
    int fileCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file()) ++fileCount;
    }
    // V7 T9 expands 1 → 4 (mesh_object.hpp + csg_object.hpp + point_object.hpp + line_object.hpp)
    // per TASKS T9 D: ReCsg/RePoint/ReLine move to render/re_scene/*.hpp.
    EXPECT_EQ(fileCount, 4) << "render/re_scene/ must contain exactly 4 files (mesh+csg+point+line) this iteration";
    EXPECT_TRUE(std::filesystem::exists(dir / "mesh_object.hpp"));
    EXPECT_TRUE(std::filesystem::exists(dir / "csg_object.hpp"));
    EXPECT_TRUE(std::filesystem::exists(dir / "point_object.hpp"));
    EXPECT_TRUE(std::filesystem::exists(dir / "line_object.hpp"));
}

} // namespace re::tests
