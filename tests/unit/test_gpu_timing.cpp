#include <doctest/doctest.h>

#include "timing/gpu_timing.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

TEST_CASE("gpu_ticks_to_ms converts ticks and rejects unusable measurements") {
  using sf::timing::gpu_ticks_to_ms;
  // 1 GHz counter: 1,000,000 ticks == 1 ms.
  CHECK(gpu_ticks_to_ms(0, 1'000'000, 1'000'000'000ULL, false) == doctest::Approx(1.0));
  CHECK(gpu_ticks_to_ms(500, 500, 1'000'000'000ULL, false) == doctest::Approx(0.0));
  // Disjoint, zero frequency, and reversed counters are all invalid (-1).
  CHECK(gpu_ticks_to_ms(0, 10, 1'000'000'000ULL, true) < 0.0);
  CHECK(gpu_ticks_to_ms(0, 10, 0, false) < 0.0);
  CHECK(gpu_ticks_to_ms(10, 0, 1'000'000'000ULL, false) < 0.0);
  // Extreme values must not overflow into NaN/negative.
  const double big = gpu_ticks_to_ms(0, std::numeric_limits<uint64_t>::max(), 1, false);
  CHECK(big > 0.0);
}

TEST_CASE("resolve_gpu_frame maps N+1 stamps to N pass segments") {
  const std::array<uint64_t, 4> stamps{0, 2'000'000, 5'000'000, 6'000'000};
  const auto t = sf::timing::resolve_gpu_frame(stamps, 1'000'000'000ULL, false);
  REQUIRE(t.valid);
  REQUIRE(t.segment_ms.size() == 3);
  CHECK(t.segment_ms[0] == doctest::Approx(2.0));
  CHECK(t.segment_ms[1] == doctest::Approx(3.0));
  CHECK(t.segment_ms[2] == doctest::Approx(1.0));
  CHECK(t.total_ms == doctest::Approx(6.0));
}

TEST_CASE("resolve_gpu_frame rejects short, disjoint, and non-monotonic input") {
  using sf::timing::resolve_gpu_frame;
  const std::array<uint64_t, 1> one{5};
  CHECK_FALSE(resolve_gpu_frame(one, 1'000'000'000ULL, false).valid);
  CHECK_FALSE(resolve_gpu_frame({}, 1'000'000'000ULL, false).valid);

  const std::array<uint64_t, 2> ok{1, 2};
  CHECK_FALSE(resolve_gpu_frame(ok, 1'000'000'000ULL, true).valid);
  CHECK_FALSE(resolve_gpu_frame(ok, 0, false).valid);

  const std::array<uint64_t, 3> backwards{10, 5, 20};
  const auto bad = resolve_gpu_frame(backwards, 1'000'000'000ULL, false);
  CHECK_FALSE(bad.valid);
  CHECK(bad.segment_ms.empty());
}

TEST_CASE("GpuSlotRing hands out slots FIFO and exhausts gracefully") {
  sf::timing::GpuSlotRing ring(2);
  CHECK(ring.capacity() == 2);
  CHECK_FALSE(ring.oldest_pending().has_value());

  const auto a = ring.begin_frame();
  const auto b = ring.begin_frame();
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  CHECK(*a != *b);
  CHECK_FALSE(ring.begin_frame().has_value()); // all slots busy

  ring.submit_frame(*a, 10);
  ring.submit_frame(*b, 11);
  CHECK(ring.pending_count() == 2);
  REQUIRE(ring.oldest_pending().has_value());
  CHECK(*ring.oldest_pending() == *a);
  CHECK(ring.frame_of(*a) == 10);

  ring.release(*a);
  CHECK(ring.pending_count() == 1);
  CHECK(*ring.oldest_pending() == *b);
  CHECK(ring.begin_frame().has_value()); // freed slot reusable
}

TEST_CASE("GpuSlotRing ignores invalid operations safely") {
  sf::timing::GpuSlotRing ring(0); // clamped to 1
  CHECK(ring.capacity() == 1);
  ring.submit_frame(0, 1);  // not recording -> ignored
  ring.submit_frame(99, 1); // out of range -> ignored
  ring.release(99);
  CHECK(ring.pending_count() == 0);
  CHECK(ring.frame_of(99) == 0);

  const auto s = ring.begin_frame();
  REQUIRE(s.has_value());
  ring.release(*s); // abandoning a recording slot is allowed
  CHECK(ring.begin_frame().has_value());
}

TEST_CASE("gpu_ticks_to_ms handles extreme frequencies and huge tick spans") {
  using sf::timing::gpu_ticks_to_ms;
  // 10 GHz counter: 10,000 ticks == 1 microsecond == 0.001 ms.
  CHECK(gpu_ticks_to_ms(0, 10'000, 10'000'000'000ULL, false) == doctest::Approx(0.001));
  // 10 GHz, 10 second span -> 10,000,000,000 ticks -> 1000 ms.
  CHECK(gpu_ticks_to_ms(0, 10'000'000'000ULL, 10'000'000'000ULL, false) == doctest::Approx(1000.0));
  // Huge tick span at 10 GHz: 0 -> UINT64_MAX must stay finite and positive, not NaN/inf.
  const double huge = gpu_ticks_to_ms(0, std::numeric_limits<uint64_t>::max(), 10'000'000'000ULL,
                                      false);
  CHECK(std::isfinite(huge));
  CHECK(huge > 0.0);
  CHECK(huge < 18446744073709551616.0); // finite (well below 2^64)
  // Near-maximum span must not overflow into NaN or negative values.
  const double near_max = gpu_ticks_to_ms(1000, std::numeric_limits<uint64_t>::max(), 10'000'000'000ULL,
                                        false);
  CHECK(std::isfinite(near_max));
  CHECK(near_max > 0.0);
}

TEST_CASE("GpuSlotRing survives rapid stress wrap-around") {
  sf::timing::GpuSlotRing ring(2);
  for (uint64_t iter = 0; iter < 10'000; ++iter) {
    // Phase 1: open two frames in flight.
    const auto s0 = ring.begin_frame();
    const auto s1 = ring.begin_frame();
    if (!s0.has_value() || !s1.has_value()) {
      break; // capacity allows two frames; a break here would be a bug we should catch below
    }
    const uint64_t f0 = 10'000 + iter;
    const uint64_t f1 = 20'000 + iter;
    ring.submit_frame(*s0, f0);
    CHECK(ring.pending_count() == 1);
    CHECK(ring.frame_of(*s0) == f0);
    CHECK(ring.oldest_pending().has_value());
    CHECK(*ring.oldest_pending() == *s0);

    ring.submit_frame(*s1, f1);
    CHECK(ring.pending_count() == 2);
    CHECK(ring.frame_of(*s1) == f1);

    // Phase 2: release both, verifying FIFO bookkeeping resets cleanly.
    ring.release(*s0);
    CHECK(ring.pending_count() == 1);
    CHECK(ring.oldest_pending().has_value());
    CHECK(*ring.oldest_pending() == *s1);
    ring.release(*s1);
    CHECK(ring.pending_count() == 0);
    CHECK(!ring.oldest_pending().has_value());
  }
  // Ring must still be usable after the stress loop.
  const auto final_slot = ring.begin_frame();
  CHECK(final_slot.has_value());
  ring.release(*final_slot);
}
