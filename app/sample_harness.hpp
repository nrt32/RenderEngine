#pragma once

// app/sample_harness.hpp — shared sample harness (SPEC §3, T12, FR-app.1).
//
// A shared app/ component that owns a visible GL 4.6 core window, wires the
// Dear ImGui (GLFW + OpenGL3) overlay, and runs the per-frame loop for the
// capability samples (mesh, plane, volume in T12; slice/OIT in T13; MPR in
// T14/T15). A sample implements the ISample interface (render one frame into
// the window's default framebuffer); the harness presents the frame and draws
// the ImGui overlay on top.
//
// The harness consumes GL only through core/ wrappers (core::Window, the
// core::Draw API via the render/ renderers). ImGui's own GLFW/OpenGL3 backends
// are third-party code; the samples and harness do not call raw glXxx.
//
// The run loop is bounded: run(maxFrames) stops cleanly after maxFrames frames
// (or when the window is closed). The samples read the RE_SAMPLE_MAX_FRAMES
// environment variable so the automated gate (T12) can run them headlessly
// under Xvfb, open a window, and exit cleanly within a timeout (FR-app.1).

#include <memory>

#include "core/window.hpp"
#include "data/result.hpp"

namespace re::app {

/// A sample: builds/renders one frame of the sample's 3D scene into the
/// harness window's default framebuffer. Called once per frame after the ImGui
/// new-frame and before the ImGui overlay is drawn (so the overlay appears on
/// top of the rendered scene).
class ISample {
   public:
    virtual ~ISample() = default;

    /// Render one frame into the window's default framebuffer (a
    /// render::RenderTarget with a null framebuffer). `width`/`height` are the
    /// current framebuffer pixel size, supplied by the harness each frame.
    /// Returns a typed error on failure (SPEC §5) — the harness aborts the run
    /// and exits non-zero.
    virtual data::Result<void> renderFrame(int width, int height) = 0;

    /// A short one-line description shown in the ImGui overlay.
    virtual const char* title() const = 0;

    /// Optional per-sample instructions on how to drive this capability, shown
    /// as help text in the ImGui overlay (T13, FR-app.1 "per-sample
    /// instructions"). May be multiple lines separated by '\n', or empty when
    /// the sample has nothing to say.
    virtual const char* instructions() const noexcept {
        return "";
    }
};

/// Shared sample harness: owns a visible window + GL context, wires the ImGui
/// overlay, and runs the frame loop.
class SampleHarness {
   public:
    /// Construct with the owned window and the sample to drive.
    SampleHarness(core::Window window, std::unique_ptr<ISample> sample);

    SampleHarness(const SampleHarness&) = delete;
    SampleHarness& operator=(const SampleHarness&) = delete;

    SampleHarness(SampleHarness&&) = delete;
    SampleHarness& operator=(SampleHarness&&) = delete;

    ~SampleHarness();

    /// Run the frame loop for up to `maxFrames` frames (or until the window is
    /// closed). Returns the process exit code: 0 on a clean stop, non-zero on a
    /// sample-frame or ImGui-init failure.
    int run(int maxFrames);

    /// The owned window (for samples that need the framebuffer size).
    core::Window& window() noexcept {
        return window_;
    }

   private:
    /// Initialise the ImGui context + GLFW/OpenGL3 backends. Returns false on
    /// failure.
    bool initImGui();
    /// Shut down the ImGui backends + context. Idempotent (safe to call from
    /// both `run()` and the destructor).
    void shutdownImGui();

    core::Window window_;
    std::unique_ptr<ISample> sample_;
    bool imGuiInitialized_{false};
};

/// Read the RE_SAMPLE_MAX_FRAMES environment variable, defaulting to
/// `defaultFrames` when unset/empty. Bounds the sample run loop so the gate can
/// terminate samples headlessly (FR-app.1).
int sampleMaxFrames(int defaultFrames) noexcept;

} // namespace re::app
