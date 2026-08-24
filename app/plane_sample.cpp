// app/plane_sample.cpp — plane rendering sample (T12, FR-app.1).
//
// Demonstrates the textured-plane capability: builds a deterministic procedural
// RGBA gradient image in code (no asset dependency), expresses it scene-side as
// a PlaneObject{image asset ref, transform, presentation}, translates it through
// the broker mediation layer (broker::PlaneMapper, V3.4b T12) into the RE-side
// render::PlaneInstance, and drives it through the shared app::SampleHarness
// (visible window + ImGui overlay + run loop) via one full-window ReView:
// render::View composes the layer through PlaneRenderer::drawLayer into its own
// ViewTarget (GPU .glsl plane.vert/frag.glsl) and presents it into the window's
// default framebuffer with core::blit (T5 V3.4 engine present).
//
// app/ holds NO quad geometry: the unit-quad geometry type stays inside
// render/ (the shared unit quad is bound by PlaneMapper; the unit-quad VAO is
// owned by PlaneRenderer alone), so there is no CPU quad vertex generation in
// this sample (V3.4b T12: every textured-plane display reaches the GPU only
// through render::PlaneRenderer).
//
// The sample exits cleanly (code 0) after RE_SAMPLE_MAX_FRAMES frames (default
// 300), so the gate can run it headlessly under Xvfb within a timeout
// (FR-app.1: exit code 0, no sanitizer reports).

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp> // glm::vec4 view clear color (renderFrame)
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/sample_harness.hpp"
#include "broker/broker.hpp"
#include "broker/plane_mapper.hpp"
#include "core/draw.hpp"
#include "core/window.hpp"
#include "data/image.hpp"
#include "data/result.hpp"
#include "render/plane_renderer.hpp"
#include "render/view.hpp"
#include "scene/object.hpp"
#include "scene/translate_context.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

// The harness window size.
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
// Default number of frames before the sample exits cleanly.
constexpr int kDefaultFrames = 300;
// The procedural texture size (square).
constexpr int kTexSize = 256;
// Perspective vertical field of view in radians (~60 deg).
constexpr float kFovY = 1.0471975511965976f;

/// Build a deterministic RGBA gradient image: R = x, G = y, B = (x+y)/2,
/// A = 255. Closed-form, so the sample is reproducible with no asset file.
re::data::Image makeGradientImage() {
    std::vector<std::uint8_t> pixels;
    pixels.reserve(static_cast<std::size_t>(kTexSize) * kTexSize * 4u);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            pixels.push_back(static_cast<std::uint8_t>(x));           // R
            pixels.push_back(static_cast<std::uint8_t>(y));           // G
            pixels.push_back(static_cast<std::uint8_t>((x + y) / 2)); // B
            pixels.push_back(255u);                                   // A
        }
    }
    return re::data::Image(kTexSize, kTexSize, 4, std::move(pixels));
}

/// The plane sample: owns the procedural image + the broker-mediated plane
/// layer and renders one frame through one full-window ReView.
class PlaneSample final : public re::app::ISample {
   public:
    PlaneSample()
        : image_(std::make_shared<re::data::Image>(makeGradientImage())) {
        // Scene side: only {asset ref, transform, presentation} (SPEC §3.1
        // SceneObject family). No geometry, no UVs, no vertex data. The asset
        // ref is the sample's shared_ptr — the scene object co-owns it (T13).
        re::scene::PlaneObject plane;
        plane.image = image_;
        plane.transform = glm::mat4(1.0f);

        // Composition root: register the plane mapper ONCE (one mapper per
        // AppT, OCP via type_index — SPEC §11), then translate only through
        // the type-erased IMapper interface fetched from the Broker — app
        // never holds a concrete mapper handle. The mapped instance SHARES
        // image_ and PlaneMapper's shared unit quad (shared_ptr co-ownership,
        // T13) — nothing to outlive, nothing to dangle.
        broker_.registerMapper(
            std::make_unique<re::broker::PlaneMapper>());
        auto* planeMapper = broker_.get<re::scene::PlaneObject,
                                        re::render::PlaneInstance>();
        re::scene::TranslateContext ctx;
        auto mapped = planeMapper->map(plane, ctx);
        if (mapped.failed()) {
            // Typed errors are surfaced, never swallowed: a skipped layer is
            // visually indistinguishable from an empty viewport.
            spdlog::error("plane sample: plane translation failed (code {}): {}",
                          mapped.error().code, mapped.error().message);
        } else {
            scene_.planes.push_back(*mapped);
        }

        camera_.position = glm::vec3(0.0f, 0.0f, 3.0f);
        camera_.view =
            glm::lookAt(camera_.position, glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f));
        camera_.proj = glm::perspective(kFovY,
                                        static_cast<float>(kWindowWidth) /
                                            static_cast<float>(kWindowHeight),
                                        0.1f, 10.0f);
    }

    re::data::Result<void> renderFrame(int width, int height) override {
        // One ReView pinned to the whole window: the layer renders into the
        // View's own ViewTarget via PlaneRenderer::drawLayer (ReView does the
        // bind+viewport+clear through DrawContext), then core::blit presents
        // the FBO into the default framebuffer rect (nullptr = window FB).
        const re::render::ViewRect rect{0, 0, width, height};
        re::render::View view(rect, glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));
        view.setCamera(camera_);
        if (!scene_.planes.empty()) {
            view.addItem(scene_, renderer_);
        }
        re::core::DrawContext ctx;
        auto rendered = view.renderWithEnsure(ctx);
        if (rendered.failed()) {
            return rendered;
        }
        return view.blitTo(nullptr);
    }

    const char* title() const override {
        return "Plane sample: textured gradient quad";
    }

    const char* instructions() const noexcept override {
        return "Capability: textured plane (SPEC FR-render.5).\n"
               "A unit XY quad textured with a closed-form RGBA gradient is "
               "drawn through render::PlaneRenderer via broker::PlaneMapper "
               "(feeds the MPR views).\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    std::shared_ptr<re::data::Image> image_;
    re::render::PlaneScene scene_;
    re::render::Camera camera_;
    re::broker::Broker broker_;
    // The shared GPU asset store (SPEC §7 T14): one GPU 2D texture per
    // distinct image content, co-owned by every renderer that resolves
    // through it. Declared before its renderer and injected as a shared_ptr
    // copy, so member-init order can never dangle it (T13).
    std::shared_ptr<re::render::AssetRegistry> assets_{
        std::make_shared<re::render::AssetRegistry>()};
    // Shared renderer: the View's renderable items co-own it via shared_ptr,
    // so view and renderer lifetimes can never race at teardown — whichever
    // dies first, the other still holds a valid reference.
    std::shared_ptr<re::render::PlaneRenderer> renderer_{
        std::make_shared<re::render::PlaneRenderer>(assets_)};
};

} // namespace

int main() {
    auto windowResult = re::core::Window::create(kWindowWidth, kWindowHeight,
                                                 "RenderEngine - Plane Sample");
    if (windowResult.failed()) {
        spdlog::error("plane sample: {}", windowResult.error().message);
        return 1;
    }

    auto sample = std::make_unique<PlaneSample>();
    re::app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.run(re::app::sampleMaxFrames(kDefaultFrames));
}
