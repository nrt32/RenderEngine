// app/sample_harness.cpp — shared sample harness implementation: window +
// GL context setup, per-frame ImGui overlay, and the run loop that calls the
// sample's renderFrame. All samples share this scaffolding so a sample file
// contains only scene/camera/renderer wiring, never platform code.

#include "app/sample_harness.hpp"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include "core/re_context.hpp"

#include <cstdlib>
#include <string>
#include <utility>

namespace re::app {

SampleHarness::SampleHarness(core::Window window,
                             std::unique_ptr<ISample> sample)
    : window_(std::move(window)), sample_(std::move(sample)) {}

SampleHarness::~SampleHarness() {
    shutdownImGui();
}

bool SampleHarness::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Do not persist an imgui.ini beside the sample binary/cwd: samples are
    // gate-driven and must be deterministic — every run starts from the same
    // UI state, and the repo tree stays clean of runtime droppings.
    io.IniFilename = nullptr;

    if (!ImGui_ImplGlfw_InitForOpenGL(window_.handle(), true)) {
        spdlog::error("harness: ImGui GLFW backend init failed");
        ImGui::DestroyContext();
        return false;
    }
    // The OpenGL3 backend uses its own self-contained imgl3w loader; the GL
    // entry points it needs are resolved lazily against the current context
    // (which core::Window already made current and loaded via glad).
    if (!ImGui_ImplOpenGL3_Init("#version 450")) {
        spdlog::error("harness: ImGui OpenGL3 backend init failed");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    imGuiInitialized_ = true;
    return true;
}

void SampleHarness::shutdownImGui() {
    if (!imGuiInitialized_) {
        return;
    }
    imGuiInitialized_ = false;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

int SampleHarness::run(int maxFrames) {
    if (sample_ == nullptr) {
        spdlog::error("harness: no sample set");
        return 2;
    }

    if (!initImGui()) {
        return 3;
    }

    int frames = 0;
    bool frameOk = true;
    while (!window_.shouldClose() && frames < maxFrames && frameOk) {
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

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Render the sample's 3D scene into the window's default framebuffer.
        const data::Result<void> frame =
            sample_->renderFrame(window_.width(), window_.height());
        if (frame.failed()) {
            spdlog::error("harness: sample frame {} failed: {}", frames + 1,
                          frame.error().message);
            frameOk = false;
            continue;
        }

        // ImGui overlay on top of the rendered scene.
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.5f);
        ImGui::Begin(
            "RenderEngine Sample", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("%s", sample_->title());
        ImGui::Text("Frame %d / %d", frames + 1, maxFrames);
        const char* instructions = sample_->instructions();
        if (instructions != nullptr && instructions[0] != '\0') {
            ImGui::Separator();
            // The instructions text is split into lines on '\n' so the overlay
            // wraps them cleanly (ImGui::TextWrapped renders one paragraph).
            ImGui::TextWrapped("How to drive this capability:");
            const std::string text(instructions);
            std::size_t start = 0u;
            while (start < text.size()) {
                const std::size_t nl = text.find('\n', start);
                const std::string line = (nl == std::string::npos)
                                             ? text.substr(start)
                                             : text.substr(start, nl - start);
                ImGui::BulletText("%s", line.c_str());
                if (nl == std::string::npos) {
                    break;
                }
                start = nl + 1u;
            }
        }
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

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
    shutdownImGui();
    return frameOk ? 0 : 1;
}

float aspectFromDims(int width, int height) noexcept {
    const float w = static_cast<float>(width > 0 ? width : 1);
    const float h = static_cast<float>(height > 0 ? height : 1);
    return w / h;
}

void fitPerspectiveViewToPixels(scene::View& view,
                                const PerspectiveFraming& framing, int width,
                                int height) noexcept {
    // The view covers the whole window and the camera's projection aspect
    // follows the live pixel ratio; fov/near/far (and the eye framing built by
    // the sample) are untouched. Both setters only bump generations on a real
    // change, so repeated calls with unchanged dims cost nothing downstream.
    view.setRect(scene::Rect{0, 0, width, height});
    view.mutateCamera([&framing, width, height](scene::Camera& camera) {
        camera.setPerspective(framing.fovDeg, aspectFromDims(width, height),
                              framing.nearPlane, framing.farPlane);
    });
}

int sampleMaxFrames(int defaultFrames) noexcept {
    const char* env = std::getenv("RE_SAMPLE_MAX_FRAMES");
    if (env == nullptr || env[0] == '\0') {
        return defaultFrames;
    }
    const int parsed = std::atoi(env);
    return parsed > 0 ? parsed : defaultFrames;
}

} // namespace re::app
