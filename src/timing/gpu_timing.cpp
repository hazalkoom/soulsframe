#include "timing/gpu_timing.h"

#include <algorithm>
#include <iterator>

namespace sf::timing {

double gpu_ticks_to_ms(uint64_t begin_ticks, uint64_t end_ticks, uint64_t frequency_hz,
                       bool disjoint) noexcept {
  if (disjoint || frequency_hz == 0 || end_ticks < begin_ticks) {
    return -1.0;
  }
  const auto delta = static_cast<double>(end_ticks - begin_ticks);
  return delta * 1000.0 / static_cast<double>(frequency_hz);
}

GpuFrameTiming resolve_gpu_frame(std::span<const uint64_t> stamps, uint64_t frequency_hz,
                                 bool disjoint) {
  GpuFrameTiming out;
  if (stamps.size() < 2 || disjoint || frequency_hz == 0) {
    return out;
  }
  out.segment_ms.reserve(stamps.size() - 1);
  for (size_t i = 0; i + 1 < stamps.size(); ++i) {
    const double ms = gpu_ticks_to_ms(stamps[i], stamps[i + 1], frequency_hz, false);
    if (ms < 0.0) {
      out.segment_ms.clear();
      return out;
    }
    out.segment_ms.push_back(ms);
  }
  out.total_ms = gpu_ticks_to_ms(stamps.front(), stamps.back(), frequency_hz, false);
  out.valid = true;
  return out;
}

GpuSlotRing::GpuSlotRing(size_t capacity)
    : states_(std::max<size_t>(capacity, 1), State::Free), frames_(states_.size(), 0) {}

std::optional<size_t> GpuSlotRing::begin_frame() {
  const auto it = std::ranges::find(states_, State::Free);
  if (it == states_.end()) {
    return std::nullopt;
  }
  *it = State::Recording;
  return static_cast<size_t>(std::distance(states_.begin(), it));
}

void GpuSlotRing::submit_frame(size_t slot, uint64_t frame_index) {
  if (slot >= states_.size() || states_[slot] != State::Recording) {
    return;
  }
  states_[slot] = State::Pending;
  frames_[slot] = frame_index;
  pending_.push_back(slot);
}

std::optional<size_t> GpuSlotRing::oldest_pending() const {
  if (pending_.empty()) {
    return std::nullopt;
  }
  return pending_.front();
}

uint64_t GpuSlotRing::frame_of(size_t slot) const {
  return slot < frames_.size() ? frames_[slot] : 0;
}

void GpuSlotRing::release(size_t slot) {
  if (slot >= states_.size()) {
    return;
  }
  states_[slot] = State::Free;
  std::erase(pending_, slot);
}

} // namespace sf::timing
