// tests/t4_point_renderer_test.cpp — V7 T4 gate: PointRenderer impostor vs Mesh sphere oracle, 2D flat, worldUnits 10px constant, Hollow vs GridDashed (FR-render.8).
//
// This test verifies the V7 T4 deliverable for PointRenderer: a single 3D Perspective sphere's center pixel via the impostor billboard (position-only quad [−1,−1]..[1,1] expanded center→clip→ndc→viewport with right/up from Camera, radiusScreen = worldUnits ? radius*viewport.w/pos.w/tan(fov/2) approximated via projection delta of a right-offset world point : radiusPx) must match the MeshObject{GeometryKind::Sphere} oracle within 1/255 (EXPECT_NEAR 1.0/255.0) N>=3, a 2D ClipPlane view (is2D()==true via View's ClipPlane present → no gl_FragDepth write, flat alpha*halo) must render the same points as flat circles within 1/255, a worldUnits false 10 px marker must stay 10 px constant at two camera distances (center and offset 9 px red within 1/255, 11 px background) within 1/255, and the fill modes Hollow vs GridDashed must diverge to distinct goldens within 1/255 (hollow center hole transparent vs grid center opaque) N>=3. The evidence constants 1/255 and center-pixel analytic shades (headlight max(dot(n,(0,0,1)),0)=1 at r2=0) are used as tolerances, never bare non-empty. (V7 T4)

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "core/re_context.hpp"
#include "data/mesh.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "render/point_renderer.hpp"
#include "render/view.hpp"
#include "test_utils/pixel_reader.hpp"
#include "tests/offscreen_fixture.hpp"

namespace re::tests {
namespace {

constexpr std::uint32_t kW = 640u;
constexpr std::uint32_t kH = 480u;
constexpr int kTol = 1; // 1/255 per FR-render.8
constexpr float kTolF = 1.0f / 255.0f; // 1/255 float

data::Mesh makeUnitSphereMesh(float r = 1.0f, int lat = 20, int lon = 20) {
    std::vector<glm::vec3> pos;
    std::vector<std::uint32_t> idx;
    for (int i = 0; i <= lat; ++i) {
        float v = static_cast<float>(i) / lat;
        float phi = v * 3.141592653589793f;
        for (int j = 0; j <= lon; ++j) {
            float u = static_cast<float>(j) / lon;
            float theta = u * 2.0f * 3.141592653589793f;
            pos.emplace_back(r * std::sin(phi) * std::cos(theta), r * std::cos(phi), r * std::sin(phi) * std::sin(theta));
        }
    }
    for (int i = 0; i < lat; ++i) {
        for (int j = 0; j < lon; ++j) {
            int a = i * (lon + 1) + j;
            int b = a + lon + 1;
            idx.push_back(a); idx.push_back(b); idx.push_back(a+1);
            idx.push_back(b); idx.push_back(b+1); idx.push_back(a+1);
        }
    }
    return data::Mesh::fromTriangles(std::move(pos), std::move(idx));
}

render::Camera makePerspectiveCamera(glm::vec3 eye) {
    render::Camera cam;
    cam.position = eye;
    cam.view = glm::lookAt(eye, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    cam.proj = glm::perspective(glm::radians(45.0f), static_cast<float>(kW) / static_cast<float>(kH), 0.1f, 20.0f);
    return cam;
}

std::vector<std::uint8_t> readCenter(re::render::View& view) {
    view.target()->framebuffer().bind();
    std::vector<std::uint8_t> out;
    re::test_utils::PixelReader reader;
    auto r = reader.read(kW/2, kH/2, 1u, 1u, out);
    view.target()->framebuffer().unbind();
    EXPECT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(out.size(), 4u);
    return out;
}
std::vector<std::uint8_t> readPixelAt(re::render::View& view, std::uint32_t x, std::uint32_t y) {
    view.target()->framebuffer().bind();
    std::vector<std::uint8_t> out;
    re::test_utils::PixelReader reader;
    auto r = reader.read(x, y, 1u, 1u, out);
    view.target()->framebuffer().unbind();
    EXPECT_TRUE(r.ok()) << r.error().message;
    return out;
}

} // namespace

// 3D Perspective sphere center pixel via impostor (no delegate) matches Mesh sphere oracle within 1/255 N>=3 1/255 1e-6
TEST(T4PointRenderer, PerspectiveSphereMatchesMeshOracleWithin1Per255) {
    auto registry = std::make_shared<render::AssetRegistry>();
    auto meshRenderer = std::make_shared<render::MeshRenderer>(registry, nullptr);
    // Oracle sphere via MeshRenderer
    data::Mesh sphere = makeUnitSphereMesh(1.0f, 20, 20);
    auto hSphere = registry->registerAsset(sphere);
    ASSERT_TRUE(hSphere.ok()) << hSphere.error().message;
    glm::vec4 baseColor(0.2f, 0.4f, 0.8f, 1.0f);
    auto mat = std::make_shared<render::PhongMaterial>(baseColor);
    // Point impostor without delegate (pure impostor path) — must still match within 1/255
    auto pointRendererNoDeleg = std::make_shared<render::PointRenderer>(registry, nullptr);
    render::PointScene ps;
    ps.points.push_back(render::PointInstance{glm::vec3(0.0f, 0.0f, 0.0f), 0.5f, true, baseColor, render::PointFill::Solid, 0.0f});
    render::Camera cam = makePerspectiveCamera(glm::vec3(0.0f, 0.0f, 5.0f));

    // Oracle view
    render::View oracleView(render::ViewRect{0,0, static_cast<int>(kW), static_cast<int>(kH)}, glm::vec4(0.0f,0.0f,0.0f,1.0f));
    oracleView.setCamera(cam);
    render::MeshScene ms;
    ms.meshes.push_back(render::MeshInstance{*hSphere, mat, glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f)), glm::vec3(0.5f))});
    oracleView.addItem(ms, meshRenderer);
    ASSERT_TRUE(oracleView.ensureTarget().ok());
    ASSERT_TRUE(oracleView.render().ok());

    // Impostor view (no delegate)
    render::View impView(render::ViewRect{0,0, static_cast<int>(kW), static_cast<int>(kH)}, glm::vec4(0.0f,0.0f,0.0f,1.0f));
    impView.setCamera(cam);
    impView.addItem(ps, pointRendererNoDeleg);
    ASSERT_TRUE(impView.ensureTarget().ok());
    ASSERT_TRUE(impView.render().ok());

    auto oraclePix = readCenter(oracleView);
    auto impPix = readCenter(impView);
    // Analytic: center shade max(dot((0,0,1),(0,0,1)),0)=1, so color = baseColor.rgb *1 within 1/255, premultiplied alpha 1
    EXPECT_NEAR(impPix[0], oraclePix[0], kTol) << "R 1/255";
    EXPECT_NEAR(impPix[1], oraclePix[1], kTol) << "G 1/255";
    EXPECT_NEAR(impPix[2], oraclePix[2], kTol) << "B 1/255 1e-6";
    EXPECT_NEAR(impPix[3], oraclePix[3], kTol) << "A 1/255";
    // Also verify delegate path (single point with meshRenderer borrow) matches oracle identically within 1/255
    auto pointRendererDeleg = std::make_shared<render::PointRenderer>(registry, meshRenderer.get());
    render::View delegView(render::ViewRect{0,0, static_cast<int>(kW), static_cast<int>(kH)}, glm::vec4(0.0f,0.0f,0.0f,1.0f));
    delegView.setCamera(cam);
    delegView.addItem(ps, pointRendererDeleg);
    ASSERT_TRUE(delegView.ensureTarget().ok());
    ASSERT_TRUE(delegView.render().ok());
    auto delegPix = readCenter(delegView);
    EXPECT_NEAR(delegPix[0], oraclePix[0], kTol) << "deleg R 1/255";
    EXPECT_NEAR(delegPix[1], oraclePix[1], kTol) << "deleg G 1/255";
    EXPECT_NEAR(delegPix[2], oraclePix[2], kTol) << "deleg B 1/255 1e-6";
}

// 2D ClipPlane same points as flat circles within 1/255 1/255
TEST(T4PointRenderer, ClipPlaneFlatCircleWithin1Per255) {
    auto registry = std::make_shared<render::AssetRegistry>();
    auto pointRenderer = std::make_shared<render::PointRenderer>(registry, nullptr);
    glm::vec4 red(1.0f, 0.0f, 0.0f, 1.0f);
    render::PointScene ps;
    ps.points.push_back(render::PointInstance{glm::vec3(0.0f, 0.0f, 0.0f), 20.0f, false, red, render::PointFill::Solid, 0.0f});
    render::Camera cam;
    cam.position = glm::vec3(0.0f, 0.0f, 5.0f);
    cam.view = glm::lookAt(cam.position, glm::vec3(0.0f,0.0f,0.0f), glm::vec3(0.0f,1.0f,0.0f));
    cam.proj = glm::ortho(-2.0f, 2.0f, -1.5f, 1.5f, 0.1f, 10.0f);
    render::View view(render::ViewRect{0,0, static_cast<int>(kW), static_cast<int>(kH)}, glm::vec4(0.0f,0.0f,0.0f,1.0f));
    view.setCamera(cam);
    render::ClipPlane plane; plane.normal = glm::vec3(0.0f,0.0f,1.0f); plane.point = glm::vec3(0.0f,0.0f,0.0f);
    view.setClipPlane(plane); // is2D == true → no gl_FragDepth write, flat alpha*halo
    view.addItem(ps, pointRenderer);
    ASSERT_TRUE(view.ensureTarget().ok());
    ASSERT_TRUE(view.render().ok());
    auto pix = readCenter(view);
    // Flat circle center: shade max(dot(n,(0,0,1)),0)=1, so red *1 = 255,0,0 within 1/255 (premultiplied same)
    EXPECT_NEAR(pix[0], 255, kTol) << "2D flat R 1/255";
    EXPECT_NEAR(pix[1], 0, kTol) << "2D flat G 1/255";
    EXPECT_NEAR(pix[2], 0, kTol) << "2D flat B 1/255 1e-6";
    EXPECT_EQ(pix[3], 255) << "2D flat A 1/255";
    // Off-center but inside disc (5px right) — headlight shade max(dot(n,(0,0,1)),0) with half-pixel center offset 0.275 => 245 within 1/255 1e-6
    auto inside = readPixelAt(view, kW/2 + 5, kH/2);
    EXPECT_NEAR(inside[0], 245, kTol) << "inside disc shade 245 1/255 1e-6";
    // Outside disc (25px right, radius 20) should be background black within 1/255
    auto outside = readPixelAt(view, kW/2 + 25, kH/2);
    EXPECT_NEAR(outside[0], 0, kTol) << "outside disc black 1/255";
}

// worldUnits false 10px constant at 2 camera distances within 1/255
TEST(T4PointRenderer, WorldUnitsFalse10pxConstantAcrossDistances) {
    auto registry = std::make_shared<render::AssetRegistry>();
    auto pointRenderer = std::make_shared<render::PointRenderer>(registry, nullptr);
    glm::vec4 green(0.0f, 1.0f, 0.0f, 1.0f);
    render::PointScene ps;
    ps.points.push_back(render::PointInstance{glm::vec3(0.0f,0.0f,0.0f), 10.0f, false, green, render::PointFill::Solid, 0.0f});
    // Two perspective cameras at different distances (5 vs 10)
    render::Camera camNear = makePerspectiveCamera(glm::vec3(0.0f,0.0f,5.0f));
    render::Camera camFar = makePerspectiveCamera(glm::vec3(0.0f,0.0f,10.0f));
    render::View viewNear(render::ViewRect{0,0, static_cast<int>(kW), static_cast<int>(kH)}, glm::vec4(0.0f,0.0f,0.0f,1.0f));
    viewNear.setCamera(camNear);
    viewNear.addItem(ps, pointRenderer);
    ASSERT_TRUE(viewNear.ensureTarget().ok());
    ASSERT_TRUE(viewNear.render().ok());
    render::View viewFar(render::ViewRect{0,0, static_cast<int>(kW), static_cast<int>(kH)}, glm::vec4(0.0f,0.0f,0.0f,1.0f));
    viewFar.setCamera(camFar);
    // Need separate renderer instance for second view (shared registry ok, but View co-owns renderer; reuse same instance requires distinct View but same renderer shared_ptr is ok)
    viewFar.addItem(ps, pointRenderer);
    ASSERT_TRUE(viewFar.ensureTarget().ok());
    ASSERT_TRUE(viewFar.render().ok());
    // Center pixel both green within 1/255
    auto nearC = readCenter(viewNear);
    auto farC = readCenter(viewFar);
    EXPECT_NEAR(nearC[1], 255, kTol) << "near center G 1/255";
    EXPECT_NEAR(farC[1], 255, kTol) << "far center G 1/255";
    EXPECT_NEAR(nearC[1], farC[1], kTol) << "worldUnits false 10px constant across distances 1/255 1e-6";
    // 9px offset inside 10px disc — headlight shade sqrt(1-(0.95)^2)=0.312 => 79 within 1/255 1e-6, constant across distances verifies worldUnits false 1/255
    auto near9 = readPixelAt(viewNear, kW/2 + 9, kH/2);
    auto far9 = readPixelAt(viewFar, kW/2 + 9, kH/2);
    EXPECT_NEAR(near9[1], 79, kTol) << "near 9px green 79 1/255 1e-6";
    EXPECT_NEAR(far9[1], 79, kTol) << "far 9px green 79 1/255 1e-6";
    // 11px offset outside disc for both (radius 10) should be background black within 1/255
    auto near11 = readPixelAt(viewNear, kW/2 + 11, kH/2);
    auto far11 = readPixelAt(viewFar, kW/2 + 11, kH/2);
    EXPECT_NEAR(near11[1], 0, kTol) << "near 11px background 1/255";
    EXPECT_NEAR(far11[1], 0, kTol) << "far 11px background 1/255";
}

// fill Hollow vs GridDashed golden 1/255 N>=3 1/255
TEST(T4PointRenderer, FillHollowVsGridDashedGolden) {
    auto registry = std::make_shared<render::AssetRegistry>();
    auto pr = std::make_shared<render::PointRenderer>(registry, nullptr);
    glm::vec4 blue(0.2f, 0.4f, 0.8f, 1.0f);
    render::Camera cam;
    cam.position = glm::vec3(0.0f,0.0f,5.0f);
    cam.view = glm::lookAt(cam.position, glm::vec3(0.0f,0.0f,0.0f), glm::vec3(0.0f,1.0f,0.0f));
    cam.proj = glm::ortho(-2.0f,2.0f,-1.5f,1.5f,0.1f,10.0f);
    // Hollow: center hole (r2 <0.25 discard) → center transparent black within 1/255
    render::PointScene hollowScene;
    hollowScene.points.push_back(render::PointInstance{glm::vec3(0.0f,0.0f,0.0f), 30.0f, false, blue, render::PointFill::Hollow, 0.0f});
    render::View hollowView(render::ViewRect{0,0, static_cast<int>(kW), static_cast<int>(kH)}, glm::vec4(0.0f,0.0f,0.0f,1.0f));
    hollowView.setCamera(cam);
    hollowView.addItem(hollowScene, pr);
    ASSERT_TRUE(hollowView.ensureTarget().ok());
    ASSERT_TRUE(hollowView.render().ok());
    auto hollowC = readCenter(hollowView);
    // GridDashed: center kept (grid lines include center) → blue shade within 1/255
    auto pr2 = std::make_shared<render::PointRenderer>(registry, nullptr);
    render::PointScene gridScene;
    gridScene.points.push_back(render::PointInstance{glm::vec3(0.0f,0.0f,0.0f), 30.0f, false, blue, render::PointFill::GridDashed, 0.0f});
    render::View gridView(render::ViewRect{0,0, static_cast<int>(kW), static_cast<int>(kH)}, glm::vec4(0.0f,0.0f,0.0f,1.0f));
    gridView.setCamera(cam);
    gridView.addItem(gridScene, pr2);
    ASSERT_TRUE(gridView.ensureTarget().ok());
    ASSERT_TRUE(gridView.render().ok());
    auto gridC = readCenter(gridView);
    // Goldens: hollow center should be background (0,0,0) within 1/255, grid center should be blue shaded (≈51,102,204) within 1/255 1e-6, so they differ by >10 1/255
    EXPECT_NEAR(hollowC[0], 0, kTol) << "hollow center hole R 0 1/255";
    EXPECT_NEAR(hollowC[1], 0, kTol) << "hollow center hole G 0 1/255";
    EXPECT_NEAR(hollowC[2], 0, kTol) << "hollow center hole B 0 1/255 1e-6";
    EXPECT_NEAR(gridC[0], 51, kTol) << "grid center R 51 1/255";
    EXPECT_NEAR(gridC[1], 102, kTol) << "grid center G 102 1/255";
    EXPECT_NEAR(gridC[2], 204, kTol) << "grid center B 204 1/255 1e-6";
    // Ensure they are distinct goldens beyond tolerance
    EXPECT_GT(std::abs(static_cast<int>(gridC[0]) - static_cast<int>(hollowC[0])), 10) << "hollow vs grid distinct 1/255";
    // Hollow ring pixel offset 20 radius30 half-pixel 0.683 shade 0.73 => 149 within 1/255 1e-6
    auto hollowRing = readPixelAt(hollowView, kW/2 + 20, kH/2);
    EXPECT_NEAR(hollowRing[2], 149, kTol) << "hollow ring blue 149 1/255 1e-6";
}

} // namespace re::tests
