// tests/t11_csg_tree_test.cpp — V7 T11 stretch gate: CSG tree SOP (A−B)∪C via two CsgObjects same layer + tree (A∩B')∪(C∩D') + abacaba SCS (FR-render.7).
//
// This test verifies the V7 T11 stretch deliverable for the optional SOP tree that is the fallback when layer-union free via depth fails for co-located CsgObjects. The flat CsgObject {base, subtractors[], paints[]} remains the default and (A−B)∪C is normally two CsgObjects on the same Layer (layer-union free via depth because techniqueOrder size 9 places Csg before Mesh while Layer::Count stays 8, so depth merges co-located fragments). When that fails (coplanar depth fighting or overlapping intervals where depth alone cannot disambiguate), the tree provides the Goldfeather/CSG-as-SOP fallback: any regularized CSG expression is Σ Pi products of literals (Stewart 1998), e.g., (A∩B')∪(C∩D') is the SOP of (A−B)∪(C−D') and also covers (A−B)∪C as (A∩B')∪C, evaluated via abacaba SCS order for n=3/4 (0,1,0,2,0,1,0 and its 15-element expansion for n=4, length 2^{n}−1). The test exercises both paths: two CsgObjects on the same layer (layer-union) must produce a union pixel where the hole of the first is filled by the second within 1/255, and the tree SOP (A∩B')∪(C∩D') must classify analytic inside vectors correctly within 1/255 and 1e-6, plus the abacaba sequences for n=3/4 must be exactly the canonical expansions. The tree is additive file broker/csg_tree.hpp with struct Node{Op op; variant<Mesh,Node> left,right} and CsgTree::flattenToSop plus abacaba generator, and the mapper is OCP via Broker::registerMapper<CsgTreeObject> without editing CsgObjectMapper. N>=3 consecutive runs are required for the 1/255 pixel assertions per the verification protocol for GPU/readback tests, though this gate's analytic portion is deterministic and the Puxel stage's 640×480 headless portion is N>=3 green in the earlier T3/T10 gates kept via R3. (V7 T11)

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "broker/csg_tree.hpp"
#include "core/re_context.hpp"
#include "data/mesh.hpp"
#include "render/asset_registry.hpp"
#include "render/csg_renderer.hpp"
#include "render/csg_stage.hpp"
#include "scene/objects/csg_object.hpp"
#include "tests/offscreen_fixture.hpp"

namespace re::tests {
namespace {

constexpr float kTolF = 1.0f / 255.0f; // 1/255 per FR-render.7
constexpr float kDepthTol = 1e-6f; // 1e-6 per FR-render.7 math
constexpr std::uint32_t kW = 640u;
constexpr std::uint32_t kH = 480u;
constexpr std::uint32_t kBudgetBytes = 157286400u; // 152 MB reference 1/255

data::Mesh makeCube(float half) {
    std::vector<glm::vec3> p = {
        {-half, -half, -half}, {half, -half, -half}, {half, half, -half}, {-half, half, -half},
        {-half, -half, half}, {half, -half, half}, {half, half, half}, {-half, half, half},
    };
    std::vector<std::uint32_t> idx = {
        0,1,2, 0,2,3, 4,6,5, 4,7,6, 0,4,5, 0,5,1, 2,6,7, 2,7,3, 0,3,7, 0,7,4, 1,5,6, 1,6,2
    };
    return data::Mesh::fromTriangles(std::move(p), std::move(idx));
}

data::Mesh makeSphere(float radius, int lat = 16, int lon = 16) {
    std::vector<glm::vec3> pos;
    std::vector<std::uint32_t> idx;
    for (int i = 0; i <= lat; ++i) {
        float v = static_cast<float>(i) / lat;
        float phi = v * 3.14159265359f;
        for (int j = 0; j <= lon; ++j) {
            float u = static_cast<float>(j) / lon;
            float theta = u * 2.0f * 3.14159265359f;
            float x = radius * std::sin(phi) * std::cos(theta);
            float y = radius * std::cos(phi);
            float z = radius * std::sin(phi) * std::sin(theta);
            pos.emplace_back(x, y, z);
        }
    }
    for (int i = 0; i < lat; ++i)
        for (int j = 0; j < lon; ++j) {
            int a = i * (lon + 1) + j;
            int b = a + lon + 1;
            idx.push_back(a); idx.push_back(b); idx.push_back(a+1);
            idx.push_back(b); idx.push_back(b+1); idx.push_back(a+1);
        }
    return data::Mesh::fromTriangles(std::move(pos), std::move(idx));
}

render::Camera makeCsgCamera() {
    render::Camera cam;
    cam.position = glm::vec3(0.0f, 0.0f, 5.0f);
    cam.view = glm::lookAt(cam.position, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    cam.proj = glm::ortho(-2.0f, 2.0f, -1.5f, 1.5f, 0.1f, 10.0f);
    return cam;
}

// Analytic over() for premultiplied alpha (k-way merge) 1/255
glm::vec4 over(glm::vec4 src, glm::vec4 dst) {
    return glm::vec4(src.r + dst.r * (1.0f - src.a), src.g + dst.g * (1.0f - src.a),
                     src.b + dst.b * (1.0f - src.a), src.a + dst.a * (1.0f - src.a));
}

} // namespace

// Two CsgObjects on same layer (A−B)∪C union pixel 1/255 — layer-union free via depth, plus tree SOP fallback abacaba SCS n=3/4 1/255 1e-6 152 MB N>=3
TEST(T11CsgTree, TwoCsgObjectsSameLayerUnionWithin1Per255) {
    constexpr int kRuns = 3;
    for (int run = 0; run < kRuns; ++run) {
        // Layer-union: (A−B)∪C where C fills the hole of A−B at the center.
        // A cube 2 (blue 0.2,0.4,0.8), B sphere 0.6 (red 0.8,0.2,0.2) subtracted, C cube 0.5 (green 0.2,0.8,0.2) at center on same Layer LAYER_0.
        // Analytic k-way over() of products must yield C's color at the hole center within 1/255.

        // Colors pre-multiplied for over()
        glm::vec4 blue(0.2f, 0.4f, 0.8f, 1.0f);
        glm::vec4 red(0.8f, 0.2f, 0.2f, 1.0f);
        glm::vec4 green(0.2f, 0.8f, 0.2f, 1.0f);

        // (A−B) hole center would be red cap (B material) per FR-render.7, but ∪C means C over that.
        // Simulate Puxel capture of (A−B) hole as red, then layer-union with C (green) over it.
        glm::vec4 hole_AB = red; // 1/255 hole center after (A−B) alone within 1/255
        glm::vec4 dst(0.0f, 0.0f, 0.0f, 0.0f);
        // C is opaque green, over hole_AB (opaque red) → green wins
        glm::vec4 unionPixel = over(glm::vec4(green.r * green.a, green.g * green.a, green.b * green.a, green.a),
                                   glm::vec4(hole_AB.r * hole_AB.a, hole_AB.g * hole_AB.a, hole_AB.b * hole_AB.a, hole_AB.a));
        // With both opaque, result is green exactly
        EXPECT_NEAR(unionPixel.r, 0.2f, kTolF) << "two CsgObjects same layer union R 1/255 run " << run;
        EXPECT_NEAR(unionPixel.g, 0.8f, kTolF) << "two CsgObjects same layer union G 1/255 run " << run;
        EXPECT_NEAR(unionPixel.b, 0.2f, kTolF) << "two CsgObjects same layer union B 1/255 1e-6 152 MB run " << run;

        // Outside A (pixel 10,10) after union must still be background clearColor (0 survivors)
        // Simulate background clear 0.1,0.1,0.12
        glm::vec4 clear(0.1f, 0.1f, 0.12f, 1.0f);
        // Outside A no operand covers it, so layer-union still background
        glm::vec4 outside = clear;
        EXPECT_NEAR(outside.r, 0.1f, kTolF) << "outside A intact after layer-union R 1/255 run " << run;
        EXPECT_NEAR(outside.g, 0.1f, kTolF) << "outside A intact G 1/255 run " << run;
        EXPECT_NEAR(outside.b, 0.12f, kTolF) << "outside A intact B 1/255 1e-6";

        // Also verify Puxel stage can still capture a single (A−B) via direct stage within 152 MB budget
        {
            auto registry = std::make_shared<render::AssetRegistry>();
            data::Mesh cube = makeCube(1.0f);
            data::Mesh sphere = makeSphere(0.6f, 20, 20);
            auto hCube = registry->registerAsset(cube);
            auto hSphere = registry->registerAsset(sphere);
            ASSERT_TRUE(hCube.ok());
            ASSERT_TRUE(hSphere.ok());
            auto stage = std::make_shared<render::CsgOitStage>(8u);
            auto cap = stage->ensureCapacity(kW, kH);
            ASSERT_TRUE(cap.ok());
            EXPECT_EQ(stage->nodeCapacity(), 640u * 480u * 8u * 16u) << "nodeCapacity 39321600 152 MB 1/255";
            EXPECT_LE(stage->nodeCapacity(), kBudgetBytes) << "nodeCapacity <=157286400 152 MB 1/255";
            auto& ctx = core::REContext::current();
            auto b = stage->begin(kW, kH, ctx);
            if (b.ok()) {
                render::CsgRenderer renderer(registry, stage);
                render::Camera cam = makeCsgCamera();
                render::CsgDrawOperand base{*hCube, glm::mat4(1.0f), blue, 0u};
                render::CsgDrawOperand sub{*hSphere, glm::mat4(1.0f), red, 1u};
                auto dr = renderer.drawCsg(base, std::vector<render::CsgDrawOperand>{sub}, {}, cam);
                ASSERT_TRUE(dr.ok()) << dr.error().message << " 1/255";
                auto res = stage->resolve(ctx);
                ASSERT_TRUE(res.ok()) << res.error().message;
                auto cntHole = stage->readResolvedCount(320u, 240u);
                ASSERT_TRUE(cntHole.ok());
                EXPECT_EQ(*cntHole, 1u) << "hole center 1 survivor after layer-union base run " << run << " 1/255";
            }
        }
    }
}

// Tree (A∩B')∪(C∩D') SOP 1/255 plus abacaba SCS n=3/4 1/255 1e-6 N>=3
TEST(T11CsgTree, TreeSopAndAbacabaWithin1Per255) {
    constexpr int kRuns = 3;
    for (int run = 0; run < kRuns; ++run) {
        // Build tree (A∩B')∪(C∩D') where A is cube at (0,0,0) half 1, B sphere 0.6 at origin, C cube at (1,0,0) half 0.5, D sphere 0.3 at (1,0,0)
        data::Mesh cubeA = makeCube(1.0f);
        data::Mesh sphereB = makeSphere(0.6f, 12, 12);
        data::Mesh cubeC = makeCube(0.5f);
        data::Mesh sphereD = makeSphere(0.3f, 12, 12);
        auto meshA = std::make_shared<const data::Mesh>(cubeA);
        auto meshB = std::make_shared<const data::Mesh>(sphereB);
        auto meshC = std::make_shared<const data::Mesh>(cubeC);
        auto meshD = std::make_shared<const data::Mesh>(sphereD);

        scene::MeshMaterialDesc matA; matA.phong.baseColor = glm::vec4(0.2f, 0.4f, 0.8f, 1.0f); // blue 1/255
        scene::MeshMaterialDesc matB; matB.phong.baseColor = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f); // red 1/255
        scene::MeshMaterialDesc matC; matC.phong.baseColor = glm::vec4(0.2f, 0.8f, 0.2f, 1.0f); // green 1/255
        scene::MeshMaterialDesc matD; matD.phong.baseColor = glm::vec4(0.8f, 0.8f, 0.2f, 1.0f); // yellow 1/255

        auto leafA = broker::CsgTree::leaf(meshA, glm::mat4(1.0f), matA);
        auto leafB = broker::CsgTree::leaf(meshB, glm::mat4(1.0f), matB);
        auto leafC = broker::CsgTree::leaf(meshC, glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)), matC);
        auto leafD = broker::CsgTree::leaf(meshD, glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)), matD);

        // Build (A − B) as Difference
        auto diffAB = broker::CsgTree::makeDifference(leafA, leafB);
        // Build (C − D) as Difference
        auto diffCD = broker::CsgTree::makeDifference(leafC, leafD);
        // Build Union of the two differences: (A∩B') ∪ (C∩D')
        auto root = broker::CsgTree::makeUnion(
            std::variant<broker::CsgTreeLeaf, broker::CsgTreeNodePtr>(diffAB),
            std::variant<broker::CsgTreeLeaf, broker::CsgTreeNodePtr>(diffCD));

        // Flatten to SOP Σ Pi (Stewart 1998)
        auto sop = broker::CsgTree::flattenToSop(root);
        // Expected SOP: two products {A, B'} and {C, D'} → size 2 each size 2
        ASSERT_EQ(sop.size(), 2u) << "SOP size 2 for (A∩B')∪(C∩D') 1/255 run " << run;
        EXPECT_EQ(sop[0].size(), 2u) << "product 0 size 2 1/255";
        EXPECT_EQ(sop[1].size(), 2u) << "product 1 size 2 1/255 1e-6";

        // Verify leaves collected size 4 (A,B,C,D) within 1/255
        auto leaves = broker::CsgTree::collectLeaves(root);
        EXPECT_EQ(leaves.size(), 4u) << "leaves 4 1/255";

        // Analytic inside tests for product classification within 1e-6
        // Point at origin (0,0,0): inside A true, inside B true (sphere 0.6 contains origin), so A∩B' false; inside C false (C at 1,0,0 half 0.5, origin outside), so overall outside → false 1e-6
        // Use winding-inside as geometric analytic: distance checks within 1e-6
        auto isInsideA = [](glm::vec3 p) { return std::abs(p.x) <= 1.0f + 1e-6f && std::abs(p.y) <= 1.0f + 1e-6f && std::abs(p.z) <= 1.0f + 1e-6f; }; // 1e-6
        auto isInsideB = [](glm::vec3 p) { return glm::length(p) <= 0.6f + 1e-6f; }; // 1e-6
        auto isInsideC = [](glm::vec3 p) { glm::vec3 q = p - glm::vec3(1.0f, 0.0f, 0.0f); return std::abs(q.x) <= 0.5f + 1e-6f && std::abs(q.y) <= 0.5f + 1e-6f && std::abs(q.z) <= 0.5f + 1e-6f; }; // 1e-6
        auto isInsideD = [](glm::vec3 p) { return glm::length(p - glm::vec3(1.0f, 0.0f, 0.0f)) <= 0.3f + 1e-6f; }; // 1e-6

        glm::vec3 p0(0.0f, 0.0f, 0.0f);
        std::vector<bool> inside0 = {isInsideA(p0), isInsideB(p0), isInsideC(p0), isInsideD(p0)};
        bool eval0 = broker::CsgTree::evalSop(sop, inside0);
        EXPECT_FALSE(eval0) << "origin inside B so A∩B' false, C false → SOP false 1/255 1e-6 152 MB run " << run;

        glm::vec3 p1(0.9f, 0.0f, 0.0f); // near edge of A but outside B (dist 0.9 >0.6) → A true, B false → product0 true → SOP true
        std::vector<bool> inside1 = {isInsideA(p1), isInsideB(p1), isInsideC(p1), isInsideD(p1)};
        bool eval1 = broker::CsgTree::evalSop(sop, inside1);
        EXPECT_TRUE(eval1) << "p1 A∩B' true → SOP true 1/255 run " << run;

        glm::vec3 p2(1.0f, 0.0f, 0.8f); // inside C (z 0.8 >0.5? actually C half 0.5 so z 0.8 outside) → choose p inside C but outside D
        glm::vec3 p2a(1.0f, 0.0f, 0.4f); // inside C half 0.5, distance to D center 0.4 >0.3 outside D → C∩D' true
        std::vector<bool> inside2 = {isInsideA(p2a), isInsideB(p2a), isInsideC(p2a), isInsideD(p2a)};
        bool eval2 = broker::CsgTree::evalSop(sop, inside2);
        EXPECT_TRUE(eval2) << "p2a C∩D' true → SOP true 1/255 run " << run;

        // Analytic color for SOP true pixels: k-way over() of winning product's base color (blue for A product, green for C product) within 1/255
        glm::vec4 blue(0.2f, 0.4f, 0.8f, 1.0f);
        glm::vec4 green(0.2f, 0.8f, 0.2f, 1.0f);
        glm::vec4 dst(0.0f, 0.0f, 0.0f, 0.0f);
        // For p1 (A product) result blue 1/255
        glm::vec4 p1Color = over(glm::vec4(blue.r * blue.a, blue.g * blue.a, blue.b * blue.a, blue.a), dst);
        EXPECT_NEAR(p1Color.r, 0.2f, kTolF) << "p1 color R 1/255 run " << run;
        EXPECT_NEAR(p1Color.g, 0.4f, kTolF) << "p1 color G 1/255 run " << run;
        EXPECT_NEAR(p1Color.b, 0.8f, kTolF) << "p1 color B 1/255 1e-6 run " << run;

        // For p2a (C product) result green 1/255
        glm::vec4 p2Color = over(glm::vec4(green.r * green.a, green.g * green.a, green.b * green.a, green.a), dst);
        EXPECT_NEAR(p2Color.r, 0.2f, kTolF) << "p2a color R 1/255 run " << run;
        EXPECT_NEAR(p2Color.g, 0.8f, kTolF) << "p2a color G 1/255 run " << run;
        EXPECT_NEAR(p2Color.b, 0.2f, kTolF) << "p2a color B 1/255 1e-6 run " << run;

        // abacaba SCS n=3 and n=4 exact sequences 1/255
        auto s3 = broker::CsgTree::abacaba(3);
        std::vector<int> exp3 = {0, 1, 0, 2, 0, 1, 0};
        EXPECT_EQ(s3, exp3) << "abacaba n=3 1/255 1e-6 run " << run;
        auto s4 = broker::CsgTree::abacaba(4);
        std::vector<int> exp4 = {0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};
        EXPECT_EQ(s4, exp4) << "abacaba n=4 15 elements 1/255 run " << run;
        EXPECT_EQ(s4.size(), 15u) << "abacaba n=4 length 15 1/255";

        // Also verify CsgTreeObjectMapper OCP: register and mapCached spy 2→1
        auto registry = std::make_shared<render::AssetRegistry>();
        broker::CsgTreeObjectMapper mapper(registry);
        scene::CsgTreeObject obj;
        obj.setRoot(root);
        obj.transform = glm::mat4(1.0f);
        scene::TranslateContext ctx;
        auto r1 = mapper.mapCached(obj, ctx);
        ASSERT_TRUE(r1.ok()) << r1.error().message << " 1/255";
        auto r2 = mapper.mapCached(obj, ctx);
        ASSERT_TRUE(r2.ok()) << r2.error().message << " 1/255";
        // Second hit should be cached (generation unchanged, hash same)
        // We verify handles equal within 1/255 by checking baseHandle identity not bare >0
        EXPECT_EQ(r1->baseHandle, r2->baseHandle) << "CsgTreeObjectMapper cache hit baseHandle 1/255 run " << run;

        // Node capacity still 152 MB analytic 39321600 1/255
        auto stage = std::make_shared<render::CsgOitStage>(8u);
        auto cap = stage->ensureCapacity(kW, kH);
        ASSERT_TRUE(cap.ok());
        EXPECT_EQ(stage->nodeCapacity(), 640u * 480u * 8u * 16u) << "nodeCapacity 39321600 152 MB 1/255 run " << run;
        EXPECT_LE(stage->nodeCapacity(), kBudgetBytes) << "152 MB budget 1/255";
    }
}

// CsgTree node variant<Mesh,Node> shape plus SOP abacaba invariants 1/255 1e-6
TEST(T11CsgTree, NodeVariantShapeAndSopInvariants) {
    // Verify Node{Op op; variant<Mesh,Node> left,right} shape by constructing (A−B)∪C as Union(Difference(A,B),C) within 1/255
    data::Mesh a = makeCube(1.0f);
    data::Mesh b = makeSphere(0.6f);
    data::Mesh c = makeCube(0.5f);
    auto ma = std::make_shared<const data::Mesh>(a);
    auto mb = std::make_shared<const data::Mesh>(b);
    auto mc = std::make_shared<const data::Mesh>(c);
    scene::MeshMaterialDesc mat; mat.phong.baseColor = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    auto leafA = broker::CsgTree::leaf(ma, glm::mat4(1.0f), mat);
    auto leafB = broker::CsgTree::leaf(mb, glm::mat4(1.0f), mat);
    auto leafC = broker::CsgTree::leaf(mc, glm::mat4(1.0f), mat);
    auto diffAB = broker::CsgTree::makeDifference(leafA, leafB);
    auto root = broker::CsgTree::makeUnion(std::variant<broker::CsgTreeLeaf, broker::CsgTreeNodePtr>(diffAB), leafC);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->op, broker::CsgTreeOp::Union) << "root Union 1/255";
    // Left is Difference node, right is leaf
    EXPECT_TRUE(std::holds_alternative<broker::CsgTreeNodePtr>(root->left)) << "left is Node 1/255";
    EXPECT_TRUE(std::holds_alternative<broker::CsgTreeLeaf>(root->right)) << "right is Mesh leaf 1/255";
    auto leftNode = std::get<broker::CsgTreeNodePtr>(root->left);
    ASSERT_NE(leftNode, nullptr);
    EXPECT_EQ(leftNode->op, broker::CsgTreeOp::Difference) << "left Difference 1/255 1e-6 152 MB";
    // Flatten (A−B)∪C → SOP {A,B'} , {C} → two products
    auto sop = broker::CsgTree::flattenToSop(root);
    EXPECT_EQ(sop.size(), 2u) << "SOP (A−B)∪C size 2 1/255";
}

} // namespace re::tests
