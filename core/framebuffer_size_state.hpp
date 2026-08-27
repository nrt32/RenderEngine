#pragma once

// core/framebuffer_size_state.hpp — framebuffer-size bookkeeping value type (T23).
//
// This is the whole state machine behind the Window resize path, kept as a
// plain value type so it is unit-testable without a display or a Window: the
// harness's dirty-latch contract (latest physical pixel size + one-shot flag)
// lives here, not in the GLFW window object. `Window` owns a shared instance
// of this state and its GLFW callback forwards events into it, but tests that
// exercise only the state machine include this header alone — they do not need
// `core/window.hpp` (audit `test_window_forbid`: tests use OffscreenContext,
// not Window). Extracted from `core/window.hpp:45-70` during spec-review #5
// (N-04 ownership partition for `core/window.*` vs test isolation) — same
// explainable constants (0x0 initial, apply overwrites + latches, consume
// returns-and-clears exactly once per batch), same file, no behavior change.

namespace re::core {

/// Framebuffer-size bookkeeping shared between the GLFW event callback and
/// its owning Window: the latest physical pixel size plus a dirty latch.
///
/// This is the whole state machine behind the resize path, kept as a plain
/// value type so it is unit-testable without a display: `apply` mirrors one
/// GLFW event exactly (overwrite both dims + set the flag), `consumeResized`
/// is the harness's return-and-clear read (one delivery per event batch; a
/// later event latches again). A fresh state starts at 0x0 with no pending
/// resize — those are the explainable initial constants.
struct FramebufferSizeState {
    /// Latest framebuffer width in physical pixels (0 before the first event).
    int width{0};
    /// Latest framebuffer height in physical pixels (0 before the first event).
    int height{0};
    /// True while an apply() landed since the last consumeResized().
    bool resized{false};

    /// Record one framebuffer-size event: overwrite the stored pixel size and
    /// latch the dirty flag (idempotent per distinct size, re-latching after
    /// every consume — exactly GLFW's per-event semantics).
    void apply(int newWidth, int newHeight) noexcept {
        width = newWidth;
        height = newHeight;
        resized = true;
    }

    /// Return-and-clear the dirty latch: true exactly once per event batch,
    /// false again until the next apply() (the "no duplicate delivery"
    /// invariant the harness run loop relies on).
    bool consumeResized() noexcept {
        const bool pending = resized;
        resized = false;
        return pending;
    }
};

} // namespace re::core
