#include <doctest/doctest.h>

#include "timing/frame_pacer.h"

#include <cstdint>

namespace {

/// Deterministic virtual clock for testing `FramePacer` without wall-clock sleeps.
class MockPacerClock final : public sf::timing::IPacerClock {
public:
  explicit MockPacerClock(uint64_t start_ns = 1'000'000'000ULL, uint64_t spin_step_ns = 100'000ULL)
      : now_(start_ns), spin_step_ns_(spin_step_ns) {}

  uint64_t now_ns() const override { return now_; }

  void sleep_ms(uint32_t ms) override {
    if (!stall_sleep_) {
      now_ += static_cast<uint64_t>(ms) * 1'000'000ULL + sleep_overshoot_ns_;
    }
    ++sleep_calls_;
  }

  void yield_spin() override {
    now_ += spin_step_ns_;
    ++spin_calls_;
  }

  void advance_ns(uint64_t delta_ns) { now_ += delta_ns; }
  void set_sleep_overshoot_ns(uint64_t overshoot_ns) { sleep_overshoot_ns_ = overshoot_ns; }
  void set_stall_sleep(bool stall) { stall_sleep_ = stall; }
  uint64_t sleep_calls() const { return sleep_calls_; }
  uint64_t spin_calls() const { return spin_calls_; }

private:
  uint64_t now_ = 0ULL;
  uint64_t spin_step_ns_ = 100'000ULL;
  uint64_t sleep_overshoot_ns_ = 0ULL;
  bool stall_sleep_ = false;
  uint64_t sleep_calls_ = 0ULL;
  uint64_t spin_calls_ = 0ULL;
};

} // namespace

TEST_CASE("FramePacer disabled when target_fps is 0 and clamps above 360") {
  sf::timing::FramePacer pacer(0U);
  CHECK_FALSE(pacer.enabled());
  CHECK(pacer.target_fps() == 0U);
  CHECK(pacer.target_frame_ns() == 0ULL);

  MockPacerClock clk;
  CHECK(pacer.pace(clk) == 0ULL);
  CHECK(pacer.stats().paced_frames == 0ULL);

  pacer.set_target_fps(999U);
  CHECK(pacer.enabled());
  CHECK(pacer.target_fps() == sf::timing::FramePacer::kMaxSupportedFps);
  CHECK(pacer.target_frame_ns() == 1'000'000'000ULL / 360ULL);
}

TEST_CASE("FramePacer uses hybrid sleep+spin to pace 60 FPS accurately") {
  sf::timing::FramePacer pacer(60U);
  REQUIRE(pacer.enabled());
  const uint64_t target_ns = pacer.target_frame_ns(); // 16,666,666 ns

  MockPacerClock clk(10'000'000'000ULL, 100'000ULL);
  const uint64_t start_ns = clk.now_ns();
  // First frame calibrates the deadline and returns 0.
  CHECK(pacer.pace(clk) == 0ULL);

  // Simulate 5 fast frames taking 4 ms of work each.
  for (int i = 0; i < 5; ++i) {
    clk.advance_ns(4'000'000ULL);
    const uint64_t waited = pacer.pace(clk);
    // Cadence-preserving deadline stepping may shorten a wait by up to one spin step
    // to compensate for the previous frame's tiny overshoot.
    CHECK(waited >= target_ns - 4'000'000ULL - 100'000ULL);
    CHECK(waited <= target_ns - 4'000'000ULL + 100'000ULL);
  }

  // Total elapsed time across 5 frames stays locked to 5 * target_ns within one spin step.
  const uint64_t total_elapsed = clk.now_ns() - start_ns;
  CHECK(total_elapsed >= 5ULL * target_ns);
  CHECK(total_elapsed <= 5ULL * target_ns + 100'000ULL);

  CHECK(pacer.stats().paced_frames == 5ULL);
  CHECK(pacer.stats().missed_deadlines == 0ULL);
  CHECK(pacer.stats().sleep_calls >= 5ULL);
  CHECK(pacer.stats().spin_iterations > 0ULL);
  CHECK(pacer.stats().max_wait_ns >= 12'000'000ULL);
}

TEST_CASE("FramePacer handles missed deadlines without catch-up bursts") {
  sf::timing::FramePacer pacer(60U);
  MockPacerClock clk(1'000'000'000ULL, 100'000ULL);

  CHECK(pacer.pace(clk) == 0ULL);

  // Hitch frame: takes 40 ms (> 16.67 ms target).
  clk.advance_ns(40'000'000ULL);
  CHECK(pacer.pace(clk) == 0ULL);
  CHECK(pacer.stats().missed_deadlines == 1ULL);
  CHECK(pacer.stats().paced_frames == 1ULL);

  // Next frame is fast (2 ms): must still be paced to ~14.67 ms, NOT burst immediately.
  clk.advance_ns(2'000'000ULL);
  const uint64_t waited = pacer.pace(clk);
  CHECK(waited >= 14'500'000ULL);
  CHECK(pacer.stats().missed_deadlines == 1ULL);
  CHECK(pacer.stats().paced_frames == 2ULL);
}

TEST_CASE("FramePacer handles OS sleep overshoot and stalled sleep clocks safely") {
  sf::timing::FramePacer pacer(60U);
  MockPacerClock clk(5'000'000'000ULL, 100'000ULL);
  clk.set_sleep_overshoot_ns(25'000'000ULL); // OS oversleeps past next frame

  CHECK(pacer.pace(clk) == 0ULL);
  clk.advance_ns(1'000'000ULL);
  const uint64_t waited = pacer.pace(clk);
  CHECK(waited >= 25'000'000ULL);

  // Subsequent frame after reset must recalibrate cleanly.
  pacer.reset();
  CHECK(pacer.pace(clk) == 0ULL);

  // Stalled sleep clock must fall through to spin without hanging.
  MockPacerClock stall_clk(2'000'000'000ULL, 500'000ULL);
  stall_clk.set_stall_sleep(true);
  sf::timing::FramePacer pacer2(120U);
  CHECK(pacer2.pace(stall_clk) == 0ULL);
  stall_clk.advance_ns(1'000'000ULL);
  const uint64_t spin_waited = pacer2.pace(stall_clk);
  CHECK(spin_waited >= 7'000'000ULL);
  CHECK(stall_clk.sleep_calls() == 1ULL);
  CHECK(stall_clk.spin_calls() > 0ULL);
}
