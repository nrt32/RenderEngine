// app/imgui_overlay.cpp — ImGuiOverlay implementation (SPEC §3, V5 T3).
//
// The sole owner of the ImGui GLFW backend init (V5 T3 gate for the harness
// file — the harness no longer owns the ImGui init, the overlay does). The
// implementation mirrors the previous `SampleHarness::initImGui` /
// `shutdownImGui` + per-frame
// overlay code, extracted so `FrameLoop` + `renderViews` are usable without a
// window or ImGui (prerequisite for T4 offscreen).

#include "app/imgui_overlay.hpp"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include <string>

#include "app/sample_harness.hpp"

namespace re::app {

ImGuiOverlay::~ImGuiOverlay() {
    shutdown();
}

ImGuiOverlay::ImGuiOverlay(ImGuiOverlay&& other) noexcept
    : initialized_(other.initialized_) {
    other.initialized_ = false;
}

ImGuiOverlay& ImGuiOverlay::operator=(ImGuiOverlay&& other) noexcept {
    if (this != &other) {
        shutdown();
        initialized_ = other.initialized_;
        other.initialized_ = false;
    }
    return *this;
}

bool ImGuiOverlay::init(GLFWwindow* /*borrow*/ window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Do not persist an imgui.ini beside the sample binary/cwd: samples are
    // gate-driven and must be deterministic — every run starts from the same
    // UI state, and the repo tree stays clean of runtime droppings.
    io.IniFilename = nullptr;

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        spdlog::error("overlay: ImGui GLFW backend init failed");
        ImGui::DestroyContext();
        return false;
    }
    // The OpenGL3 backend uses its own self-contained imgl3w loader; the GL
    // entry points it needs are resolved lazily against the current context
    // (which core::Window already made current and loaded via glad).
    if (!ImGui_ImplOpenGL3_Init("#version 450")) {
        spdlog::error("overlay: ImGui OpenGL3 backend init failed");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    initialized_ = true;
    return true;
}

void ImGuiOverlay::shutdown() noexcept {
    if (!initialized_) {
        return;
    }
    initialized_ = false;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiOverlay::newFrame() noexcept {
    if (!initialized_) {
        return;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiOverlay::render() noexcept {
    if (!initialized_) {
        return;
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiOverlay::drawSampleOverlay(const ISample& sample, int frame, int maxFrames) noexcept {
    if (!initialized_) {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("RenderEngine Sample", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("%s", sample.title());
    if (maxFrames > 0) {
        ImGui::Text("Frame %d / %d", frame + 1, maxFrames);
    } else {
        ImGui::Text("Frame %d", frame + 1);
    }
    const char* /*borrow*/ instructions = sample.instructions(); // @note lifetime: borrowed — owned by sample, valid for duration of call
    if (instructions != nullptr && instructions[0] != '\0') {
        ImGui::Separator();
        ImGui::TextWrapped("How to drive this capability:");
        const std::string text(instructions);
        std::size_t start = 0u;
        while (start < text.size()) {
            const std::size_t nl = text.find('\n', start);
            const std::string line = (nl == std::string::npos) ? text.substr(start)
                                                                : text.substr(start, nl - start);
            ImGui::BulletText("%s", line.c_str());
            if (nl == std::string::npos) {
                break;
            }
            start = nl + 1u;
        }
    }
    ImGui::End();
}

} // namespace re::app
