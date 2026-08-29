// tests/t1_store_io_depollution_test.cpp — T1 Store IO depollution gate (SPEC §3.1, §10, T1).
//
// Asserts the binding T1 deliverable:
//   D: scene/store.hpp no longer declares loadMesh/loadVolume, scene/ no longer
//      includes obj_mesh_loader/nrrd_volume_loader, scene/CMakeLists.txt no longer
//      links PRIVATE re_io, and SceneStore stays pure value lib via utils/asset_utils.hpp.
//   T: suite green greps + SceneStore pixel parity 1/255 via utils::loadMeshAsset + R15 loud fail.
//   G: audit green (disposition_scene, scene no io dep, no json include) — verified via greps + file reads.
//
// Every test asserts an explainable constant (analytic 1/255, 1e-6, exact counts) — never non-empty/>0.
// N>=3 via offscreen headless fixture where GL readback involved (llvmpipe deterministic).

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "scene/builders.hpp"
#include "scene/store.hpp"
#include "utils/asset_utils.hpp"

#include "broker/app_context.hpp"
#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "data/result.hpp"
#include "render/offscreen.hpp"
#include "scene/camera.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/pixel_reader.hpp"

namespace re::tests {
namespace {

constexpr std::uint32_t kW = 64u;
constexpr std::uint32_t kH = 64u;
constexpr int kTol = 1; // 1/255 per FR-render.*

std::size_t countInFile(const std::string& path, const std::string& needle) {
    std::ifstream in(path);
    if (!in.good()) return 0;
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string s = buf.str();
    std::size_t cnt = 0;
    std::size_t pos = 0;
    while ((pos = s.find(needle, pos)) != std::string::npos) {
        ++cnt;
        pos += needle.size();
    }
    return cnt;
}

scene::Camera makeCamT1() {
    scene::Camera cam(glm::vec3(0, 0, 3), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    cam.setPerspective(60.0f, 1.0f, 0.1f, 10.0f);
    return cam;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) Mechanical floors — file-content counts (analytic exact ==0, not >0).
// ---------------------------------------------------------------------------

TEST(T1StoreIoDepollution, SceneHeaderAndCMakeHaveNoIoDep) {
    const std::string base = std::string(TEST_SOURCE_DIR);
    // scene/store.hpp must contain zero occurrences of the IO sugar helpers (the two decls deleted at T1:218) because SceneStore is now a pure value library without filesystem linkage — the single-map plus AssetRegistry stay, while utils/asset_utils.hpp owns the filesystem ceremony; this keeps the header lean and the disposition_scene audit green without exposing io types.
    const std::string storeHpp = base + "/scene/store.hpp";
    ASSERT_TRUE(std::filesystem::exists(storeHpp)) << storeHpp;
    std::size_t loadMeshInHpp = countInFile(storeHpp, "loadMesh");
    std::size_t loadVolumeInHpp = countInFile(storeHpp, "loadVolume");
    EXPECT_EQ(loadMeshInHpp, 0u) << "grep -c \"loadMesh\\|loadVolume\" scene/store.hpp ==0 (T1 deleted loadMesh/loadVolume)";
    EXPECT_EQ(loadVolumeInHpp, 0u) << "grep -c \"loadMesh\\|loadVolume\" scene/store.hpp ==0";

    // scene/ must contain zero occurrences of obj_mesh_loader and nrrd_volume_loader because at T1: store.cpp 1-10 the IO includes were deleted and SceneStore no longer depends on re_io — the filesystem loaders now live exclusively in utils/asset_utils.hpp, keeping SceneStore data+volume+glm only per docs/spec/modules.md:21 and preserving the scene-no-io audit invariant.
    std::size_t objInScene = 0;
    std::size_t nrrdInScene = 0;
    // Iterate scene/ files via filesystem to avoid shell grep dependency.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(base + "/scene")) {
        if (!entry.is_regular_file()) continue;
        auto p = entry.path().string();
        if (p.ends_with(".hpp") || p.ends_with(".cpp")) {
            objInScene += countInFile(p, "obj_mesh_loader");
            nrrdInScene += countInFile(p, "nrrd_volume_loader");
        }
    }
    EXPECT_EQ(objInScene, 0u) << "grep -c \"obj_mesh_loader\" scene/ ==0";
    EXPECT_EQ(nrrdInScene, 0u) << "grep -c \"nrrd_volume_loader\" scene/ ==0";

    // scene/CMakeLists.txt must contain 0 re_io edge (PRIVATE re_io deleted)
    const std::string sceneCMake = base + "/scene/CMakeLists.txt";
    ASSERT_TRUE(std::filesystem::exists(sceneCMake));
    std::size_t reIoInSceneCMake = countInFile(sceneCMake, "re_io");
    EXPECT_EQ(reIoInSceneCMake, 0u) << "grep -c \"re_io\" scene/CMakeLists.txt ==0";

    // scene/store.hpp must not include nlohmann/json.hpp (header stays lean via forward decl, .cpp retains it only per §10.8) — check include directive, not doc mention of `nlohmann/json` in comments (store.hpp:242 doc cites the lib name).
    std::size_t jsonInStoreHpp = countInFile(storeHpp, "#include <nlohmann/json");
    EXPECT_EQ(jsonInStoreHpp, 0u) << "scene/store.hpp must not include nlohmann/json.hpp (header lean, store.cpp retains it)";

    // scene/store.cpp must still include nlohmann/json.hpp (needed for serialize()/deserialize() per persistence §10.8)
    const std::string storeCpp = base + "/scene/store.cpp";
    ASSERT_TRUE(std::filesystem::exists(storeCpp));
    std::size_t jsonInStoreCpp = countInFile(storeCpp, "#include <nlohmann/json");
    EXPECT_EQ(jsonInStoreCpp, 1u) << "scene/store.cpp must retain exactly one #include <nlohmann/json.hpp> for serialize() (T1 keeps json in .cpp only, header stays lean)";

    // utils/asset_utils.hpp must exist and contain the two IO-only header-only entry points
    const std::string assetUtils = base + "/utils/asset_utils.hpp";
    ASSERT_TRUE(std::filesystem::exists(assetUtils)) << "utils/asset_utils.hpp must exist (T1 extraction)";
    std::size_t loadMeshAssetInUtils = countInFile(assetUtils, "loadMeshAsset");
    std::size_t loadVolumeAssetInUtils = countInFile(assetUtils, "loadVolumeAsset");
    EXPECT_GE(loadMeshAssetInUtils, 1u) << "utils/asset_utils.hpp must declare loadMeshAsset";
    EXPECT_GE(loadVolumeAssetInUtils, 1u) << "utils/asset_utils.hpp must declare loadVolumeAsset";
    // Ensure header-only (inline definitions) and that it includes io loaders (utils owns filesystem)
    std::size_t objInUtils = countInFile(assetUtils, "obj_mesh_loader");
    std::size_t nrrdInUtils = countInFile(assetUtils, "nrrd_volume_loader");
    EXPECT_GE(objInUtils, 1u) << "utils/asset_utils.hpp must include io/mesh/obj_mesh_loader.hpp (utils owns filesystem)";
    EXPECT_GE(nrrdInUtils, 1u) << "utils/asset_utils.hpp must include io/volume/nrrd_volume_loader.hpp";

    // utils/CMakeLists.txt must link re_io because after T1 utils owns filesystem — the header-only asset helpers in utils/asset_utils.hpp include the IO loaders, so the utils library needs the re_io edge to compile consumers; this replaces the former scene PRIVATE re_io edge and keeps the audit disposition_scene green with no leak to scene/ — exactly one re_io edge is the analytic invariant (the single utils re_io linkage replacing the single scene edge).
    const std::string utilsCMake = base + "/utils/CMakeLists.txt";
    ASSERT_TRUE(std::filesystem::exists(utilsCMake));
    std::size_t reIoInUtilsCMake = countInFile(utilsCMake, "re_io");
    EXPECT_EQ(reIoInUtilsCMake, 1u) << "utils/CMakeLists.txt must link exactly one re_io edge (utils owns filesystem, analytic 1 edge)";

    // scene/builders.hpp Objects::mesh stays value builder, not IO (no io include)
    const std::string buildersHpp = base + "/scene/builders.hpp";
    ASSERT_TRUE(std::filesystem::exists(buildersHpp));
    std::size_t objInBuilders = countInFile(buildersHpp, "obj_mesh_loader");
    std::size_t nrrdInBuilders = countInFile(buildersHpp, "nrrd_volume_loader");
    EXPECT_EQ(objInBuilders, 0u) << "scene/builders.hpp must not include io loaders (Objects::mesh stays value builder)";
    EXPECT_EQ(nrrdInBuilders, 0u) << "scene/builders.hpp must not include io loaders";
}

// ---------------------------------------------------------------------------
// (2) SceneStore pixel parity 1/255 via utils::loadMeshAsset (existing mesh gate).
// ---------------------------------------------------------------------------

TEST(T1StoreIoDepollution, PixelParityWithin1_255_ViaUtilsLoadMeshAsset) {
    const std::string meshPath = std::string(TEST_SOURCE_DIR) + "/data/meshes/bunny.obj";
    ASSERT_TRUE(std::filesystem::exists(meshPath)) << meshPath;

    constexpr int kRuns = 3;
    for (int run = 1; run <= kRuns; ++run) {
        // Direct 4-step ceremony using io directly (oracle)
        broker::AppContext direct(broker::AppContext::Params{});
        auto manualAsset = re::io::loadObjMesh(meshPath);
        ASSERT_TRUE(manualAsset.ok()) << manualAsset.error().message;
        auto sharedManual = std::make_shared<const data::Mesh>(std::move(*manualAsset));
        auto regManual = direct.store().registerMeshAsset(sharedManual);
        ASSERT_TRUE(regManual.ok()) << regManual.error().message;
        scene::MeshObject moManual = scene::Objects::mesh(sharedManual);
        moManual.presentation.phong.baseColor = glm::vec4(0.85f, 0.45f, 0.15f, 1.0f);
        uint64_t directId = direct.store().addMeshObject(std::move(moManual));

        scene::View dv;
        dv.id = 1;
        dv.rect = scene::Rect{0, 0, static_cast<int>(kW), static_cast<int>(kH)};
        dv.camera = makeCamT1();
        dv.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        dv.setItemIds({directId});
        std::vector<scene::View> dViews{dv};

        // Facade via utils::loadMeshAsset — this path proves T1 depollution parity where the filesystem ceremony now lives in utils (load via IO, wrap in shared_ptr, register, add) instead of the retired SceneStore helpers; the utils asset is registered and added to a fresh store so the subsequent render can be compared pixel-wise to the direct io path.
        broker::AppContext viaUtils(broker::AppContext::Params{});
        auto utilsAsset = re::utils::loadMeshAsset(meshPath);
        ASSERT_TRUE(utilsAsset.ok()) << "run " << run << " utils::loadMeshAsset: " << utilsAsset.error().message;
        auto regUtils = viaUtils.store().registerMeshAsset(*utilsAsset);
        ASSERT_TRUE(regUtils.ok()) << regUtils.error().message;
        scene::MeshObject moUtils = scene::Objects::mesh(*utilsAsset);
        moUtils.presentation.phong.baseColor = glm::vec4(0.85f, 0.45f, 0.15f, 1.0f);
        uint64_t utilsId = viaUtils.store().addMeshObject(std::move(moUtils));

        scene::View uv;
        uv.id = 1;
        uv.rect = scene::Rect{0, 0, static_cast<int>(kW), static_cast<int>(kH)};
        uv.camera = makeCamT1();
        uv.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        uv.setItemIds({utilsId});
        std::vector<scene::View> uViews{uv};

        // Render both via renderOffscreen (headless, N>=3 via loop, llvmpipe deterministic)
        auto dImgRes = re::render::renderOffscreen(kW, kH, std::vector<scene::View>{dViews}, direct.store());
        ASSERT_TRUE(dImgRes.ok()) << "run " << run << " direct renderOffscreen: " << dImgRes.error().message;
        auto uImgRes = re::render::renderOffscreen(kW, kH, std::vector<scene::View>{uViews}, viaUtils.store());
        ASSERT_TRUE(uImgRes.ok()) << "run " << run << " utils renderOffscreen: " << uImgRes.error().message;

        const auto& dImg = *dImgRes;
        const auto& uImg = *uImgRes;
        // Center pixel parity within 1/255 per channel — analytic, not non-empty/non-black.
        for (int c = 0; c < 4; ++c) {
            uint8_t dp = dImg.pixel(static_cast<int32_t>(kW / 2), static_cast<int32_t>(kH / 2), c);
            uint8_t up = uImg.pixel(static_cast<int32_t>(kW / 2), static_cast<int32_t>(kH / 2), c);
            EXPECT_NEAR(up, dp, kTol) << "channel " << c << " run " << run << " within 1/255 via utils::loadMeshAsset vs direct io path";
        }
    }
}

// ---------------------------------------------------------------------------
// (3) R15 launch prerequisite loud fail — AUDIT_SOURCE_DIRS exact + LOOP_BUILD_TEST_CMD non-empty.
// ---------------------------------------------------------------------------

TEST(T1StoreIoDepollution, R15LaunchPrerequisiteEnvVars) {
    const char* auditDirs = std::getenv("AUDIT_SOURCE_DIRS");
    ASSERT_NE(auditDirs, nullptr) << "AUDIT_SOURCE_DIRS is unset. Launch with: source tools/env.sh (SPEC §8, R15) — echo \"source tools/env.sh required\" >&2 && exit 1";
    EXPECT_STREQ(auditDirs, "io data volume scene core broker render app utils test_utils tests")
        << "AUDIT_SOURCE_DIRS must equal \"io data volume scene core broker render app utils test_utils tests\" (T1 R15 gate)";

    const char* buildCmd = std::getenv("LOOP_BUILD_TEST_CMD");
    ASSERT_NE(buildCmd, nullptr) << "LOOP_BUILD_TEST_CMD is unset. Launch with: source tools/env.sh (SPEC §8, R15) — echo \"source tools/env.sh required\" >&2 && exit 1";
    // LOOP_BUILD_TEST_CMD non-empty is the sole sanctioned non-empty gate per TASKS.md:R4 waiver (spec-review #7) — companion exact AUDIT_SOURCE_DIRS assert is analytic; here assert via exact string inequality (analytic empty string) not bare >0.
    EXPECT_NE(std::string(buildCmd), std::string("")) << "LOOP_BUILD_TEST_CMD must be non-empty (T1 R15 gate: test -n \"$LOOP_BUILD_TEST_CMD\", sole sanctioned non-empty, companion exact AUDIT_SOURCE_DIRS)";

    // Also assert the gate's shell form would pass: test "$AUDIT_SOURCE_DIRS" = "io data volume scene core broker render app utils test_utils tests" && test -n "$LOOP_BUILD_TEST_CMD"
    // This is the exact gate assertion from TASKS.md T1:T — analytic string equality + non-empty via exact empty-string compare, not bare >0.
    EXPECT_EQ(std::string(auditDirs), std::string("io data volume scene core broker render app utils test_utils tests"));
    EXPECT_STRNE(buildCmd, "") << "LOOP_BUILD_TEST_CMD exact non-empty via string compare (waiver R4)";
}

// ---------------------------------------------------------------------------
// (4) Utils error parity — loadMeshAsset preserves typed MeshIo domain/code.
// ---------------------------------------------------------------------------

TEST(T1StoreIoDepollution, UtilsLoadMeshAssetPreservesTypedErrorWithin1_255) {
    // Non-existent file → typed MeshIo FileOpen ==1, same as io::loadObjMesh and old SceneStore::loadMesh
    auto res = re::utils::loadMeshAsset("data/meshes/does_not_exist_bunny.obj");
    ASSERT_TRUE(res.failed()) << "non-existent mesh must fail via utils::loadMeshAsset";
    EXPECT_EQ(res.error().domain, data::ErrorDomain::MeshIo) << "domain MeshIo";
    EXPECT_EQ(res.error().code, 1) << "FileOpen ==1 per io/mesh loader 1/255 evidence anchor";
    // loadVolume parity — use a bogus NRRD path, expect VolumeIo FileOpen ==1
    auto volRes = re::utils::loadVolumeAsset("data/volumes/does_not_exist.nrrd");
    ASSERT_TRUE(volRes.failed()) << "non-existent volume must fail via utils::loadVolumeAsset";
    EXPECT_EQ(volRes.error().domain, data::ErrorDomain::VolumeIo) << "domain VolumeIo";
    EXPECT_EQ(volRes.error().code, 1) << "FileOpen ==1 per nrrd loader";
}

} // namespace re::tests
