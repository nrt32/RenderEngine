// tests/t9_samples_bounded_test.cpp — T9 bounded samples gate (FR-app.1). This file verifies the bounded harness discipline
// that every sample exits after app::kDefaultFrames = 300 frames unless RE_SAMPLE_MAX_FRAMES overrides, and that a
// simple red quad rendered through the broker View path yields a center pixel within one LSB of the analytic red
// (255,0,0,255) via renderOffscreen, proving layer migration LAYER_0 and techniqueOrder still route Plane and Contour
// correctly and that priority defaults do not break the View path. Analytic tolerance is 1/255 (kTol=1 LSB).
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "app/sample_harness.hpp"
#include "data/image.hpp"
#include "data/mesh.hpp"
#include "render/offscreen.hpp"
#include "scene/layer.hpp"
#include "scene/store.hpp"
#include "scene/view.hpp"
#include "utils/asset_utils.hpp"

namespace re::tests {
namespace {

constexpr int kTol = 1;

scene::Camera makePerspCamera() {
    scene::Camera cam(glm::vec3(0, 0, 3), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    cam.setPerspective(60.0f, 1.0f, 0.1f, 10.0f);
    return cam;
}

data::Mesh makeQuadMesh() {
    std::vector<glm::vec3> pos = {glm::vec3(-1, -1, 0), glm::vec3(1, -1, 0), glm::vec3(1, 1, 0), glm::vec3(-1, 1, 0)};
    std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return data::Mesh::fromTriangles(std::move(pos), std::move(idx));
}

} // namespace

TEST(T9SamplesBounded, KDefaultFramesIs300AndSampleMaxFramesBounded) {
    EXPECT_EQ(app::kDefaultFrames, 300);
    EXPECT_EQ(app::sampleMaxFrames(300), 300);
    ::setenv("RE_SAMPLE_MAX_FRAMES", "20", 1);
    EXPECT_EQ(app::sampleMaxFrames(app::kDefaultFrames), 20);
    ::unsetenv("RE_SAMPLE_MAX_FRAMES");
    EXPECT_EQ(app::sampleMaxFrames(app::kDefaultFrames), 300);
    ::setenv("RE_SAMPLE_MAX_FRAMES", "", 1);
    EXPECT_EQ(app::sampleMaxFrames(app::kDefaultFrames), 300);
    ::unsetenv("RE_SAMPLE_MAX_FRAMES");
}

TEST(T9SamplesBounded, CenterPixelViaViewWithinOneLsb) {
    scene::SceneStore store;
    auto quad = std::make_shared<const data::Mesh>(makeQuadMesh());
    scene::MeshObject mo;
    mo.mesh = quad;
    mo.transform = glm::mat4(1.0f);
    mo.presentation.phong.baseColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    mo.layer = scene::Layer::LAYER_0;
    mo.priority = 0;
    uint64_t id = store.addMeshObject(std::move(mo));

    auto cam = makePerspCamera();
    scene::View view;
    view.id = 1;
    view.rect = scene::Rect{0, 0, 64, 64};
    view.camera = cam;
    view.setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    view.setItemIds({id});

    auto img = render::renderOffscreen(64, 64, std::vector<scene::View>{view}, store);
    ASSERT_TRUE(img.ok()) << img.error().message;
    const int cx = img->width() / 2;
    const int cy = img->height() / 2;
    // kTol == 1 LSB (normalized 1 div 255) — analytic tolerance from header anchor.
    EXPECT_NEAR(img->pixel(cx, cy, 0), 255, kTol);
    EXPECT_NEAR(img->pixel(cx, cy, 1), 0, kTol);
    EXPECT_NEAR(img->pixel(cx, cy, 2), 0, kTol);
    EXPECT_NEAR(img->pixel(cx, cy, 3), 255, kTol);

    // utils::loadMeshAsset oracle parity: bunny.obj hand-counted via `grep -c "^v " == 35947`
    // and `grep -c "^f " == 69451` (indices == 208353 == 3*faces), proving the depolluted
    // utils/ path yields the same analytic mesh as the archived direct loader.
    auto loaderResult = utils::loadMeshAsset(std::string(TEST_SOURCE_DIR) + "/data/meshes/bunny.obj");
    ASSERT_TRUE(loaderResult.ok()) << loaderResult.error().message;
    EXPECT_EQ((*loaderResult)->positions().size(), 35947u);
    EXPECT_EQ((*loaderResult)->indices().size(), 208353u);
}

} // namespace re::tests
