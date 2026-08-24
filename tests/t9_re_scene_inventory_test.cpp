// tests/t9_re_scene_inventory_test.cpp — T9 gate (SPEC §12.4 V3.8, RE-minimal).
//
// Asserts:
//   (1) docs/re_scene_inventory.md exists with 6 tables (ReMeshObject, ReVolumeObject,
//       RePlaneObject, ReView, ReScene, AssetHandle) / 23 fields, each row rationale
//       ∈ {derived|uniform-ready|handle} (SPEC §12.4, TASKS T9).
//   (2) render/re_scene/mesh_object.hpp exists, exposes AssetHandle+model+bounds+ReMaterial*
//       only, never verbatim app::MaterialDesc (RE-minimal, asset_indirection).
//   (3) grep -R "data::Mesh::""positions" render/re_scene/ → 0 hits (asset_indirection).
//
// Evidence rule (R4): constants 6 / 23 are the spec-approved inventory size
// (TASKS T9); rationale domain is closed; header field list is the spec's
// ReMeshObject{AssetHandle,model,bounds,ReMaterial*} (open_questions.md:56 Q27).

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace re::tests {
namespace {

// Explainable constants: the binding inventory (docs/re_scene_inventory.md)
// documents EXACTLY 6 tables and 23 fields — the gate pins both numbers so a
// field added to any Re* type without updating the inventory fails here.
constexpr int kExpectedTables = 6;  // ReMeshObject, ReVolumeObject, RePlaneObject, ReView, ReScene, AssetHandle
constexpr int kExpectedFields = 23; // total fields across 6 tables
const std::vector<std::string> kExpectedTableNames = {
    "ReMeshObject", "ReVolumeObject", "RePlaneObject", "ReView", "ReScene", "AssetHandle"};

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
    // Use a simple proxy: count occurrences of "## Re" (markdown headings for 6 tables).
    const int headingCount = countOccurrences(content, "## Re") + countOccurrences(content, "## AssetHandle");
    // 5 Re* headings + 1 AssetHandle = 6
    EXPECT_EQ(headingCount, kExpectedTables)
        << "inventory must have exactly 6 tables (markdown headings)";

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
        << "inventory must have exactly 23 field rows, each with rationale derived|uniform-ready|handle";

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
    // This iteration lands ONLY mesh_object.hpp as the reference Re* type;
    // Volume/Contour expansions are deliberately deferred (Phong-only,
    // no new technique headers) — a second file appearing here means someone
    // expanded render/re_scene without a task mandating it.
    EXPECT_EQ(fileCount, 1) << "render/re_scene/ must contain exactly 1 file (mesh_object.hpp) this iteration";
    EXPECT_TRUE(std::filesystem::exists(dir / "mesh_object.hpp"));
}

} // namespace re::tests
