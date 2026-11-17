#include "bench_harness.h"

#include "probe/jsonl_writer.h"
#include "probe/trace_types.h"
#include "timing/frame_stats.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace {

void bench_frame_stats_record(uint64_t iters) {
  sf::timing::FrameStats stats(1000);
  // Pre-fill ring so measurement reflects steady-state circular overwrites.
  for (uint64_t i = 0; i < 1000; ++i) {
    stats.record_frame_ms(16.667);
  }
  for (uint64_t i = 0; i < iters; ++i) {
    const double sample = 15.5 + static_cast<double>(i & 7U) * 0.25;
    stats.record_frame_ms(sample);
  }
  sf::bench::do_not_optimize(stats.count());
}

void bench_frame_stats_metrics(uint64_t iters) {
  sf::timing::FrameStats stats(1000);
  for (uint64_t i = 0; i < 1000; ++i) {
    stats.record_frame_ms(14.0 + static_cast<double>(i % 15U) * 0.5);
  }
  for (uint64_t i = 0; i < iters; ++i) {
    const auto m = stats.compute_metrics();
    sf::bench::do_not_optimize(m.p1_low_ms);
  }
}

void bench_trace_record_draw(uint64_t iters) {
  sf::probe::TraceRecorder rec;
  rec.begin_frame(1);
  const sf::probe::DrawCallState state{
      .api = sf::probe::DrawApi::DrawIndexed,
      .vertex_or_index_count = 3600,
      .instance_count = 1,
      .caller_rva = 0x123456ULL,
      .ps_hash = 0xabcdef0123456789ULL,
      .vs_hash = 0x9876543210fedcbaULL,
      .rtv_signature = 0x11223344ULL,
      .rt_width = 1920,
      .rt_height = 1080,
      .rtv_count = 1,
      .blend_enabled = true,
      .depth_write = false,
  };
  // Warm call-site entry so loop exercises steady-state hash lookup.
  rec.record_draw(state);
  for (uint64_t i = 0; i < iters; ++i) {
    rec.record_draw(state);
  }
  sf::bench::clobber_memory();
}

void bench_trace_full_frame_1k_draws(uint64_t iters) {
  sf::probe::TraceRecorder rec;
  constexpr uint32_t kDrawsPerFrame = 1000;
  constexpr uint32_t kPasses = 8;
  constexpr uint32_t kSites = 40;

  for (uint64_t f = 0; f < iters; ++f) {
    rec.begin_frame(f + 1);
    for (uint32_t d = 0; d < kDrawsPerFrame; ++d) {
      const uint32_t pass_idx = (d * kPasses) / kDrawsPerFrame;
      const uint32_t site_idx = d % kSites;
      const sf::probe::DrawCallState state{
          .api = (pass_idx == 7) ? sf::probe::DrawApi::DrawIndexedInstanced
                                 : sf::probe::DrawApi::DrawIndexed,
          .vertex_or_index_count = 1200 + site_idx * 10U,
          .instance_count = (pass_idx == 4) ? 8U : 1U,
          .caller_rva = 0x100000ULL + static_cast<uint64_t>(site_idx) * 0x40ULL,
          .ps_hash = 0xdead0000ULL + static_cast<uint64_t>(site_idx),
          .vs_hash = 0xbeef0000ULL + static_cast<uint64_t>(pass_idx),
          .rtv_signature = 0xabc000ULL + pass_idx,
          .rt_width = 1920,
          .rt_height = 1080,
          .rtv_count = static_cast<uint8_t>(pass_idx == 1 ? 3U : 1U),
          .blend_enabled = (pass_idx == 4),
          .depth_write = (pass_idx < 4),
      };
      rec.record_draw(state);
    }
    const auto snap = rec.end_frame();
    sf::bench::do_not_optimize(snap.total_draw_calls);
  }
}

void bench_trace_record_resource_hit(uint64_t iters) {
  sf::probe::TraceRecorder rec;
  const sf::probe::ResourceSummary res{
      .kind = "Texture2D",
      .id_hash = 0x123456789abcdef0ULL,
      .width = 1920,
      .height = 1080,
      .format_or_stride = 28,
      .bind_flags = 40,
      .mip_levels = 1,
      .sample_count = 1,
      .is_render_target = true,
  };
  (void)rec.record_resource(res);
  for (uint64_t i = 0; i < iters; ++i) {
    const bool inserted = rec.record_resource(res);
    sf::bench::do_not_optimize(inserted);
  }
}

void bench_jsonl_escape(uint64_t iters) {
  constexpr std::string_view kSample =
      R"(pass="particles_alpha"\nshader="sfx_fire_additive.ps"\tpath="C:\DS3\Game\sf.toml")";
  for (uint64_t i = 0; i < iters; ++i) {
    const auto out = sf::probe::json_escape(kSample);
    sf::bench::do_not_optimize(out.size());
  }
}

void bench_jsonl_serialize_snapshot(uint64_t iters) {
  sf::probe::TraceRecorder rec;
  rec.begin_frame(42);
  for (uint32_t s = 0; s < 40; ++s) {
    const sf::probe::DrawCallState st{
        .api = sf::probe::DrawApi::DrawIndexed,
        .vertex_or_index_count = 2400,
        .instance_count = 1,
        .caller_rva = 0x400000ULL + s * 0x20ULL,
        .ps_hash = 0xfeed0000ULL + s,
        .vs_hash = 0xcafe0000ULL,
        .rtv_signature = 0x7700ULL + (s / 5U),
        .rt_width = 1920,
        .rt_height = 1080,
        .rtv_count = 1,
        .blend_enabled = (s >= 20),
        .depth_write = (s < 20),
    };
    rec.record_draw(st);
  }
  const auto snap = rec.end_frame();

  for (uint64_t i = 0; i < iters; ++i) {
    const auto lines = sf::probe::serialize_frame_snapshot(snap, 16.42);
    sf::bench::do_not_optimize(lines.size());
  }
}

SF_REGISTER_BENCH("FrameStats::record_frame_ms", "timing", 1'000'000, 0, bench_frame_stats_record);
SF_REGISTER_BENCH("FrameStats::compute_metrics", "timing", 10'000, 0, bench_frame_stats_metrics);
SF_REGISTER_BENCH("TraceRecorder::record_draw", "probe", 200'000, 0, bench_trace_record_draw);
SF_REGISTER_BENCH("TraceRecorder::frame_1k_draws", "probe", 300, 0,
                  bench_trace_full_frame_1k_draws);
SF_REGISTER_BENCH("TraceRecorder::resource_hit", "probe", 500'000, 0,
                  bench_trace_record_resource_hit);
SF_REGISTER_BENCH("probe::json_escape (83B)", "jsonl", 200'000, 83, bench_jsonl_escape);
SF_REGISTER_BENCH("probe::serialize_snapshot(40)", "jsonl", 5'000, 0,
                  bench_jsonl_serialize_snapshot);

} // namespace
