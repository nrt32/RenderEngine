// tests/t10_oit_test.cpp — T10 gate tests (FR-render.2/3, SPEC §4) — T3b View port.
//
// Asserts via View + ViewCompositor single OIT path (T3b): MeshRenderer no
// longer has inline OIT branch (single drawInstances blend-off), OIT only via
// broker/view_compositor.cpp:94 captureTransparents out-of-band.
//
// Analytic setup unchanged: two full-screen quads {0.4,0.2,0.1,0.5} near at z=0
// and {0.1,0.6,0.3,0.4} far at z=-1 composite to {56,56,28,179} within one-over-255 tolerance.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

#include "broker/broker.hpp"
#include "broker/render_stack.hpp"
#include "broker/view_compositor.hpp"
#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "core/re_context.hpp"
#include "utils/pixel_reader.hpp"
#include "core/texture2d.hpp"
#include "data/mesh.hpp"
#include "render/asset_registry.hpp"
#include "render/imaterial.hpp"
#include "render/linked_list_oit.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "render/view.hpp"
#include "scene/view.hpp"
#include "tests/offscreen_fixture.hpp"
#include "tests/test_helpers.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants (FR-render.2/3).
// ---------------------------------------------------------------------------

constexpr std::uint32_t kTargetWidth = 64u;
constexpr std::uint32_t kTargetHeight = 64u;
constexpr int kColorTolerance = 1; // one-over-255 per FR-render.2 (tolerance 1)

constexpr glm::vec4 kNearColor(0.4f, 0.2f, 0.1f, 0.5f);
constexpr glm::vec4 kFarColor(0.1f, 0.6f, 0.3f, 0.4f);

constexpr std::uint8_t kExpectedR = 56u;
constexpr std::uint8_t kExpectedG = 56u;
constexpr std::uint8_t kExpectedB = 28u;
constexpr std::uint8_t kExpectedA = 179u;

constexpr std::uint32_t kExpectedCapturedFragments =
    kTargetWidth * kTargetHeight * 2u;

struct RenderedTarget {
    core::Texture2D color;
    core::Framebuffer framebuffer;
    RenderedTarget(core::Texture2D c, core::Framebuffer f)
        : color(std::move(c)), framebuffer(std::move(f)) {}
};

[[maybe_unused]] RenderedTarget makeTarget(std::uint32_t w, std::uint32_t h) {
    auto color = core::Texture2D::create();
    auto framebuffer = core::Framebuffer::create();
    EXPECT_TRUE(color.ok()) << color.error().message;
    EXPECT_TRUE(framebuffer.ok()) << framebuffer.error().message;
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(w) * h * 4u, 0u);
    color->bind(0u);
    color->upload(w, h, zeros.data());
    color->unbind(0u);
    framebuffer->bind();
    framebuffer->attachColor(*color);
    EXPECT_TRUE(framebuffer->isComplete());
    framebuffer->unbind();
    return RenderedTarget(std::move(*color), std::move(*framebuffer));
}

class [[maybe_unused]] RecordingPipeline final : public render::ITransparencyPipeline {
   public:
    data::Result<void> begin(const render::Camera&,
                             const render::RenderTarget&,
                             core::REContext&) override {
        ++beginCount_;
        return data::Result<void>(data::value);
    }
    data::Result<void> drawTransparent(const render::MeshGeometry&,
                                       const glm::vec4&, const glm::mat4&,
                                       const render::Camera&) override {
        ++drawTransparentCount_;
        return data::Result<void>(data::value);
    }
    data::Result<void> end(const render::Camera&,
                           const render::RenderTarget&,
                           core::REContext&) override {
        ++endCount_;
        return data::Result<void>(data::value);
    }
    bool isEngaged() const noexcept override { return beginCount_ > endCount_; }
    int beginCount() const noexcept { return beginCount_; }
    int drawTransparentCount() const noexcept { return drawTransparentCount_; }
    int endCount() const noexcept { return endCount_; }

   private:
    int beginCount_{0};
    int drawTransparentCount_{0};
    int endCount_{0};
};

} // namespace

// ---------------------------------------------------------------------------
// (1) FR-render.2 — two overlapping quads composite to depth-ordered blend
//     within one-over-255 via View + ViewCompositor single OIT path.
// ---------------------------------------------------------------------------

TEST(T10Oit, TwoQuadsCompositeToDepthOrderedBlend) {
    data::Mesh quad = makeQuad();
    auto registry = std::make_shared<render::AssetRegistry>();
    const auto handle = registry->registerAsset(quad);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    auto nearMaterial = std::make_shared<render::PhongMaterial>(kNearColor);
    auto farMaterial = std::make_shared<render::PhongMaterial>(kFarColor);
    ASSERT_TRUE(nearMaterial->isTransparent());
    ASSERT_TRUE(farMaterial->isTransparent());

    glm::mat4 nearModel(1.0f);
    glm::mat4 farModel = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.0f));

    render::Camera camera = makeCamera();

    // View path: single OIT via broker/view_compositor captureTransparents.
    auto stack = broker::RenderStack::create(registry, true);
    auto pipeline = stack->pipeline;
    ASSERT_NE(pipeline, nullptr);
    auto brokerPtr = std::make_shared<broker::Broker>();
    broker::ViewCompositor compositor(brokerPtr, stack);

    scene::View appView;
    appView.id = 1;
    appView.rect = {0, 0, static_cast<int>(kTargetWidth), static_cast<int>(kTargetHeight)};
    appView.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    render::View* rv = compositor.ensureView(0, appView);
    ASSERT_NE(rv, nullptr);
    rv->setCamera(camera);
    rv->setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    auto et = rv->ensureTarget();
    ASSERT_TRUE(et.ok()) << et.error().message;

    // Both quads are transparent -> route via compositor pending, not View items.
    std::vector<render::MeshInstance> pending;
    pending.push_back(render::MeshInstance{*handle, nearMaterial, nearModel});
    pending.push_back(render::MeshInstance{*handle, farMaterial, farModel});
    compositor.setTransparentItems(0, 1, pending);

    auto result = compositor.renderAll();
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(core::hasPendingGlError());

    rv->target()->framebuffer().bind();
    constexpr std::uint32_t kSampleX[5] = {32u, 8u, 56u, 8u, 56u};
    constexpr std::uint32_t kSampleY[5] = {32u, 8u, 8u, 56u, 56u};
    for (int i = 0; i < 5; ++i) {
        const std::vector<std::uint8_t> pixel = readPixel(kSampleX[i], kSampleY[i]);
        EXPECT_NEAR(pixel[0], kExpectedR, kColorTolerance)
            << "R at (" << kSampleX[i] << "," << kSampleY[i] << ")";
        EXPECT_NEAR(pixel[1], kExpectedG, kColorTolerance)
            << "G at (" << kSampleX[i] << "," << kSampleY[i] << ")";
        EXPECT_NEAR(pixel[2], kExpectedB, kColorTolerance)
            << "B at (" << kSampleX[i] << "," << kSampleY[i] << ")";
        EXPECT_NEAR(pixel[3], kExpectedA, kColorTolerance)
            << "A at (" << kSampleX[i] << "," << kSampleY[i] << ")";
    }
    rv->target()->framebuffer().unbind();
    EXPECT_FALSE(core::hasPendingGlError());

    const auto captured = pipeline->readCapturedFragmentCount();
    ASSERT_TRUE(captured.ok()) << captured.error().message;
    EXPECT_EQ(*captured, kExpectedCapturedFragments);
    EXPECT_FALSE(pipeline->isEngaged());
}

// ---------------------------------------------------------------------------
// (2) FR-render.3 — opaque-only alpha == 1.0 via View path, transparent via
//     compositor spy.
// ---------------------------------------------------------------------------

TEST(T10Oit, OpaqueAlphaIsOneAndTransparentQuadEngagesPipeline) {
    data::Mesh quad = makeQuad();
    auto registry = std::make_shared<render::AssetRegistry>();
    const auto handle = registry->registerAsset(quad);
    ASSERT_TRUE(handle.ok()) << handle.error().message;

    // Opaque-only via View (no compositor pending, alpha 1.0 within one-over-255)
    {
        auto opaque = std::make_shared<render::PhongMaterial>(glm::vec4(0.2f, 0.4f, 0.8f, 1.0f));
        ASSERT_FALSE(opaque->isTransparent());
        render::MeshScene opaqueScene;
        opaqueScene.meshes.push_back(render::MeshInstance{*handle, opaque, glm::mat4(1.0f)});

        auto meshRenderer = std::make_shared<render::MeshRenderer>(registry, nullptr);
        render::View view(render::ViewRect{0, 0, static_cast<int>(kTargetWidth), static_cast<int>(kTargetHeight)},
                          glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        view.setCamera(makeCamera());
        view.addItem(opaqueScene, meshRenderer);
        auto res = view.renderWithEnsure();
        ASSERT_TRUE(res.ok()) << res.error().message;
        ASSERT_NE(view.target(), nullptr);
        view.target()->framebuffer().bind();
        constexpr std::uint32_t kSampleX[4] = {32u, 8u, 56u, 16u};
        constexpr std::uint32_t kSampleY[4] = {32u, 8u, 56u, 48u};
        for (int i = 0; i < 4; ++i) {
            const std::vector<std::uint8_t> pixel = readPixel(kSampleX[i], kSampleY[i]);
            EXPECT_EQ(pixel[3], 255u) << "alpha 1.0 at (" << kSampleX[i] << "," << kSampleY[i] << ")";
        }
        view.target()->framebuffer().unbind();
        EXPECT_FALSE(core::hasPendingGlError());
    }

    // Mixed: opaque via View, transparent via compositor (real pipeline).
    {
        auto opaque = std::make_shared<render::PhongMaterial>(glm::vec4(0.2f, 0.4f, 0.8f, 1.0f));
        auto transparent = std::make_shared<render::PhongMaterial>(glm::vec4(0.4f, 0.2f, 0.1f, 0.5f));
        ASSERT_TRUE(transparent->isTransparent());

        auto pipeline2 = std::make_shared<render::LinkedListOIT>();
        auto registry2 = registry;
        auto stack = std::make_shared<broker::RenderStack>();
        stack->assets = registry2;
        stack->pipeline = pipeline2;
        stack->mesh = std::make_shared<render::MeshRenderer>(registry2, pipeline2);
        stack->meshSlice = std::make_shared<render::SliceRenderer>(registry2);
        stack->volume = std::make_shared<render::VolumeRenderer>(registry2);
        stack->slice = std::make_shared<render::VolumeSliceRenderer>(registry2);
        stack->plane = std::make_shared<render::PlaneRenderer>(registry2);
        stack->contour = std::make_shared<render::ContourRenderer>(registry2);

        auto brokerPtr = std::make_shared<broker::Broker>();
        broker::ViewCompositor compositor(brokerPtr, stack);

        scene::View appView;
        appView.id = 10;
        appView.rect = {0, 0, static_cast<int>(kTargetWidth), static_cast<int>(kTargetHeight)};
        appView.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        render::View* rv = compositor.ensureView(0, appView);
        rv->setCamera(makeCamera());
        rv->setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        // Opaque layer via View item
        render::MeshScene opaqueLayer;
        opaqueLayer.meshes.push_back(render::MeshInstance{*handle, opaque, glm::mat4(1.0f)});
        rv->addItem(opaqueLayer, stack->mesh);
        auto et = rv->ensureTarget();
        ASSERT_TRUE(et.ok()) << et.error().message;

        std::vector<render::MeshInstance> pending;
        pending.push_back(render::MeshInstance{*handle, transparent, glm::mat4(1.0f)});
        compositor.setTransparentItems(0, 10, pending);

        auto result = compositor.renderAll();
        ASSERT_TRUE(result.ok()) << result.error().message;
        EXPECT_FALSE(pipeline2->isEngaged());
        auto cap = pipeline2->readCapturedFragmentCount();
        ASSERT_TRUE(cap.ok()) << cap.error().message;
        EXPECT_EQ(*cap, kTargetWidth*kTargetHeight*1u);
        EXPECT_FALSE(core::hasPendingGlError());
    }
}

// ---------------------------------------------------------------------------
// (3) FR-render.3 — pipeline swappable via compositor.
// ---------------------------------------------------------------------------

TEST(T10Oit, PipelineInterfaceIsSwappable) {
    data::Mesh quad = makeQuad();
    auto registry = std::make_shared<render::AssetRegistry>();
    const auto handle = registry->registerAsset(quad);
    ASSERT_TRUE(handle.ok()) << handle.error().message;
    auto transparent = std::make_shared<render::PhongMaterial>(glm::vec4(0.4f, 0.2f, 0.1f, 0.5f));
    ASSERT_TRUE(transparent->isTransparent());

    auto pipeline3 = std::make_shared<render::LinkedListOIT>();
    auto stack = std::make_shared<broker::RenderStack>();
    stack->assets = registry;
    stack->pipeline = pipeline3;
    stack->mesh = std::make_shared<render::MeshRenderer>(registry, pipeline3);
    stack->meshSlice = std::make_shared<render::SliceRenderer>(registry);
    stack->volume = std::make_shared<render::VolumeRenderer>(registry);
    stack->slice = std::make_shared<render::VolumeSliceRenderer>(registry);
    stack->plane = std::make_shared<render::PlaneRenderer>(registry);
    stack->contour = std::make_shared<render::ContourRenderer>(registry);

    auto brokerPtr = std::make_shared<broker::Broker>();
    broker::ViewCompositor compositor(brokerPtr, stack);
    scene::View appView;
    appView.id = 20;
    appView.rect = {0, 0, static_cast<int>(kTargetWidth), static_cast<int>(kTargetHeight)};
    appView.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    render::View* rv = compositor.ensureView(0, appView);
    rv->setCamera(makeCamera());
    rv->setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    auto et = rv->ensureTarget();
    ASSERT_TRUE(et.ok()) << et.error().message;

    std::vector<render::MeshInstance> pending;
    pending.push_back(render::MeshInstance{*handle, transparent, glm::mat4(1.0f)});
    compositor.setTransparentItems(0, 20, pending);

    auto result = compositor.renderAll();
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(pipeline3->isEngaged());
    auto cap = pipeline3->readCapturedFragmentCount();
    ASSERT_TRUE(cap.ok()) << cap.error().message;
    EXPECT_EQ(*cap, kTargetWidth*kTargetHeight*1u);
    EXPECT_FALSE(core::hasPendingGlError());
}

} // namespace re::tests
