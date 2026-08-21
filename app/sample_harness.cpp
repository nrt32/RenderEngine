// app/sample_harness.cpp — shared sample harness implementation (T12).

#include "app/sample_harness.hpp"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

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
    // deterministic and must not leave state files behind (SPEC §5
    // determinism).
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
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        window_.swapBuffers();
        ++frames;
    }

    // Single cleanup path; the destructor's call is a no-op via the flag.
    shutdownImGui();
    return frameOk ? 0 : 1;
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
