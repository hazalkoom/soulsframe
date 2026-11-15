#include <doctest/doctest.h>

#include "timing/frame_stats.h"

#include <limits>

namespace {

struct FakeClock final : public sf::timing::IClock {
  double now = 0.0;
  double now_ms() const override { return now; }
};

} // namespace

TEST_CASE("frame_stats computes avg, lows, and spikes with fake clock") {
  FakeClock clk;
  sf::timing::FrameStats stats(100);

  CHECK(stats.tick(clk) == doctest::Approx(0.0));

  for (int i = 0; i < 98; ++i) {
    clk.now += 10.0;
    CHECK(stats.tick(clk) == doctest::Approx(10.0));
  }
  clk.now += 40.0; // spike (> 2 * 10)
  stats.tick(clk);
  clk.now += 50.0; // spike (> 2 * 10)
  stats.tick(clk);

  const auto m = stats.compute_metrics();
  CHECK(m.sample_count == 100);
  CHECK(m.min_ms == doctest::Approx(10.0));
  CHECK(m.max_ms == doctest::Approx(50.0));
  CHECK(m.avg_ms == doctest::Approx(10.7));
  CHECK(m.p1_low_ms >= 40.0);
  CHECK(m.p0_1_low_ms == doctest::Approx(50.0));
  CHECK(m.spike_count == 2);
}

TEST_CASE("frame_stats handles empty, zero, negative and NaN safely") {
  sf::timing::FrameStats stats(10);
  const auto empty = stats.compute_metrics();
  CHECK(empty.sample_count == 0);
  CHECK(empty.avg_fps == doctest::Approx(0.0));

  stats.record_frame_ms(-5.0);
  stats.record_frame_ms(std::numeric_limits<double>::quiet_NaN());
  stats.record_frame_ms(std::numeric_limits<double>::infinity());
  CHECK(stats.count() == 0);

  stats.record_frame_ms(0.0);
  const auto z = stats.compute_metrics();
  CHECK(z.sample_count == 1);
  CHECK(z.avg_fps == doctest::Approx(0.0));
}
