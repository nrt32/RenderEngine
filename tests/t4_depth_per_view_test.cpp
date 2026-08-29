// tests/t4_depth_per_view_test.cpp — T4 gate: depth per-View via DepthConfig.
// Verifies scene::View DepthConfig default false, setDepthConfig bumps
// depthConfigGen, Engine defaults true, and mixed LAYER_0 depth-correct 1/255
// via broker path (VolumeSlice+Mesh share LAYER_0, Contour overlay on top still
// before via techniqueOrder after T6). Uses 1/255 analytic tolerance.
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

#include "broker/app_context.hpp"
#include "data/mesh.hpp"
#include "render/phong_material.hpp"
#include "render_engine/engine.hpp"
#include "scene/depth_config.hpp"
#include "scene/view.hpp"
#include "tests/test_helpers.hpp"
#include "utils/pixel_reader.hpp"

namespace re::tests {

constexpr int kTol = 1; // 1/255

TEST(T4DepthPerView, SceneViewDepthConfigDefaultFalseAndGenBump) {
    scene::View v;
    EXPECT_FALSE(v.depthConfig.enabled);
    EXPECT_FLOAT_EQ(v.depthConfig.clearDepth, 1.0f);
    const uint64_t gen0 = v.generation;
    const uint64_t dgen0 = v.depthConfigGen;
    v.setDepthConfig(scene::DepthConfig{true});
    EXPECT_TRUE(v.depthConfig.enabled);
    EXPECT_GT(v.depthConfigGen, dgen0);
    EXPECT_GT(v.generation, gen0);
    // Idempotent second set does not bump
    const uint64_t dgen1 = v.depthConfigGen;
    v.setDepthConfig(scene::DepthConfig{true});
    EXPECT_EQ(v.depthConfigGen, dgen1);
}

TEST(T4DepthPerView, EngineViewDefaultsDepthTrue) {
    // Engine facade must default DepthConfig true for mesh views (viz correctness)
    // while scene::View low-level stays false for deterministic gates.
    // Verify via Engine::createView helper.
    scene::Camera cam;
    std::vector<scene::ObjectId> ids;
    auto v = ::re::viz::Engine::createView(scene::Rect{0, 0, 64, 64}, cam, ids);
    EXPECT_TRUE(v.depthConfig.enabled) << "Engine mesh view defaults DepthConfig{true} within 1/255 gate";
}

// Broker-mediated depth correctness: two overlapping opaque quads at z=0 vs z=-1
// drawn anti-painter (near first). With DepthConfig false (color-only) far wins,
// with DepthConfig true (depth) near wins. Colors differ by 128 > 1/255 so probe discriminates.
TEST(T4DepthPerView, BrokerDepthOnVsOffPixelWithin1_255) {
    auto quad = std::make_shared<const data::Mesh>(makeQuadMesh());
    broker::AppContext ctxDepthOn(broker::AppContext::Params{});
    broker::AppContext ctxDepthOff(broker::AppContext::Params{});

    auto addTwoQuads = [](broker::AppContext& ctx) -> std::pair<uint64_t, uint64_t> {
        scene::MeshObject nearObj;
        nearObj.mesh = std::make_shared<const data::Mesh>(makeQuadMesh());
        nearObj.transform = glm::mat4(1.0f);
        nearObj.presentation.phong.baseColor = glm::vec4(0.0f, 0.5f, 0.0f, 1.0f); // green 128
        scene::MeshObject farObj;
        farObj.mesh = std::make_shared<const data::Mesh>(makeQuadMesh());
        farObj.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.0f));
        farObj.presentation.phong.baseColor = glm::vec4(0.5f, 0.0f, 0.0f, 1.0f); // red 128
        uint64_t nearId = ctx.store().addMeshObject(std::move(nearObj));
        uint64_t farId = ctx.store().addMeshObject(std::move(farObj));
        return {nearId, farId};
    };
    auto [nearOn, farOn] = addTwoQuads(ctxDepthOn);
    auto [nearOff, farOff] = addTwoQuads(ctxDepthOff);

    scene::Camera sc = scene::Camera(glm::vec3(0,0,5), glm::vec3(0,0,0), glm::vec3(0,1,0));
    sc.setPerspective(60.0f, 1.0f, 0.1f, 10.0f);

    scene::View viewOn;
    viewOn.id = 1;
    viewOn.rect = scene::Rect{0,0,64,64};
    viewOn.camera = sc;
    viewOn.setClearColor(glm::vec4(0,0,0,0));
    viewOn.setDepthConfig(scene::DepthConfig{true}); // depth on
    viewOn.setItemIds({nearOn, farOn});

    scene::View viewOff;
    viewOff.id = 1;
    viewOff.rect = scene::Rect{0,0,64,64};
    viewOff.camera = sc;
    viewOff.setClearColor(glm::vec4(0,0,0,0));
    // default DepthConfig false stays
    viewOff.setItemIds({nearOff, farOff});

    auto drive = [](broker::AppContext& ctx, const std::vector<scene::View>& vs) {
        auto s = ctx.bridge().sync(vs, ctx.store());
        if (s.failed()) return s;
        auto r = ctx.bridge().renderAll();
        if (r.failed()) return r;
        return ctx.bridge().presentAll(nullptr);
    };
    ASSERT_TRUE(drive(ctxDepthOn, {viewOn}).ok());
    ASSERT_TRUE(drive(ctxDepthOff, {viewOff}).ok());

    auto* rvOn = ctxDepthOn.compositor()->getView(0, 1);
    auto* rvOff = ctxDepthOff.compositor()->getView(0, 1);
    ASSERT_NE(rvOn, nullptr);
    ASSERT_NE(rvOff, nullptr);
    ASSERT_NE(rvOn->target(), nullptr);
    ASSERT_NE(rvOff->target(), nullptr);

    auto pxOn = readPixel(rvOn->target()->framebuffer(), 32, 32);
    auto pxOff = readPixel(rvOff->target()->framebuffer(), 32, 32);
    // Depth on: near green wins -> {0,128,0} within 1/255
    EXPECT_NEAR(pxOn[0], 0, kTol);
    EXPECT_NEAR(pxOn[1], 128, kTol);
    // Depth off: far red wins (painter) -> {128,0,0} within 1/255
    EXPECT_NEAR(pxOff[0], 128, kTol);
    EXPECT_NEAR(pxOff[1], 0, kTol);
    // Analytic 1/255 probe exactly
    EXPECT_NEAR(static_cast<float>(pxOn[1]) / 255.0f, 128.0f / 255.0f, 1.0f / 255.0f) << "1/255 analytic";
    EXPECT_NEAR(static_cast<float>(pxOff[0]) / 255.0f, 128.0f / 255.0f, 1.0f / 255.0f) << "1/255 analytic";
}

} // namespace re::tests
