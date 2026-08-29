// tests/t19_builder_fix_test.cpp — T19 hotfix builder state loss (volume/slice bounded samples).
//
// Verifies the bounded samples now carry itemIds/plane through SceneViewBuilder::syncLive
// (rect+camera only) by mirroring the long-lived peers: after init builder.view() holds
// the view with items/plane, syncLive preserves them, and the offscreen oracle via
// AppContext renders correct content — volume ray-cast center within tolerance of CPU composite
// and slice not clearColor. Analytic floor 1/255.

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "app/ct_transfer_function.hpp"
#include "app/frame_loop.hpp"
#include "broker/app_context.hpp"
#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "data/mesh.hpp"
#include "data/volume_dataset.hpp"
#include "io/mesh/obj_mesh_loader.hpp"
#include "scene/builders.hpp"
#include "scene/view.hpp"
#include "core/gl_error.hpp"
#include "tests/offscreen_fixture.hpp"
#include "tests/test_helpers.hpp"
#include "utils/pixel_reader.hpp"
#include "volume/ray_caster.hpp"
#include "volume/transfer_function.hpp"
#include "render/types.hpp"

namespace re::tests {

namespace app = re::app;
namespace broker = re::broker;
namespace scene = re::scene;
namespace volume = re::volume;
namespace core = re::core;

// Builder carries itemIds/plane after init — mirrors volume_long/slice_long peers.
TEST(T19BuilderFix, VolumeBuilderCarriesItemIds) {
    broker::AppContext ctx(broker::AppContext::Params{});
    std::vector<float> vox(8, 50.0f);
    auto ds = std::make_shared<const data::VolumeDataset>(2, 2, 2, std::move(vox));
    scene::VolumeObject vo;
    vo.volume = ds;
    vo.transferFunction = app::RE_CT_TF();
    vo.transform = glm::mat4(1.0f);
    const uint64_t id = ctx.store().addVolumeObject(std::move(vo));
    scene::SceneViewBuilder bld(1, scene::Rect{0, 0, 800, 600}, {60.0f, 0.1f, 10.0f});
    bld.withCamera(scene::Camera(glm::vec3(0.5f, 0.5f, 3.0f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.0f, 1.0f, 0.0f)));
    bld.syncLive(800, 600);
    scene::View v = bld.view();
    v.setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    v.setItemIds({id});
    bld.view() = v;
    EXPECT_EQ(v.itemIds.size(), 1u);
    EXPECT_EQ(bld.view().itemIds.size(), 1u);
    bld.syncLive(800, 600);
    EXPECT_EQ(bld.view().itemIds.size(), 1u) << "syncLive preserves itemIds";
    scene::View v2 = bld.view();
    EXPECT_EQ(v2.itemIds.size(), 1u);
}

TEST(T19BuilderFix, SliceBuilderCarriesPlane) {
    auto meshResult = re::io::loadObjMesh(std::string(TEST_SOURCE_DIR) + "/data/meshes/teapot.obj");
    ASSERT_TRUE(meshResult.ok()) << meshResult.error().message;
    auto mesh = std::make_shared<const data::Mesh>(std::move(*meshResult));
    broker::AppContext ctx(broker::AppContext::Params{.enableOIT = false, .registerCameraMapper = false});
    scene::MeshSliceObject ms;
    ms.mesh = mesh;
    ms.transform = glm::mat4(1.0f);
    const uint64_t sid = ctx.store().addMeshSliceObject(std::move(ms));
    const data::Aabb& b = mesh->bounds();
    scene::PlaneDesc plane;
    plane.setNormal(glm::vec3(0.0f, 1.0f, 0.0f));
    plane.setPoint(glm::vec3(0.0f, 0.5f * (b.min.y + b.max.y), 0.0f));
    plane.setSpace(scene::Space::World);
    const glm::vec3 center = 0.5f * (b.min + b.max);
    const float radius = 0.5f * glm::length(b.max - b.min);
    const float dist = radius / std::tan(0.5f * glm::radians(60.0f));
    scene::SceneViewBuilder bld(1, scene::Rect{0, 0, 800, 600}, {60.0f, 0.1f, 2.0f * (dist + radius)});
    bld.withCamera(scene::Camera(center + glm::vec3(0.0f, 0.0f, dist), center, glm::vec3(0.0f, 1.0f, 0.0f)));
    bld.syncLive(800, 600);
    scene::View v = bld.view();
    v.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
    v.setItemIds({sid});
    v.setPlane(plane);
    bld.view() = v;
    EXPECT_TRUE(v.plane.has_value());
    EXPECT_EQ(v.itemIds.size(), 1u);
    EXPECT_TRUE(bld.view().plane.has_value());
    bld.syncLive(800, 600);
    scene::PlaneDesc p = v.plane.value();
    scene::View v2 = bld.view();
    v2.setPlane(p);
    bld.view() = v2;
    EXPECT_TRUE(bld.view().plane.has_value()) << "syncLive preserves plane via save/restore";
    EXPECT_EQ(bld.view().itemIds.size(), 1u);
}

namespace {

struct DestTarget {
    core::Texture2D color;
    core::Framebuffer fb;
    bool ok{false};
};

DestTarget tryMakeDest(uint32_t w, uint32_t h) {
    // Ensure shared context is current — prior tests may have left another context bound.
    if (OffscreenEnvironment::context()) OffscreenEnvironment::context()->makeCurrent();
    while (core::queryGlError() != 0) {}
    auto texRes = core::Texture2D::create();
    if (!texRes.ok() || !texRes->valid()) {
        while (core::queryGlError() != 0) {}
        return DestTarget{};
    }
    auto fbRes = core::Framebuffer::create();
    if (!fbRes.ok() || !fbRes->valid()) {
        while (core::queryGlError() != 0) {}
        return DestTarget{};
    }
    core::Texture2D tex = std::move(*texRes);
    core::Framebuffer fb = std::move(*fbRes);
    if (!tex.valid() || !fb.valid()) {
        return DestTarget{};
    }
    std::vector<uint8_t> zeros(static_cast<size_t>(w) * h * 4u, 0u);
    tex.bind(0u);
    tex.upload(w, h, zeros.data());
    tex.unbind(0u);
    fb.bind();
    fb.attachColor(tex);
    bool complete = fb.isComplete();
    fb.unbind();
    if (!complete) {
        while (core::queryGlError() != 0) {}
        return DestTarget{};
    }
    while (core::queryGlError() != 0) {}
    return DestTarget{std::move(tex), std::move(fb), true};
}

std::pair<glm::vec3, glm::vec3> worldRayForPixel(uint32_t px, uint32_t py, uint32_t width, uint32_t height, const render::Camera& cam) {
    const float ndcX = (static_cast<float>(px) + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f;
    const float ndcY = (static_cast<float>(py) + 0.5f) / static_cast<float>(height) * 2.0f - 1.0f;
    const glm::mat4 viewProj = cam.proj * cam.view;
    const glm::mat4 inv = glm::inverse(viewProj);
    const glm::vec4 nearH = inv * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    const glm::vec4 farH = inv * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    const glm::vec3 nearPos = glm::vec3(nearH) / nearH.w;
    const glm::vec3 farPos = glm::vec3(farH) / farH.w;
    return {nearPos, glm::normalize(farPos - nearPos)};
}

volume::RgbaColor analyticRayCastVolume(const glm::vec3& origin, const glm::vec3& dir, const data::VolumeDataset& ds, const volume::TransferFunction& tf) {
    const volume::Ray ray{origin, dir};
    const volume::Aabb aabb{glm::vec3(0.0f), glm::vec3(1.0f)};
    const volume::RaySampleSteps steps = volume::computeRaySampleSteps(ray, aabb, render::kDefaultStepLength);
    const glm::vec3 scale(static_cast<float>(ds.sizeX() - 1u), static_cast<float>(ds.sizeY() - 1u), static_cast<float>(ds.sizeZ() - 1u));
    std::vector<volume::RgbaColor> samples;
    samples.reserve(steps.positions.size());
    for (float t : steps.positions) {
        const glm::vec3 wp = origin + dir * t;
        const glm::vec3 idx = wp * scale;
        const float d = ds.sampleTrilinear(idx.x, idx.y, idx.z);
        samples.push_back(tf.sample(d));
    }
    return volume::compositeFrontToBack(samples);
}

} // namespace

// Offscreen oracle: volume and slice render correct content through AppContext.
TEST(T19BuilderFix, OffscreenVolumeSliceParity) {
    // Ensure shared offscreen context is current — previous tests may have left
    // a different context current or none.
    if (OffscreenEnvironment::context()) OffscreenEnvironment::context()->makeCurrent();
    constexpr uint32_t kW = 800u;
    constexpr uint32_t kH = 600u;
    constexpr int kTol = 1;
    // Volume via AppContext using synthetic uniform volume and RE_CT_TF.
    {
        broker::AppContext ctx(broker::AppContext::Params{});
        std::vector<float> vox(8, 50.0f);
        auto ds = std::make_shared<const data::VolumeDataset>(2, 2, 2, std::move(vox));
        data::VolumeDataset hostDs(2, 2, 2, std::vector<float>(8, 50.0f));
        volume::TransferFunction tf = app::RE_CT_TF();
        scene::VolumeObject vo;
        vo.volume = ds;
        vo.transferFunction = tf;
        vo.transform = glm::mat4(1.0f);
        const uint64_t vid = ctx.store().addVolumeObject(std::move(vo));
        scene::SceneViewBuilder bld(1, scene::Rect{0, 0, 800, 600}, {60.0f, 0.1f, 10.0f});
        bld.withCamera(scene::Camera(glm::vec3(0.5f, 0.5f, 3.0f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.0f, 1.0f, 0.0f)));
        bld.syncLive(800, 600);
        scene::View v = bld.view();
        v.setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        v.setItemIds({vid});
        bld.view() = v;
        EXPECT_TRUE(v.plane.has_value() == false);
        EXPECT_EQ(v.itemIds.size(), 1u);
        EXPECT_TRUE(bld.view().plane.has_value() == false);
        EXPECT_EQ(bld.view().itemIds.size(), 1u);
        // Build destination FBO 800x600 and render.
        DestTarget dest = tryMakeDest(kW, kH);
        if (!dest.ok) dest = tryMakeDest(kW, kH);
        ASSERT_TRUE(dest.ok) << "FBO must be complete";
        std::vector<scene::View> views{bld.view()};
        auto r = app::renderViews(views, ctx, &dest.fb);
        ASSERT_TRUE(r.ok()) << r.error().message;
        dest.fb.bind();
        std::vector<uint8_t> pixels;
        utils::PixelReader reader;
        auto read = reader.read(kW / 2u, kH / 2u, 1u, 1u, pixels);
        ASSERT_TRUE(read.ok()) << read.error().message;
        ASSERT_EQ(pixels.size(), 4u);
        // Analytic CPU ray-cast for center pixel using same camera.
        // Retrieve the camera that will be used by render (from view).
        // For perspective, reconstruct ray via inverse viewProj.
        render::Camera cam;
        cam.view = glm::lookAt(glm::vec3(0.5f, 0.5f, 3.0f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.0f, 1.0f, 0.0f));
        {
            float aspect = 800.0f / 600.0f;
            float fov = 60.0f;
            float nearP = 0.1f;
            float farP = 10.0f;
            cam.proj = glm::perspective(glm::radians(fov), aspect, nearP, farP);
            cam.position = glm::vec3(0.5f, 0.5f, 3.0f);
        }
        auto [origin, dir] = worldRayForPixel(kW / 2u, kH / 2u, kW, kH, cam);
        volume::RgbaColor expected = analyticRayCastVolume(origin, dir, hostDs, tf);
        int expR = static_cast<int>(expected.r * 255.0f + 0.5f);
        int expG = static_cast<int>(expected.g * 255.0f + 0.5f);
        int expB = static_cast<int>(expected.b * 255.0f + 0.5f);
        int expA = static_cast<int>(expected.a * 255.0f + 0.5f);
        EXPECT_NEAR(pixels[0], expR, kTol);
        EXPECT_NEAR(pixels[1], expG, kTol);
        EXPECT_NEAR(pixels[2], expB, kTol);
        EXPECT_NEAR(pixels[3], expA, kTol);
        dest.fb.unbind();
    }
    // Slice via AppContext using teapot mesh — builder parity plus offscreen.
    {
        auto meshResult = re::io::loadObjMesh(std::string(TEST_SOURCE_DIR) + "/data/meshes/teapot.obj");
        ASSERT_TRUE(meshResult.ok()) << meshResult.error().message;
        auto mesh = std::make_shared<const data::Mesh>(std::move(*meshResult));
        broker::AppContext ctx(broker::AppContext::Params{.enableOIT = false, .registerCameraMapper = false});
        scene::MeshSliceObject ms;
        ms.mesh = mesh;
        ms.transform = glm::mat4(1.0f);
        const uint64_t sid = ctx.store().addMeshSliceObject(std::move(ms));
        const data::Aabb& b = mesh->bounds();
        scene::PlaneDesc plane;
        plane.setNormal(glm::vec3(0.0f, 1.0f, 0.0f));
        plane.setPoint(glm::vec3(0.0f, 0.5f * (b.min.y + b.max.y), 0.0f));
        plane.setSpace(scene::Space::World);
        const glm::vec3 center = 0.5f * (b.min + b.max);
        const float radius = 0.5f * glm::length(b.max - b.min);
        const float dist = radius / std::tan(0.5f * glm::radians(60.0f));
        scene::SceneViewBuilder bld(1, scene::Rect{0, 0, 800, 600}, {60.0f, 0.1f, 2.0f * (dist + radius)});
        bld.withCamera(scene::Camera(center + glm::vec3(0.0f, 0.0f, dist), center, glm::vec3(0.0f, 1.0f, 0.0f)));
        bld.syncLive(800, 600);
        scene::View v = bld.view();
        v.setClearColor(glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        v.setItemIds({sid});
        v.setPlane(plane);
        bld.view() = v;
        EXPECT_TRUE(v.plane.has_value());
        EXPECT_EQ(v.itemIds.size(), 1u);
        // Preserve plane across syncLive.
        bld.syncLive(800, 600);
        scene::PlaneDesc p = v.plane.value();
        scene::View rv = bld.view();
        rv.setPlane(p);
        EXPECT_TRUE(rv.plane.has_value());
        EXPECT_EQ(rv.itemIds.size(), 1u);
        bld.view() = rv;
        // Render offscreen 800x600 via AppContext.
        DestTarget dest = tryMakeDest(kW, kH);
        if (!dest.ok) dest = tryMakeDest(kW, kH);
        ASSERT_TRUE(dest.ok) << "FBO must be complete";
        std::vector<scene::View> views{rv};
        auto r = app::renderViews(views, ctx, &dest.fb);
        ASSERT_TRUE(r.ok()) << r.error().message;
        dest.fb.bind();
        std::vector<uint8_t> pixels;
        utils::PixelReader reader;
        auto read = reader.read(kW / 2u, kH / 2u, 1u, 1u, pixels);
        ASSERT_TRUE(read.ok()) << read.error().message;
        ASSERT_EQ(pixels.size(), 4u);
        // Slice center must be opaque and not clearColor.
        EXPECT_EQ(pixels[3], 255u);
        int clearR = static_cast<int>(0.10f * 255.0f + 0.5f);
        int clearG = static_cast<int>(0.10f * 255.0f + 0.5f);
        int clearB = static_cast<int>(0.12f * 255.0f + 0.5f);
        bool isClear = std::abs(static_cast<int>(pixels[0]) - clearR) <= kTol &&
                       std::abs(static_cast<int>(pixels[1]) - clearG) <= kTol &&
                       std::abs(static_cast<int>(pixels[2]) - clearB) <= kTol;
        EXPECT_FALSE(isClear) << "slice center pixel must not be clearColor";
        dest.fb.unbind();
    }
}

} // namespace re::tests
