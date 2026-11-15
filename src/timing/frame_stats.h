#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sf::timing {

/// Abstract millisecond clock interface allowing frame-timing code to be tested
/// deterministically on Linux using a fake clock.
struct IClock {
  virtual ~IClock() = default;

  /// Returns current monotonic timestamp in milliseconds.
  virtual double now_ms() const = 0;
};

/// Summary statistics computed over a rolling window of frame times.
struct FrameMetrics {
  uint64_t sample_count = 0;
  double avg_ms = 0.0;
  double min_ms = 0.0;
  double max_ms = 0.0;
  double avg_fps = 0.0;
  double p1_low_ms = 0.0;   ///< 99th percentile frame time (worst 1% frames).
  double p0_1_low_ms = 0.0; ///< 99.9th percentile frame time (worst 0.1% frames).
  uint32_t spike_count = 0; ///< Count of frames exceeding 2.0x the median frame time.
};

/// Fixed-capacity ring buffer that records frame deltas and calculates pacing metrics.
class FrameStats {
public:
  /// Constructs a rolling window holding up to `window_capacity` frame samples (>= 1).
  explicit FrameStats(size_t window_capacity = 1000);

  /// Records a frame duration in milliseconds (negative, NaN, and Inf inputs are ignored).
  void record_frame_ms(double frame_ms);

  /// Samples `clock`, stores delta since the prior tick, and returns that delta (0.0 on first
  /// call).
  double tick(const IClock& clock);

  /// Computes percentiles, averages, and spike counts over recorded samples.
  FrameMetrics compute_metrics() const;

  /// Clears all recorded samples and resets tick history.
  void reset();

  /// Returns the number of currently stored frame samples.
  size_t count() const { return samples_.size(); }

private:
  size_t capacity_;
  std::vector<double> samples_;
  size_t write_idx_ = 0;
  bool filled_ = false;
  bool has_last_tick_ = false;
  double last_tick_ms_ = 0.0;
};

} // namespace sf::timing
