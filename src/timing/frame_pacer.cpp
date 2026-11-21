#include "timing/frame_pacer.h"

#include <algorithm>

namespace sf::timing {

namespace {

constexpr uint64_t kNsPerSecond = 1'000'000'000ULL;
constexpr uint64_t kNsPerMillisecond = 1'000'000ULL;

// Safety cap on spin iterations per frame so a broken/stalled mock clock cannot hang forever.
constexpr uint64_t kMaxSpinIterationsPerFrame = 5'000'000ULL;

} // namespace

FramePacer::FramePacer(uint32_t target_fps, uint64_t spin_margin_ns)
    : spin_margin_ns_(spin_margin_ns) {
  set_target_fps(target_fps);
}

void FramePacer::set_target_fps(uint32_t target_fps) {
  target_fps_ = std::min(target_fps, kMaxSupportedFps);
  if (target_fps_ == 0U) {
    target_frame_ns_ = 0ULL;
  } else {
    target_frame_ns_ = kNsPerSecond / static_cast<uint64_t>(target_fps_);
  }
  reset();
}

void FramePacer::reset() noexcept {
  has_deadline_ = false;
  next_deadline_ns_ = 0ULL;
}

uint64_t FramePacer::pace(IPacerClock& clock) {
  if (!enabled() || target_frame_ns_ == 0ULL) {
    return 0ULL;
  }

  const uint64_t entry_ns = clock.now_ns();
  if (!has_deadline_) {
    has_deadline_ = true;
    next_deadline_ns_ = entry_ns + target_frame_ns_;
    return 0ULL;
  }

  if (entry_ns >= next_deadline_ns_) {
    // Frame arrived late: avoid catch-up bursts by anchoring the next deadline to now.
    ++stats_.missed_deadlines;
    ++stats_.paced_frames;
    next_deadline_ns_ = entry_ns + target_frame_ns_;
    return 0ULL;
  }

  uint64_t now_ns = entry_ns;
  // Phase 1: coarse OS sleep while comfortably ahead of the spin margin.
  while (now_ns < next_deadline_ns_) {
    const uint64_t remaining_ns = next_deadline_ns_ - now_ns;
    if (remaining_ns <= spin_margin_ns_) {
      break;
    }
    const uint64_t sleep_budget_ns = remaining_ns - spin_margin_ns_;
    const auto sleep_ms =
        static_cast<uint32_t>(std::max<uint64_t>(1ULL, sleep_budget_ns / kNsPerMillisecond));
    clock.sleep_ms(sleep_ms);
    ++stats_.sleep_calls;
    const uint64_t after_sleep_ns = clock.now_ns();
    if (after_sleep_ns <= now_ns) {
      // Defensive exit if the clock did not advance.
      break;
    }
    now_ns = after_sleep_ns;
  }

  // Phase 2: fine-grained spin-wait until the exact deadline.
  uint64_t spins = 0ULL;
  while (now_ns < next_deadline_ns_ && spins < kMaxSpinIterationsPerFrame) {
    clock.yield_spin();
    ++spins;
    ++stats_.spin_iterations;
    now_ns = clock.now_ns();
  }

  const uint64_t waited_ns = (now_ns >= entry_ns) ? (now_ns - entry_ns) : 0ULL;
  ++stats_.paced_frames;
  stats_.total_wait_ns += waited_ns;
  stats_.max_wait_ns = std::max(stats_.max_wait_ns, waited_ns);

  // Advance deadline cleanly from the target cadence (or from `now_ns` if OS overslept past
  // an entire extra frame interval).
  const uint64_t stepped_deadline = next_deadline_ns_ + target_frame_ns_;
  next_deadline_ns_ = (now_ns >= stepped_deadline) ? (now_ns + target_frame_ns_) : stepped_deadline;
  return waited_ns;
}

} // namespace sf::timing
