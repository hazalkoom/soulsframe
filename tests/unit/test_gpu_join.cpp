#include <doctest/doctest.h>

#include "probe/gpu_join.h"

#include <cstdint>

namespace {

sf::probe::FrameTraceSnapshot make_snap(uint64_t idx, size_t pass_count) {
  sf::probe::FrameTraceSnapshot s;
  s.frame_index = idx;
  s.passes.resize(pass_count);
  return s;
}

sf::timing::GpuFrameTiming make_timing(std::vector<double> segments) {
  sf::timing::GpuFrameTiming t;
  t.valid = true;
  t.segment_ms = std::move(segments);
  for (const double v : t.segment_ms) {
    t.total_ms += v;
  }
  return t;
}

} // namespace

TEST_CASE("joiner holds a frame until its GPU result arrives, then releases in order") {
  sf::probe::GpuFrameJoiner j(4);
  j.push(make_snap(1, 2), 16.0);
  CHECK(j.take_ready().empty()); // still waiting for the GPU
  CHECK(j.held_count() == 1);

  j.on_gpu_result(1, make_timing({2.0, 3.0}));
  const auto ready = j.take_ready();
  REQUIRE(ready.size() == 1);
  CHECK(ready[0].cpu_frame_ms == doctest::Approx(16.0));
  CHECK(ready[0].snap.gpu_frame_ms == doctest::Approx(5.0));
  CHECK(ready[0].snap.passes[0].gpu_ms == doctest::Approx(2.0));
  CHECK(ready[0].snap.passes[1].gpu_ms == doctest::Approx(3.0));
  CHECK(j.held_count() == 0);
}

TEST_CASE("joiner never reorders frames: an unresolved head blocks resolved followers") {
  sf::probe::GpuFrameJoiner j(8);
  j.push(make_snap(1, 1), 10.0);
  j.push(make_snap(2, 1), 11.0);
  j.on_gpu_result(2, make_timing({1.0})); // newer frame resolves first
  CHECK(j.take_ready().empty());          // frame 1 still blocks the queue

  j.on_gpu_result(1, make_timing({4.0}));
  const auto ready = j.take_ready();
  REQUIRE(ready.size() == 2);
  CHECK(ready[0].snap.frame_index == 1);
  CHECK(ready[1].snap.frame_index == 2);
}

TEST_CASE("joiner times out frames whose GPU result never arrives") {
  sf::probe::GpuFrameJoiner j(2);
  j.push(make_snap(1, 1), 10.0);
  j.push(make_snap(2, 1), 10.0);
  CHECK(j.take_ready().empty()); // 2 held == limit, not yet overdue
  j.push(make_snap(3, 1), 10.0); // 3 held > limit -> frame 1 overdue
  const auto ready = j.take_ready();
  REQUIRE(ready.size() == 1);
  CHECK(ready[0].snap.frame_index == 1);
  CHECK(ready[0].snap.gpu_frame_ms < 0.0); // emitted without GPU data
}

TEST_CASE("joiner flush_all drains everything, resolved or not") {
  sf::probe::GpuFrameJoiner j(8);
  j.push(make_snap(1, 1), 1.0);
  j.push(make_snap(2, 1), 2.0);
  j.on_gpu_result(2, make_timing({0.5}));
  const auto ready = j.take_ready(true);
  REQUIRE(ready.size() == 2);
  CHECK(ready[0].snap.gpu_frame_ms < 0.0);
  CHECK(ready[1].snap.gpu_frame_ms == doctest::Approx(0.5));
  CHECK(j.held_count() == 0);
}

TEST_CASE("joiner ignores unknown frames, invalid timings, and mismatched pass counts") {
  sf::probe::GpuFrameJoiner j(4);
  j.push(make_snap(5, 3), 1.0);

  j.on_gpu_result(99, make_timing({1.0})); // unknown frame
  sf::timing::GpuFrameTiming invalid;      // valid == false
  j.on_gpu_result(5, invalid);
  CHECK(j.take_ready().empty()); // still unresolved

  // Segment count (2) != pass count (3): keep only the frame total, never mislabel passes.
  j.on_gpu_result(5, make_timing({1.0, 2.0}));
  const auto ready = j.take_ready();
  REQUIRE(ready.size() == 1);
  CHECK(ready[0].snap.gpu_frame_ms == doctest::Approx(3.0));
  for (const auto& p : ready[0].snap.passes) {
    CHECK(p.gpu_ms < 0.0);
  }

  sf::probe::GpuFrameJoiner clamped(0); // clamped to 1
  clamped.push(make_snap(1, 0), 1.0);
  clamped.push(make_snap(2, 0), 1.0);
  CHECK(clamped.take_ready().size() == 1);
}
