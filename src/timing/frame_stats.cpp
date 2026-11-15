#include "timing/frame_stats.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sf::timing {

FrameStats::FrameStats(size_t window_capacity)
    : capacity_(window_capacity > 0 ? window_capacity : 1) {
  samples_.reserve(capacity_);
}

void FrameStats::record_frame_ms(double frame_ms) {
  if (!std::isfinite(frame_ms) || frame_ms < 0.0) {
    return;
  }

  if (!filled_) {
    samples_.push_back(frame_ms);
    if (samples_.size() == capacity_) {
      filled_ = true;
      write_idx_ = 0;
    }
  } else {
    samples_[write_idx_] = frame_ms;
    write_idx_ = (write_idx_ + 1) % capacity_;
  }
}

double FrameStats::tick(const IClock& clock) {
  const double now = clock.now_ms();
  if (!std::isfinite(now)) {
    return 0.0;
  }
  if (!has_last_tick_) {
    has_last_tick_ = true;
    last_tick_ms_ = now;
    return 0.0;
  }
  const double dt = now - last_tick_ms_;
  last_tick_ms_ = now;
  if (dt >= 0.0 && std::isfinite(dt)) {
    record_frame_ms(dt);
    return dt;
  }
  return 0.0;
}

FrameMetrics FrameStats::compute_metrics() const {
  FrameMetrics m;
  if (samples_.empty()) {
    return m;
  }

  m.sample_count = samples_.size();
  std::vector<double> sorted = samples_;
  std::sort(sorted.begin(), sorted.end());

  const double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
  m.avg_ms = sum / static_cast<double>(sorted.size());
  m.min_ms = sorted.front();
  m.max_ms = sorted.back();
  m.avg_fps = (m.avg_ms > 1e-9) ? (1000.0 / m.avg_ms) : 0.0;

  auto percentile = [&](double p) -> double {
    const size_t n = sorted.size();
    const double idx_d = p * static_cast<double>(n - 1);
    const auto idx = static_cast<size_t>(std::ceil(idx_d));
    return sorted[std::min(idx, n - 1)];
  };

  m.p1_low_ms = percentile(0.99);
  m.p0_1_low_ms = percentile(0.999);

  const double median = sorted[sorted.size() / 2];
  const double spike_threshold = 2.0 * median;
  for (double v : sorted) {
    if (median > 1e-9 && v > spike_threshold) {
      ++m.spike_count;
    }
  }

  return m;
}

void FrameStats::reset() {
  samples_.clear();
  write_idx_ = 0;
  filled_ = false;
  has_last_tick_ = false;
  last_tick_ms_ = 0.0;
}

} // namespace sf::timing
