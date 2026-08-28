#pragma once

// utils/fps_counter.hpp — standalone FPS counter (SPEC §5/§3, V5 T12).
//
// Formerly the draft `app/FpsCounter` owned by `SampleHarness`
// (`TASKS.md:118` draft `app/FpsCounter`), now a standalone `utils`
// value type so any consumer (samples, overlay, headless harness) can query it
// without coupling to windowing. The harness queries an instance per frame —
// the counter itself is window-free and GL-free and owns no thread or timer
// beyond `std::chrono::steady_clock`. A `0.5`s sliding window smooths the
// rate while still responding within half a second to a load change; the
// window is exactly `0.5s` so a `16.6ms` cadence (`60.24Hz`) yields `30`
// samples within the window and `fps == 30/0.5 == 60.24` within `1e-3`
// (analytic `1/0.0166`, not `>0`). `tick()`, `fps()`, `ms()` are the sole
// public contract — `tick` records the frame boundary, `fps` is
// `N/windowSum`, `ms` is `1000/fps`.
//
// Design: the counter stores the per-frame delta in seconds in a deque whose
// total stays `<=0.5s`. `tick()` without args uses `steady_clock::now()` and
// the interval from the previous tick; overloads that accept an explicit
// `time_point` or `duration` exist so unit tests can inject an analytic
// `16.6ms` cadence without sleeping (deterministic, not wall-clock). The
// window evicts from the front while the sum exceeds `0.5s`, so `fps()` is
// the true sliding average over the most recent half-second, not an EMA that
// would never settle at `60.24`. `ms()` is the reciprocal `1000/fps` and is
// `0` when no data is yet available. The implementation is header-only so
// no additional link dependency is needed beyond `re_utils`.

#include <chrono>
#include <deque>

namespace re::utils {

/// Standalone FPS counter with a `0.5s` sliding window.
///
/// The counter is window-free and GL-free. Call `tick()` once per frame
/// (or the deterministic overloads in tests) and query `fps()`/`ms()`.
/// The harness owns an instance and ticks it each frame — the counter does
/// not own the windowing loop (V5 T12 standalone discipline, the former
/// `app/FpsCounter` owned by `SampleHarness` is now `utils::FpsCounter`).
class FpsCounter {
   public:
    /// Sliding window length in seconds (analytic `0.5s` per T12 D).
    ///
    /// The window is exactly half a second so a steady 16.6ms cadence at 60.24Hz
    /// yields precisely 30 samples within the window (30 times 0.0166 equals
    /// 0.498 seconds) and fps equals 30 divided by 0.498 equals 60.24 within
    /// one per mille; this analytic coincidence makes the gate deterministic
    /// without wall-clock sleep and proves the standalone counter smooths the
    /// rate while still responding within half a second to a load change, not
    /// an exponential moving average that would never settle at the analytic
    /// target.
    static constexpr double kWindowSeconds = 0.5;

    FpsCounter() noexcept = default;

    /// Record a frame boundary using the current wall time.
    ///
    /// The first call establishes the baseline and produces `fps()==0` until
    /// a second tick arrives; thereafter `fps()` is the sliding average over
    /// the most recent `kWindowSeconds`.
    void tick() {
        const auto now = std::chrono::steady_clock::now();
        tick(now);
    }

    /// Record a frame boundary at the explicit `now` time point (deterministic
    /// overload for unit tests). The interval is `now - previous`, so a steady
    /// `16.6ms` cadence yields `fps()==60.24` within `1e-3` after the window
    /// fills (`N=30` samples, `30*0.0166==0.498s`, `30/0.498==60.24`, analytic
    /// `1/0.0166`, per T12 T).
    void tick(std::chrono::steady_clock::time_point now) {
        if (!hasLast_) {
            last_ = now;
            hasLast_ = true;
            return;
        }
        const double delta =
            std::chrono::duration<double>(now - last_).count();
        last_ = now;
        if (delta <= 0.0) {
            return;
        }
        pushDelta(delta);
    }

    /// Record a frame boundary with an explicit delta (deterministic overload
    /// for unit tests that prefer `tick(16.6ms)` phrasing). `delta` must be
    /// positive; a non-positive delta is ignored. The analytic gate
    /// `tick(16.6ms)` means `tick(duration<double>(0.0166))` yielding
    /// `fps()==60.24` within `1e-3` and `ms()==16.6` within `1e-3`.
    void tick(std::chrono::duration<double> delta) {
        const double seconds = delta.count();
        if (seconds <= 0.0) {
            return;
        }
        // Keep last_ coherent so a subsequent wall-clock tick() still has a
        // baseline: advance last_ by delta from its previous value, or
        // establish it now if this is the first tick.
        if (!hasLast_) {
            last_ = std::chrono::steady_clock::now();
            hasLast_ = true;
        } else {
            last_ += std::chrono::duration_cast<
                std::chrono::steady_clock::duration>(delta);
        }
        pushDelta(seconds);
    }

    /// Current frames per second as a sliding average over `kWindowSeconds`.
    ///
    /// Returns `0` before two ticks have been recorded. The analytic value for
    /// a `16.6ms` cadence is `fps == 1/0.0166 == 60.24096` within `1e-3`; with a
    /// full `0.5s` window (`N=30` deltas, `30*0.0166==0.498s`) the average is
    /// `30/0.498==60.24` within `1e-3` as well.
    [[nodiscard]] double fps() const noexcept {
        if (deltas_.empty() || sum_ <= 0.0) {
            return 0.0;
        }
        return static_cast<double>(deltas_.size()) / sum_;
    }

    /// Current frame time in milliseconds (`1000/fps`), or `0` when `fps()==0`.
    ///
    /// For the `16.6ms` gate the analytic is `ms == 16.6` within `1e-3` when
    /// `fps == 60.24` (since `1000/60.24 == 16.6005`).
    [[nodiscard]] double ms() const noexcept {
        const double f = fps();
        if (f <= 0.0) {
            return 0.0;
        }
        return 1000.0 / f;
    }

    /// Number of deltas currently retained in the window (test introspection).
    [[nodiscard]] std::size_t count() const noexcept {
        return deltas_.size();
    }

    /// Reset to the initial state (no samples, `fps()==0`).
    void reset() noexcept {
        deltas_.clear();
        sum_ = 0.0;
        hasLast_ = false;
    }

   private:
    void pushDelta(double deltaSeconds) {
        deltas_.push_back(deltaSeconds);
        sum_ += deltaSeconds;
        while (sum_ > kWindowSeconds && !deltas_.empty()) {
            sum_ -= deltas_.front();
            deltas_.pop_front();
        }
    }

    std::chrono::steady_clock::time_point last_{};
    bool hasLast_{false};
    std::deque<double> deltas_{};
    double sum_{0.0};
};

} // namespace re::utils
