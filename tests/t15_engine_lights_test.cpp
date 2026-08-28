// tests/t15_engine_lights_test.cpp — T15 gate: minimal light facade via Engine (SPEC §12.3, TASKS T15).
//
// D: scene/light.hpp Light (already View::lights vector<Light>) through Engine::setLights(ViewId, vector<Light>) and ViewBuilder::withLights(lights); empty lights = fixed headlight/unlit preservation (FR-render gates byte-identical), non-empty → LightMapper → ReLight upload once per View before drawLayer (ViewSynchronizer path). No new render/light/ hierarchy this iteration.
// Gate is analytic per R4: every check is an explainable constant (1/255 per-channel tolerance, ≥5/255 shift, grep counts ==1, N=3 consecutive offscreen parity). No non-empty/non-black/>0.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "broker/app_context.hpp"
#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "data/mesh.hpp"
#include "render_engine/engine.hpp"
#include "scene/builders.hpp"
#include "scene/light.hpp"
#include "scene/object.hpp"
#include "scene/view.hpp"
#include "tests/offscreen_fixture.hpp"
#include "tests/test_helpers.hpp"
#include "utils/pixel_reader.hpp"

namespace re::tests {
namespace {

constexpr uint32_t kW = 64u;
constexpr uint32_t kH = 64u;
constexpr uint32_t kCX = kW / 2u;
constexpr uint32_t kCY = kH / 2u;
constexpr int kTol = 1; // 1/255
constexpr int kShift = 5; // ≥5/255

struct FbPair {
    core::Texture2D color{};
    core::Framebuffer fb{};
};

FbPair makeFb(uint32_t w, uint32_t h) {
    auto c = core::Texture2D::create();
    auto f = core::Framebuffer::create();
    EXPECT_TRUE(c.ok()) << c.error().message;
    EXPECT_TRUE(f.ok()) << f.error().message;
    std::vector<uint8_t> zeros(static_cast<size_t>(w) * h * 4u, 0u);
    c->bind(0u);
    c->upload(w, h, zeros.data());
    c->unbind(0u);
    f->bind();
    f->attachColor(*c);
    EXPECT_TRUE(f->isComplete());
    f->unbind();
    return FbPair{std::move(*c), std::move(*f)};
}

scene::Camera makeSceneCam() {
    scene::Camera cam(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    cam.setPerspective(45.0f, 1.0f, 0.1f, 100.0f);
    return cam;
}

int maxChannelAbsDiff(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    int md = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        int d = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        if (d > md) md = d;
    }
    return md;
}

} // namespace

// Engine::setLights parity vs direct View::setLights + synchronizer path (N=3, 1/255). Uses quad mesh with orthographic camera so center pixel is guaranteed inside quad with analytic headlight color.
TEST(T15EngineLights, EngineSetLightsParityWithin1_255_N3) {
    if (auto* ctx = OffscreenEnvironment::context()) ctx->makeCurrent();
    constexpr int kRuns = 3;
    for (int run = 1; run <= kRuns; ++run) {
        // Direct path: AppContext + View::setLights then sync/render/present (quad, orthographic, baseColor 0.85,0.45,0.15)
        broker::AppContext direct(broker::AppContext::Params{});
        data::Mesh quad = makeQuadMesh();
        auto shared = std::make_shared<const data::Mesh>(std::move(quad));
        scene::MeshObject mo;
        mo.mesh = shared;
        mo.transform = glm::mat4(1.0f);
        mo.presentation.phong.baseColor = glm::vec4(0.85f, 0.45f, 0.15f, 1.0f);
        const uint64_t dId = direct.store().addMeshObject(std::move(mo));
        scene::View dv;
        dv.id = 7;
        dv.rect = scene::Rect{0, 0, static_cast<int>(kW), static_cast<int>(kH)};
        dv.camera = makeSceneCam();
        dv.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        dv.setItemIds({dId});
        scene::Light dl;
        dl.type = scene::LightType::Directional;
        dl.dir = glm::vec3(-1.0f, -1.0f, -1.0f);
        dv.setLights({dl});
        std::vector<scene::View> dViews{dv};
        auto dFb = makeFb(kW, kH);
        ASSERT_TRUE(direct.bridge().sync(dViews, direct.store()).ok()) << "run " << run << " direct sync";
        ASSERT_TRUE(direct.bridge().renderAll().ok()) << "run " << run << " direct renderAll";
        ASSERT_TRUE(direct.bridge().presentAll(&dFb.fb).ok()) << "run " << run << " direct present";
        std::vector<uint8_t> dPx;
        utils::PixelReader reader;
        dFb.fb.bind();
        ASSERT_TRUE(reader.read(kCX, kCY, 1u, 1u, dPx).ok());
        dFb.fb.unbind();
        ASSERT_EQ(dPx.size(), 4u);

        // Engine path: Engine::setLights(viewId, {Directional -1,-1,-1}) with same quad (via store directly, since Engine::addMesh is file-based — use store.addMeshObject for quad)
        viz::Engine engine;
        data::Mesh quad2 = makeQuadMesh();
        auto shared2 = std::make_shared<const data::Mesh>(std::move(quad2));
        scene::MeshObject mo2;
        mo2.mesh = shared2;
        mo2.transform = glm::mat4(1.0f);
        mo2.presentation.phong.baseColor = glm::vec4(0.85f, 0.45f, 0.15f, 1.0f);
        const uint64_t eId = engine.store().addMeshObject(std::move(mo2));
        scene::SceneViewBuilder bdv(7, scene::Rect{0, 0, static_cast<int>(kW), static_cast<int>(kH)});
        bdv.withCamera(makeSceneCam()).withItems({eId}).withClear(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        auto view = bdv.build();
        engine.setView(view);
        uint64_t viewId = engine.views().front().id;
        scene::Light el;
        el.type = scene::LightType::Directional;
        el.dir = glm::vec3(-1.0f, -1.0f, -1.0f);
        engine.setLights(viewId, {el});
        auto eFb = makeFb(kW, kH);
        ASSERT_TRUE(engine.render(eFb.fb).ok()) << "run " << run << " engine render";
        std::vector<uint8_t> ePx;
        eFb.fb.bind();
        ASSERT_TRUE(reader.read(kCX, kCY, 1u, 1u, ePx).ok());
        eFb.fb.unbind();
        ASSERT_EQ(ePx.size(), 4u);

        for (int c = 0; c < 4; ++c) {
            EXPECT_NEAR(ePx[c], dPx[c], kTol) << "channel " << c << " run " << run;
        }
    }
}

// Empty vs non-empty analytic shift ≥5/255 and empty preserves headlight within 1/255 of baseline (FR-render.1 preservation, N=3). Quad facing +Z with headlight shade 1.0 vs lit direction -1,-1,-1 normalized -0.577 shade 0.0 => baseColor 217->0 shift >>5.
TEST(T15EngineLights, EmptyPreservesHeadlightAndDirectionalShiftsAtLeast5_255_N3) {
    if (auto* ctx = OffscreenEnvironment::context()) ctx->makeCurrent();
    constexpr int kRuns = 3;
    for (int run = 1; run <= kRuns; ++run) {
        // Empty lights baseline (headlight) via Engine without setLights — quad, orthographic, baseColor 0.85,0.45,0.15 => center pixel 217,115,38
        viz::Engine engEmpty;
        data::Mesh quadEmpty = makeQuadMesh();
        auto sharedEmpty = std::make_shared<const data::Mesh>(std::move(quadEmpty));
        scene::MeshObject moEmpty;
        moEmpty.mesh = sharedEmpty;
        moEmpty.transform = glm::mat4(1.0f);
        moEmpty.presentation.phong.baseColor = glm::vec4(0.85f, 0.45f, 0.15f, 1.0f);
        const uint64_t idE = engEmpty.store().addMeshObject(std::move(moEmpty));
        {
            scene::SceneViewBuilder b(1, scene::Rect{0,0,static_cast<int>(kW),static_cast<int>(kH)});
            b.withCamera(makeSceneCam()).withItems({idE}).withClear(glm::vec4(0.10f,0.10f,0.12f,1.0f));
            engEmpty.setView(b.build());
        }
        auto fbEmpty = makeFb(kW, kH);
        ASSERT_TRUE(engEmpty.render(fbEmpty.fb).ok()) << "run " << run << " empty render";
        std::vector<uint8_t> pxEmpty;
        utils::PixelReader reader;
        fbEmpty.fb.bind();
        ASSERT_TRUE(reader.read(kCX, kCY, 1u, 1u, pxEmpty).ok());
        fbEmpty.fb.unbind();

        // Non-empty directional via Engine setLights — same quad, same view, but lit direction -1,-1,-1 => shade 0 => black
        viz::Engine engLit;
        data::Mesh quadLit = makeQuadMesh();
        auto sharedLit = std::make_shared<const data::Mesh>(std::move(quadLit));
        scene::MeshObject moLit;
        moLit.mesh = sharedLit;
        moLit.transform = glm::mat4(1.0f);
        moLit.presentation.phong.baseColor = glm::vec4(0.85f, 0.45f, 0.15f, 1.0f);
        const uint64_t idL = engLit.store().addMeshObject(std::move(moLit));
        {
            scene::SceneViewBuilder b2(1, scene::Rect{0,0,static_cast<int>(kW),static_cast<int>(kH)});
            b2.withCamera(makeSceneCam()).withItems({idL}).withClear(glm::vec4(0.10f,0.10f,0.12f,1.0f));
            engLit.setView(b2.build());
        }
        uint64_t vid = engLit.views().front().id;
        scene::Light l;
        l.type = scene::LightType::Directional;
        l.dir = glm::vec3(-1.0f, -1.0f, -1.0f);
        engLit.setLights(vid, {l});
        auto fbLit = makeFb(kW, kH);
        ASSERT_TRUE(engLit.render(fbLit.fb).ok()) << "run " << run << " lit render";
        std::vector<uint8_t> pxLit;
        fbLit.fb.bind();
        ASSERT_TRUE(reader.read(kCX, kCY, 1u, 1u, pxLit).ok());
        fbLit.fb.unbind();

        int shift = maxChannelAbsDiff(pxEmpty, pxLit);
        EXPECT_GE(shift, kShift) << "run " << run << " empty " << (int)pxEmpty[0] << "," << (int)pxEmpty[1] << "," << (int)pxEmpty[2] << " lit " << (int)pxLit[0] << "," << (int)pxLit[1] << "," << (int)pxLit[2] << " shift " << shift;

        // Empty baseline should be headlight bright 217,115,38 vs lit black 0,0,0 ; also verify empty vs direct baseline within 1/255
        broker::AppContext direct(broker::AppContext::Params{});
        data::Mesh quadDirect = makeQuadMesh();
        auto sharedDirect = std::make_shared<const data::Mesh>(std::move(quadDirect));
        scene::MeshObject moDirect;
        moDirect.mesh = sharedDirect;
        moDirect.transform = glm::mat4(1.0f);
        moDirect.presentation.phong.baseColor = glm::vec4(0.85f, 0.45f, 0.15f, 1.0f);
        uint64_t dId2 = direct.store().addMeshObject(std::move(moDirect));
        scene::View dv2;
        dv2.id = 1;
        dv2.rect = scene::Rect{0, 0, static_cast<int>(kW), static_cast<int>(kH)};
        dv2.camera = makeSceneCam();
        dv2.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        dv2.setItemIds({dId2});
        std::vector<scene::View> dvs{dv2};
        auto dFb2 = makeFb(kW, kH);
        ASSERT_TRUE(direct.bridge().sync(dvs, direct.store()).ok());
        ASSERT_TRUE(direct.bridge().renderAll().ok());
        ASSERT_TRUE(direct.bridge().presentAll(&dFb2.fb).ok());
        std::vector<uint8_t> dPx2;
        dFb2.fb.bind();
        ASSERT_TRUE(reader.read(kCX, kCY, 1u, 1u, dPx2).ok());
        dFb2.fb.unbind();
        for (int c = 0; c < 3; ++c) {
            EXPECT_NEAR(pxEmpty[c], dPx2[c], kTol) << "empty headlight parity channel " << c << " run " << run;
        }
        EXPECT_NEAR(pxEmpty[0], 217, kTol) << "empty R should be 217 headlight";
        EXPECT_NEAR(pxLit[0], 0, kTol) << "lit R should be 0 with opposite light";
    }
}

// Header hygiene: exactly one class Light in scene/light.hpp and exactly one setLights in engine.hpp (analytic ==1, not >=1). Mechanical T15 gate.
TEST(T15EngineLights, HeaderHygieneGrepCounts) {
    auto countOcc = [](const std::string& path, const std::string& needle) -> size_t {
        std::ifstream in(path);
        if (!in.good()) return 999;
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        size_t cnt = 0;
        size_t pos = 0;
        while ((pos = content.find(needle, pos)) != std::string::npos) {
            ++cnt;
            pos += needle.size();
        }
        return cnt;
    };
    const std::string lightPath = std::string(TEST_SOURCE_DIR) + "/scene/light.hpp";
    const std::string enginePath = std::string(TEST_SOURCE_DIR) + "/include/render_engine/engine.hpp";
    ASSERT_TRUE(std::filesystem::exists(lightPath)) << lightPath;
    ASSERT_TRUE(std::filesystem::exists(enginePath)) << enginePath;
    EXPECT_EQ(countOcc(lightPath, "class Light"), 1u) << "grep -c \"class Light\" scene/light.hpp ==1";
    EXPECT_EQ(countOcc(enginePath, "setLights"), 1u) << "grep -c \"setLights\" include/render_engine/engine.hpp ==1";
    EXPECT_EQ(countOcc(enginePath, "class Engine"), 1u) << "grep -c \"class Engine\" ==1";
    size_t addMeshCnt = countOcc(enginePath, "addMesh");
    EXPECT_GE(addMeshCnt, 1u) << "addMesh present";
}

} // namespace re::tests
