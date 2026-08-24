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
//
// Resize path (T23): core::Window registers the GLFW framebuffer-size
// callback; each frame the harness consumes the dirty latch and — when an
// event arrived — delivers ISample::onResize(currentWidth, currentHeight)
// before the frame. Fixed-size offscreen runs never fire the callback, so the
// headless gate path is unchanged.

#include <memory>

#include "core/window.hpp"
#include "data/result.hpp"
#include "scene/view.hpp"

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
    /// current framebuffer pixel size, supplied by the harness each frame —
    /// samples must derive their view rects and camera aspect from THESE live
    /// dims, never from compile-time window constants, so a window resize
    /// reframes geometry instead of stretching it (T23). Returns a typed error
    /// on failure (SPEC §5) — the harness aborts the run and exits non-zero.
    virtual data::Result<void> renderFrame(int width, int height) = 0;

    /// A short one-line description shown in the ImGui overlay.
    virtual const char* title() const = 0;

    /// Optional resize notification: the harness calls this exactly when one
    /// or more framebuffer-size events arrived since the previous frame,
    /// passing the CURRENT framebuffer pixel size — the same values the very
    /// next renderFrame will receive. The default is a deliberate no-op:
    /// samples that re-derive everything from renderFrame's per-frame pixel
    /// dims need no override. Override to react eagerly between frames; keep
    /// the body idempotent (the setters only bump generations on real change),
    /// because coalesced events can deliver the same size twice.
    virtual void onResize(int /*width*/, int /*height*/) noexcept {}

    /// Optional per-sample instructions on how to drive this capability, shown
    /// as help text in the ImGui overlay (T13, FR-app.1 "per-sample
    /// instructions"). May be multiple lines separated by '\n', or empty when
    /// the sample has nothing to say.
    virtual const char* instructions() const noexcept {
        return "";
    }
};

/// The perspective-framing parameters a full-window view keeps FIXED while the
/// live framebuffer pixel size changes: vertical field of view (degrees) and
/// the near/far clip distances. Eye position and framing distance are derived
/// once by the sample from its scene bounds; a resize changes ONLY the
/// projection aspect (width/height), never the framing — that split is what
/// makes resizes cheap and free of re-framing surprises.
struct PerspectiveFraming {
    float fovDeg{60.0f};     ///< Vertical field of view in degrees.
    float nearPlane{0.1f};   ///< Near clip distance.
    float farPlane{10.0f};   ///< Far clip distance.
};

/// Width/height as the float aspect ratio cameras expect, with degenerate
/// sizes clamped: GLFW can report 0 for a dimension while a window is
/// minimized, and an unguarded division would poison every projection with
/// inf/NaN. Clamping each non-positive dim to 1 keeps the ratio finite and
/// deterministic (the next real-size event corrects it again).
float aspectFromDims(int width, int height) noexcept;

/// Apply live framebuffer pixel dims to ONE full-window perspective view:
/// rect := {0, 0, width, height} and the camera's perspective aspect :=
/// width/height with fov/near/far preserved from `framing`. This is the ONE
/// shared definition of the live-aspect rule for the full-window samples —
/// called both from ISample::onResize and at the top of every renderFrame, so
/// the projection is always derived from the CURRENT pixel size and compile-
/// time window constants never feed projections (a resize reframes instead of
/// stretching). All scene::View setters are change-guarded, so frames where
/// nothing moved cost no generation churn or sync work.
void fitPerspectiveViewToPixels(scene::View& view,
                                const PerspectiveFraming& framing, int width,
                                int height) noexcept;

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
