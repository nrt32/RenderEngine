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

#include <functional>
#include <memory>
#include <vector>

#include "broker/app_context.hpp"
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

// ---------------------------------------------------------------------------
// Shared sample constants + helpers (T17 AS1/AS2 — batch app polish).
// ---------------------------------------------------------------------------

/// Shared OIT/mesh/plane/volume window size — the OPENING size only; every
/// per-frame view rect and camera aspect derives from the LIVE framebuffer dims
/// via fitPerspectiveViewToPixels / aspectFromDims (T23), so compile-time
/// constants never feed projections.
inline constexpr int kWindowWidth = 800;
inline constexpr int kWindowHeight = 600;
/// MPR window size (SPEC FR-app.2 pins 1280×960 as the default MPR window).
inline constexpr int kMprWindowWidth = 1280;
inline constexpr int kMprWindowHeight = 960;
/// Default number of frames before a sample exits cleanly (gate overrides via
/// RE_SAMPLE_MAX_FRAMES). Single source for the six samples (AS2 constants).
inline constexpr int kDefaultFrames = 300;
/// Default perspective vertical field of view in degrees (~60 deg).
inline constexpr float kDefaultFovYDeg = 60.0f;

/// Per-frame bridge helper (AS2) — dedups the `sync → renderAll → presentAll`
/// triple that previously appeared verbatim in six `ISample::renderFrame`
/// implementations. The helper is the ONE site for the broker façade sequence:
/// sync translates dirty scene fields into cached Re state, renderAll draws every
/// ReView into its own target, presentAll blits each target 1:1 into its window
/// rect (null destination = the window's default framebuffer). Returns the first
/// typed error encountered, or success.
data::Result<void> syncRenderPresent(broker::AppContext& ctx,
                                     const std::vector<scene::View>& views);

/// Main-entry helper (AS2) — dedups the `load → window → harness → run` mains
/// that were sextuplicated across mesh/plane/volume/slice/oit/mpr samples plus
/// their `kWindowWidth/kWindowHeight/kDefaultFrames` constants. The factory
/// creates the sample (including any asset loading it needs) and returns a
/// ready-to-run ISample; the helper owns window creation, harness wiring and
/// the bounded run loop. Returns the process exit code (0 on clean stop).
/// The factory must return nullptr on failure (and log via spdlog); the helper
/// then returns 1 without opening a window.
int runSample(const char* windowTitle, int width, int height,
              int defaultFrames,
              std::function<std::unique_ptr<ISample>()> factory);

/// Read the RE_SAMPLE_MAX_FRAMES environment variable, defaulting to
/// `defaultFrames` when unset/empty. Bounds the sample run loop so the gate can
/// terminate samples headlessly (FR-app.1).
int sampleMaxFrames(int defaultFrames) noexcept;

} // namespace re::app
