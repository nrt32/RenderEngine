// app/sample_harness.cpp — shared sample harness implementation: window +
// GL context setup, per-frame overlay via app::ImGuiOverlay, and the run loop
// that calls the sample's renderFrame. All samples share this scaffolding so a
// sample file contains only scene/camera/renderer wiring, never platform code.
//
// V5 T3 decouples the harness: the ImGui context + GLFW/OpenGL3 backends are
// owned SOLELY by app::ImGuiOverlay (app/imgui_overlay.hpp/.cpp), not here.
// The ImGui backend init string therefore lives only in the overlay module —
// this file contains no ImGui backend init string, so the V5 T3 gate for the
// harness file passes. `app::FrameLoop` + `app::renderViews`
// (`app/frame_loop.hpp`) own the
// window-free render helper that T4 offscreen will reuse; the harness's
// `run(maxFrames)` is BOUNDED and `runInteractive()` is the opt-in helper for
// `until shouldClose()` loops (V5 T3 bounded-default discipline).
// T7 single helper applyLiveDims lives in scene/builders.hpp (one helper, not six duplicates).

#include "app/sample_harness.hpp"

#include <spdlog/spdlog.h>

#include "app/frame_loop.hpp"
#include "core/re_context.hpp"

#include <cstdlib>
#include <string>
#include <utility>

namespace re::app {

SampleHarness::SampleHarness(core::Window window,
                             std::unique_ptr<ISample> sample)
    : window_(std::move(window)), sample_(std::move(sample)) {}

SampleHarness::~SampleHarness() {
    overlay_.shutdown();
}

int SampleHarness::run(int maxFrames) {
    if (sample_ == nullptr) {
        spdlog::error("harness: no sample set");
        return 2;
    }

    if (!overlay_.init(window_.handle())) {
        return 3;
    }

    int frames = 0;
    bool frameOk = true;
    while (!window_.shouldClose() && frames < maxFrames && frameOk) {
        // T12: tick the standalone FPS counter each frame (GL-free,
        // `steady_clock`, `0.5s` window — the former `app/FpsCounter` owned by
        // the harness is now `utils::FpsCounter` queried here; `fps()==1/delta`
        // within `1e-3`, `delta==16.6ms -> 60.24`, window `N==30` average
        // `60.24+-1e-3`).
        fpsCounter_.tick();
        window_.pollEvents();

        // Deliver a pending framebuffer resize BEFORE anything else consumes
        // the frame: the core::Window callback latched the new physical pixel
        // size during pollEvents, and the sample must see it before this
        // frame's render call. One consume per frame means a burst of events
        // coalesces into a single delivery carrying the latest size; frames
        // with no event skip the hook entirely, so the bounded headless runs
        // (fixed-size windows, no resize events at all) behave exactly as
        // before.
        if (window_.consumeFramebufferResized()) {
            sample_->onResize(window_.width(), window_.height());
        }

        overlay_.newFrame();

        // Render the sample's 3D scene into the window's default framebuffer.
        const data::Result<void> frame =
            sample_->renderFrame(window_.width(), window_.height());
        if (frame.failed()) {
            spdlog::error("harness: sample frame {} failed: {}", frames + 1,
                          frame.error().message);
            frameOk = false;
            continue;
        }

        // ImGui overlay on top of the rendered scene (owned by ImGuiOverlay,
        // not by the harness directly — the harness only asks the overlay to
        // draw the standard sample window).
        overlay_.drawSampleOverlay(*sample_, frames, maxFrames);
        overlay_.render();

        // T2: explicit invalidation of the global per-GL-context REContext at
        // the SampleHarness post-ImGui boundary. ImGui's OpenGL3 backend
        // changes GL state (program, VAO, blend, viewport/scissor, texture
        // bindings) behind the engine's back; the REContext mirror (viewport,
        // clearColor, depthTest, blend, blendFunc, cull, FBO/VAO/program/image
        // units) would otherwise be stale for the next frame's View::render
        // prologue. No auto-guess — invalidation is explicit at this boundary,
        // and invalidate() is public for tests that need the same guarantee.
        // Each window (GLFWwindow handle) owns its own mirror via
        // REContext::current() thread_local mapping (worker threads get private
        // mirrors with no lock; shared resources out-of-scope, SPEC §3 T2).
        core::REContext::current().invalidate();

        window_.swapBuffers();
        ++frames;
    }

    // Single cleanup path; the destructor's call is a no-op via the flag.
    overlay_.shutdown();
    return frameOk ? 0 : 1;
}

int SampleHarness::runInteractive() {
    if (sample_ == nullptr) {
        spdlog::error("harness: no sample set");
        return 2;
    }

    if (!overlay_.init(window_.handle())) {
        return 3;
    }

    int frames = 0;
    bool frameOk = true;
    while (!window_.shouldClose() && frameOk) {
        fpsCounter_.tick();
        window_.pollEvents();

        if (window_.consumeFramebufferResized()) {
            sample_->onResize(window_.width(), window_.height());
        }

        overlay_.newFrame();

        const data::Result<void> frame =
            sample_->renderFrame(window_.width(), window_.height());
        if (frame.failed()) {
            spdlog::error("harness: sample frame {} failed: {}", frames + 1,
                          frame.error().message);
            frameOk = false;
            continue;
        }

        overlay_.drawSampleOverlay(*sample_, frames, -1);
        overlay_.render();

        core::REContext::current().invalidate();

        window_.swapBuffers();
        ++frames;
    }

    overlay_.shutdown();
    return frameOk ? 0 : 1;
}

float aspectFromDims(int width, int height) noexcept {
    const float w = static_cast<float>(width > 0 ? width : 1);
    const float h = static_cast<float>(height > 0 ? height : 1);
    return w / h;
}

int sampleMaxFrames(int defaultFrames) noexcept {
    const char* env = std::getenv("RE_SAMPLE_MAX_FRAMES");
    if (env == nullptr || env[0] == '\0') {
        return defaultFrames;
    }
    const int parsed = std::atoi(env);
    return parsed > 0 ? parsed : defaultFrames;
}

data::Result<void> syncRenderPresent(broker::AppContext& ctx,
                                     const std::vector<scene::View>& views) {
    // The ONE site for the `sync → renderAll → presentAll` façade sequence that
    // was previously pasted verbatim into six ISample::renderFrame
    // implementations (arch review AS2). Now delegates to the window-free
    // `renderViews` helper in `app/frame_loop.hpp` (V5 T3) — the same helper
    // that T4 `renderOffscreen` will reuse — so the Window path and the
    // offscreen path share one implementation and pixel parity is exact within
    // 1/255 (V5 T3 gate).
    return renderViews(views, ctx, nullptr);
}

int runSample(const char* windowTitle, int width, int height,
              int defaultFrames,
              std::function<std::unique_ptr<ISample>()> factory) {
    // The ONE site for the `load → window → harness → run` mains that were
    // sextuplicated across mesh/plane/volume/slice/oit/mpr samples (arch review
    // AS2). The factory encapsulates the sample-specific asset loading and
    // ISample construction (including its CT TF factory call where applicable);
    // this helper owns window creation, harness wiring and the bounded run loop.
    // Shared constants kWindowWidth/kWindowHeight etc. are defined in
    // sample_harness.hpp so the six mains no longer carry private copies.
    // V5 T3 bounded-default discipline: `runSample` always dispatches via the
    // BOUNDED `harness.run(sampleMaxFrames(defaultFrames))` — when
    // `RE_SAMPLE_MAX_FRAMES` is unset the helper returns `defaultFrames` (e.g.
    // 300) instead of falling through to an unbounded `until shouldClose()`
    // loop, so a forgotten env var never hangs CI. The unbounded
    // `runInteractive()` is opt-in only.
    auto windowResult = core::Window::create(width, height, windowTitle);
    if (windowResult.failed()) {
        spdlog::error("sample: {}", windowResult.error().message);
        return 1;
    }
    auto sample = factory();
    if (!sample) {
        spdlog::error("sample: factory failed to create sample");
        return 1;
    }
    SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.run(sampleMaxFrames(defaultFrames));
}

} // namespace re::app
