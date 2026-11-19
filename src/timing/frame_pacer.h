#pragma once

#include <cstdint>

namespace sf::timing {

/// Abstract timing and wait backend injected into `FramePacer` so pacing math
/// can be verified deterministically on native Linux without real sleeps.
struct IPacerClock {
  virtual ~IPacerClock() = default;

  /// Returns a monotonic timestamp in nanoseconds.
  virtual uint64_t now_ns() const = 0;

  /// Sleeps for approximately `ms` milliseconds (coarse OS wait).
  virtual void sleep_ms(uint32_t ms) = 0;

  /// Yields the current thread/CPU slice during the final spin-wait window.
  virtual void yield_spin() = 0;
};

/// Cumulative pacing diagnostics reported on shutdown or periodic telemetry logs.
struct PacerStats {
  /// Number of frames actively paced by `FramePacer::pace()`.
  uint64_t paced_frames = 0;

  /// Number of frames that arrived after their target deadline (slow frame / hitch).
  uint64_t missed_deadlines = 0;

  /// Total nanoseconds waited (sleep + spin) across all paced frames.
  uint64_t total_wait_ns = 0;

  /// Longest single-frame wait observed, in nanoseconds.
  uint64_t max_wait_ns = 0;

  /// Total coarse OS sleep calls issued.
  uint64_t sleep_calls = 0;

  /// Total spin-wait yield iterations executed.
  uint64_t spin_iterations = 0;
};

/// Hybrid sleep + spin frame-rate limiter targeting a steady presentation cadence.
///
/// Pure C++20: zero Win32 or D3D11 dependencies. Sleeps coarsely while more than
/// `spin_margin_ns` remain before the deadline, then spins for sub-millisecond
/// accuracy. When a frame arrives late (past its deadline), the deadline resets
/// to `now + target_frame_ns` to avoid "catch-up bursts".
class FramePacer {
public:
  /// Default spin-wait margin before the frame deadline (1.5 ms).
  static constexpr uint64_t kDefaultSpinMarginNs = 1'500'000ULL;

  /// Maximum supported FPS cap (matches `Config::Present::max_fps` clamping).
  static constexpr uint32_t kMaxSupportedFps = 360U;

  /// Constructs a pacer with an initial target FPS (`0` = disabled) and spin margin.
  explicit FramePacer(uint32_t target_fps = 0U, uint64_t spin_margin_ns = kDefaultSpinMarginNs);

  /// Updates the target FPS (`0` disables pacing; values > 360 are clamped) and resets cadence.
  void set_target_fps(uint32_t target_fps);

  /// Returns the currently configured target FPS (`0` when disabled).
  uint32_t target_fps() const noexcept { return target_fps_; }

  /// Returns the target frame interval in nanoseconds (`0` when disabled).
  uint64_t target_frame_ns() const noexcept { return target_frame_ns_; }

  /// Returns true when active frame pacing is enabled (`target_fps > 0`).
  bool enabled() const noexcept { return target_fps_ > 0U; }

  /// Clears deadline tracking (e.g. after a swap-chain resize or long stall).
  void reset() noexcept;

  /// Waits until the next frame deadline using `clock` and returns total nanoseconds waited.
  /// Returns `0` immediately when pacing is disabled or on the very first calibration frame.
  uint64_t pace(IPacerClock& clock);

  /// Returns cumulative pacing statistics.
  const PacerStats& stats() const noexcept { return stats_; }

private:
  uint32_t target_fps_ = 0U;
  uint64_t target_frame_ns_ = 0ULL;
  uint64_t spin_margin_ns_ = kDefaultSpinMarginNs;
  bool has_deadline_ = false;
  uint64_t next_deadline_ns_ = 0ULL;
  PacerStats stats_{};
};

} // namespace sf::timing
