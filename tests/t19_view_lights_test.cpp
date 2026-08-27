// tests/t19_view_lights_test.cpp — T19 View explicit lights (stretch, deferred per SPEC §1/§12).
//
// D: scene::View and render::View gain vector<Light> lights (was implicit/absent).
// Light { Type dir/point/spot; vec3 pos/dir; vec4 color; float intensity; … } +
// setLights() bumping lightsGen (adds to CompositeKey dirty per §10). RE: ReLight
// uniform-ready uploaded per view before drawLayer loop; empty = unlit (2D).
// Broker: LightMapper : IMapper<Light,ReLight> + ViewMapper composes LightMapper.
// Persistence: lightsGen participates in ViewSynchronizer dirty check.
//
// Gate is stretch deferred (not required for V3 green while SPEC §1 Phong-only
// non-goal holds) — this test asserts the structural invariants that keep the
// future promotion byte-identical, with analytic constants per R4.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "broker/broker.hpp"
#include "broker/camera_mapper.hpp"
#include "broker/light_mapper.hpp"
#include "broker/view_compositor.hpp"
#include "broker/view_mapper.hpp"
#include "broker/view_synchronizer.hpp"
#include "render/light.hpp"
#include "scene/light.hpp"
#include "scene/translate_context.hpp"
#include "scene/view.hpp"

namespace re::tests {

// scene::View lightsGen per-field generation: setLights bumps exactly one per distinct value (per-field split keeps lights dirt isolated from rect/plane/camera/items so a light tweak dirties only LightMapper via lightsGen and CompositeKey, not the whole View; identical value does not bump, preserving idempotent sync behavior)
TEST(T19ViewLights, SetLightsBumpsLightsGenExactlyOne) {
    scene::View v;
    EXPECT_EQ(v.lightsGen, 0u) << "initial lightsGen is exactly 0";
    EXPECT_TRUE(v.lights.empty()) << "default lights empty = unlit/2D fallback (stretch)";
    const uint64_t gen0 = v.lightsGen;
    const uint64_t coarse0 = v.generation;

    scene::Light l0;
    l0.type = scene::LightType::Directional;
    l0.dir = glm::vec3(0, 0, -1);
    l0.color = glm::vec4(1, 0, 0, 1);
    l0.intensity = 1.5f;
    v.setLights({l0});
    EXPECT_EQ(v.lightsGen, gen0 + 1) << "distinct setLights bumps lightsGen by exactly 1";
    EXPECT_EQ(v.generation, coarse0 + 1) << "coarse generation +1 per per-field bump (SPEC §10.4)";
    EXPECT_EQ(v.lights.size(), 1u);

    // Idempotent set does NOT bump.
    v.setLights({l0});
    EXPECT_EQ(v.lightsGen, gen0 + 1) << "identical setLights does not bump (distinct check)";

    // Two-light vector distinct from single-light within analytic set size.
    scene::Light l1;
    l1.type = scene::LightType::Point;
    l1.pos = glm::vec3(1, 2, 3);
    l1.color = glm::vec4(0, 1, 0, 1);
    v.setLights({l0, l1});
    EXPECT_EQ(v.lights.size(), 2u) << "two lights on one view (analytic count 2)";
    EXPECT_EQ(v.lightsGen, gen0 + 2) << "second distinct change bumps to gen0+2";
}

// LightMapper pure translation is analytic per field: world-space forwarding normalizes direction to unit length and forwards color/intensity/radius/cone angles within 1e-6; the mapper is stateless and view-space conversion belongs to the shader's view matrix, not the mapper, so the translation stays pure and deterministic per the world-space decision
TEST(T19ViewLights, LightMapperForwardsAnalytic) {
    broker::LightMapper mapper;
    scene::Light app;
    app.type = scene::LightType::Directional;
    app.dir = glm::vec3(2, 0, 0); // non-unit to test normalization
    app.color = glm::vec4(0.5f, 0.25f, 0.75f, 1.0f);
    app.intensity = 2.0f;
    app.radius = 7.5f;
    app.innerCone = 0.3f;
    app.outerCone = 0.6f;

    scene::TranslateContext ctx; // world-space forwarding: the context is unused because LightMapper forwards world-space pos/dir directly and view-space conversion is the shader's responsibility, keeping the mapper pure
    auto r = mapper.map(app, ctx);
    ASSERT_TRUE(r.ok()) << "LightMapper map succeeds";
    const render::ReLight& rl = *r;
    EXPECT_EQ(rl.type, render::ReLightType::Directional);
    EXPECT_FLOAT_EQ(rl.dirWS.x, 1.0f) << "dir normalized (2,0,0) -> (1,0,0) within 1e-6";
    EXPECT_FLOAT_EQ(rl.dirWS.y, 0.0f);
    EXPECT_FLOAT_EQ(rl.dirWS.z, 0.0f);
    EXPECT_FLOAT_EQ(rl.color.r, 0.5f) << "color RGB forwarded within 1e-6";
    EXPECT_FLOAT_EQ(rl.color.g, 0.25f);
    EXPECT_FLOAT_EQ(rl.color.b, 0.75f);
    EXPECT_FLOAT_EQ(rl.intensity, 2.0f);
    EXPECT_FLOAT_EQ(rl.radius, 7.5f);
    EXPECT_FLOAT_EQ(rl.innerCone, 0.3f);
    EXPECT_FLOAT_EQ(rl.outerCone, 0.6f);
}

// Broker pair-key {Light,ReLight} via hash_combine gives distinct entries per ReT and typed miss on wrong ReT (the pair-key fix prevents UB static_cast on mismatched Re types; same AppT with different ReT hashes to distinct buckets and a wrong ReT returns nullptr instead of a type-punned pointer)
TEST(T19ViewLights, BrokerLightMapperPairKey) {
    auto broker = std::make_shared<broker::Broker>();
    broker->registerMapper<scene::Light, render::ReLight>(
        std::make_unique<broker::LightMapper>());
    auto* m = broker->get<scene::Light, render::ReLight>();
    ASSERT_NE(m, nullptr) << "Light pair-key registered, typed get finds it";

    // Wrong ReT must return nullptr (typed miss, not UB): the broker's pair-key hashes both AppT and ReT so a mismatched Re type hashes to a different bucket and misses cleanly, the same evidence pattern used for the earlier pair-key test that guards against type-punning
    struct WrongRe {};
    auto* wrong = broker->get<scene::Light, WrongRe>();
    EXPECT_EQ(wrong, nullptr) << "wrong ReT returns nullptr (typed miss)";
}

// ViewMapper composition: vector<Light> to vector<ReLight> count 2 analytic proves ViewMapper delegates per-element to LightMapper and preserves count and order; two lights on one view produce a composite distinct from single-light within the analytic count check, empty would be separate
TEST(T19ViewLights, ViewMapperComposesLightMapperTwoLights) {
    broker::LightMapper lm;
    scene::TranslateContext ctx;
    std::vector<scene::Light> appLights(2);
    appLights[0].type = scene::LightType::Directional;
    appLights[1].type = scene::LightType::Point;
    appLights[1].pos = glm::vec3(1, 1, 1);
    auto r = broker::ViewMapper::mapLights(appLights, &lm, ctx);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r->size(), 2u) << "two-light composite distinct from single-light (analytic count 2)";
    EXPECT_EQ((*r)[0].type, render::ReLightType::Directional);
    EXPECT_EQ((*r)[1].type, render::ReLightType::Point);

    // Single light count 1 distinct from 2.
    appLights.resize(1);
    auto r1 = broker::ViewMapper::mapLights(appLights, &lm, ctx);
    ASSERT_TRUE(r1.ok());
    EXPECT_EQ(r1->size(), 1u) << "single-light composite count exactly 1";
}

// Persistence: lightsGen participates in ViewSynchronizer per-field dirty check
TEST(T19ViewLights, SynchronizerLightsPerFieldDirty) {
    auto broker = std::make_shared<broker::Broker>();
    broker->registerMapper(std::make_unique<broker::LightMapper>());
    broker->registerMapper(std::make_unique<broker::CameraMapper>());
    auto comp = std::make_shared<broker::ViewCompositor>(broker);
    broker::ViewSynchronizer sync(broker, comp);
    scene::SceneStore scene;

    scene::View v;
    v.id = 11;
    v.rect = scene::Rect{0, 0, 320, 240};
    v.camera = scene::Camera(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    std::vector<scene::View> views{v};
    ASSERT_TRUE(sync.sync(views, scene, 0, comp.get()).ok());
    auto* rv = comp->getView(0, 11);
    ASSERT_NE(rv, nullptr);
    EXPECT_EQ(rv->lights().size(), 0u) << "initially empty lights = unlit as before (stretch)";

    // Add one directional light → ReView gains one ReLight (per-field, not whole-view dump)
    scene::Light l;
    l.type = scene::LightType::Directional;
    l.dir = glm::vec3(0, 0, -1);
    views[0].setLights({l});
    ASSERT_TRUE(sync.sync(views, scene, 0, comp.get()).ok());
    rv = comp->getView(0, 11);
    ASSERT_NE(rv, nullptr);
    EXPECT_EQ(rv->lights().size(), 1u) << "after lightsGen bump, ReView lights count exactly 1";
    EXPECT_EQ(rv->lights()[0].type, render::ReLightType::Directional);

    // Second sync with identical lights keeps identity and count (no map churn)
    auto* rvBefore = rv;
    ASSERT_TRUE(sync.sync(views, scene, 0, comp.get()).ok());
    EXPECT_EQ(comp->getView(0, 11), rvBefore) << "ReView identity stable when lights unchanged";
    EXPECT_EQ(rvBefore->lights().size(), 1u);
}

// render::View empty lights = unlit fallback preserves FR gates byte-identical (stretch note)
TEST(T19ViewLights, RenderViewEmptyLightsPreserved) {
    render::View rv(render::ViewRect{0, 0, 640, 480});
    EXPECT_TRUE(rv.lights().empty()) << "render View empty lights = unlit/default (stretch preserves gate)";
    std::vector<render::ReLight> two(2);
    two[0].type = render::ReLightType::Directional;
    two[1].type = render::ReLightType::Point;
    rv.setLights(two);
    EXPECT_EQ(rv.lights().size(), 2u) << "render View two lights stored distinct from empty (analytic 2 != 0)";
}

} // namespace re::tests
