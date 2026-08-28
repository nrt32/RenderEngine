#pragma once

// scene/camera_controller.hpp — pure-math camera controller for the scene value library (SPEC §3.1, V5 T9).
//
// The scene layer is GL-free and windowing-system-free, so this header owns only the mathematical mapping from 2D
// input deltas to camera-space deltas, never touching window handles, cursor queries, or overlay capture flags.
// The concrete mapping is intentionally trivial and analytic — a linear scale from pixels to degrees or world units —
// so every unit test can derive the expected view matrix by calling the same Camera primitives (rotate/pan/zoom) with
// the closed-form delta and comparing matrices within 1e-6. The adapter in app (camera interactor) owns the
// per-frame polling of cursor buttons and positions plus the WantCaptureMouse guard and then forwards the delta
// into View::mutateCamera so the generation bump (viewGen) lets the broker re-translate only the dirty camera
// fields per SPEC §10.4 per-field split. This file must never include windowing or overlay headers, keeping the
// scene disposition render-free and testable headlessly via the offscreen fixture. V5 T9.

#include "scene/camera.hpp"

namespace re::scene {

/// Mouse button abstraction for the controller — integer values mirror the usual left/right/middle ordering so the
/// adapter can map windowing constants without leaking windowing headers into scene. The scene layer never sees a
/// windowing handle; the adapter translates windowing button codes into this enum before forwarding to the controller.
enum class MouseButton : int {
    Left = 0,
    Right = 1,
    Middle = 2,
};

/// Plain value struct that captures the input-to-camera mapping constants for one view. The speeds are linear
/// scales: rotateSpeed is degrees per pixel, panSpeed is world units per pixel, zoomSpeed is a fractional factor per
/// scroll unit or per vertical drag pixel. The button fields select which button drives which degree of freedom; the
/// modifiers field is an opaque int mask that must match the adapter's modifier snapshot for the delta to fire. This
/// struct is deliberately POD so samples can override it per view without inventing a new controller type.
struct CameraBindings {
    MouseButton rotateButton{MouseButton::Left};
    MouseButton panButton{MouseButton::Right};
    MouseButton zoomButton{MouseButton::Middle};
    int modifiers{0};
    float rotateSpeed{0.5f};
    float panSpeed{0.01f};
    float zoomSpeed{0.1f};
};

/// Result of one input event interpreted by the controller. Only the fields whose has* flag is set are meaningful;
/// the others stay at their identity defaults (0 for angles/pan, 1 for zoom factor). The adapter inspects these
/// flags and calls View::mutateCamera with the matching Camera primitive (rotate/pan/zoom) exactly once per flagged
/// degree of freedom, so a pure yaw drag does not dirt pan or zoom generations.
struct CameraDelta {
    float yawDeg{0.0f};
    float pitchDeg{0.0f};
    float panX{0.0f};
    float panY{0.0f};
    float zoomFactor{1.0f};
    bool hasRotate{false};
    bool hasPan{false};
    bool hasZoom{false};
};

/// Pure-math controller — no windowing or overlay includes, no GL, no render types. The controller is a value
/// object that holds CameraBindings and exposes onMouseDrag / onScroll / onKey translators that return a CameraDelta
/// the caller can apply to a Camera via apply(). The per-frame adapter in the application layer owns the polling and the
/// WantCaptureMouse guard; this class is therefore trivially unit-testable headlessly by feeding synthetic deltas
/// and comparing the resulting viewMatrix against the analytic camera primitive within 1e-6.
class CameraController {
   public:
    explicit CameraController(CameraBindings bindings = CameraBindings{}) noexcept
        : bindings_(bindings) {}

    void setBindings(CameraBindings b) noexcept { bindings_ = b; }
    const CameraBindings& bindings() const noexcept { return bindings_; }

    /// Translate a mouse drag delta in pixels into a camera delta. The button and modifiers are compared against
    /// the stored bindings; a mismatch returns an empty delta (all has* false) so the caller can skip the
    /// View::mutateCamera bump. For the rotate button, yaw = dx * rotateSpeed and pitch = -dy * rotateSpeed
    /// (drag right yaws right, drag up pitches up); pan maps similarly via panSpeed; zoom via middle-button
    /// vertical drag maps to an exponential factor exp(-dy * zoomSpeed * 0.02) so a 10-pixel drag yields a
    /// deterministic factor within 1e-6.
    CameraDelta onMouseDrag(float dx, float dy, MouseButton button, int modifiers) const noexcept;

    /// Translate a vertical scroll delta (positive away from user) into a zoom delta. The factor is
    /// exp(-delta * zoomSpeed * 0.2) with clamping away from zero, so one scroll notch yields a reproducible
    /// analytic factor and repeated scrolls compose multiplicatively.
    CameraDelta onScroll(float delta) const noexcept;

    /// Keyboard handler — reserved for future bindings, currently returns an empty delta for any key so the
    /// controller stays forward-compatible without breaking the analytic 1e-6 gates. The parameters are opaque
    /// ints matching the usual key/action/mod mask, but the scene layer never interprets windowing key codes.
    CameraDelta onKey(int key, int action, int mods) const noexcept;

    /// Apply a CameraDelta to a Camera in place, calling rotate/pan/zoom exactly for the flagged degrees of
    /// freedom. Each call bumps viewGen exactly once per flagged branch (rotate/pan/zoom are separate bumps),
    /// preserving the per-field generation split so View::mutateCamera can propagate the bump to view generation
    /// and the broker re-translates only the dirty camera fields.
    void apply(Camera& cam, const CameraDelta& delta) const noexcept;

   private:
    CameraBindings bindings_{};
};

} // namespace re::scene
