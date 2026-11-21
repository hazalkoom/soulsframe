#include "bench_harness.h"

#include "gfx/fx/fsr1_math.h"
#include "tex/texture_pack.h"
#include "timing/fixed_step.h"
#include "timing/present_policy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

void bench_compute_texture_key(uint64_t iters) {
  const sf::tex::TextureDesc desc{
      .width = 512U, .height = 512U, .mip_levels = 9U, .array_size = 1U, .format = 71U};
  const std::vector<std::byte> pixels(512U * 512U, std::byte{0xA5});
  for (uint64_t i = 0; i < iters; ++i) {
    const uint64_t key = sf::tex::compute_texture_key(desc, pixels);
    sf::bench::do_not_optimize(key);
  }
}

void bench_parse_dds(uint64_t iters) {
  std::array<std::byte, 128U + 8U> dds{};
  const auto put_u32 = [&](size_t off, uint32_t value) {
    dds[off] = static_cast<std::byte>(value & 0xFFU);
    dds[off + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    dds[off + 2U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    dds[off + 3U] = static_cast<std::byte>((value >> 24U) & 0xFFU);
  };
  put_u32(0U, 0x20534444U);
  put_u32(4U, 124U);
  put_u32(12U, 4U);
  put_u32(16U, 4U);
  put_u32(28U, 1U);
  put_u32(76U, 32U);
  put_u32(80U, 0x4U);
  put_u32(84U, 0x31545844U);
  for (uint64_t i = 0; i < iters; ++i) {
    const auto info = sf::tex::parse_dds(dds);
    sf::bench::do_not_optimize(info.ok);
  }
}

void bench_evaluate_flip_upgrade(uint64_t iters) {
  sf::timing::SwapChainRequest req{
      .buffer_count = 1U,
      .sample_count = 1U,
      .windowed = true,
      .swap_effect = sf::timing::SwapEffectKind::BitBlt,
      .format = sf::timing::SwapFormatClass::FlipCompatible,
      .flags = sf::timing::kSwapChainFlagAllowTearing,
      .tearing_supported = true,
  };
  sf::Config cfg = sf::Config::defaults();
  cfg.modules.presentation = true;
  cfg.present.mode = "flip";
  for (uint64_t i = 0; i < iters; ++i) {
    const auto decision = sf::timing::evaluate_flip_upgrade(cfg, req);
    sf::bench::do_not_optimize(decision.upgrade);
  }
}

void bench_compute_easu_constants(uint64_t iters) {
  for (uint64_t i = 0; i < iters; ++i) {
    const auto constants =
        sf::gfx::fx::compute_easu_constants(1280U, 720U, 1920U, 1080U, 1920U, 1080U);
    sf::bench::do_not_optimize(constants.con0[0]);
  }
}

void bench_fixed_step_advance(uint64_t iters) {
  sf::timing::FixedStepStepper stepper;
  for (uint64_t i = 0; i < iters; ++i) {
    const auto step = stepper.advance(16.667);
    sf::bench::do_not_optimize(step.sim_ticks);
  }
}

SF_REGISTER_BENCH("tex::compute_texture_key", "texture", 100'000, 512U * 512U,
                  bench_compute_texture_key);
SF_REGISTER_BENCH("tex::parse_dds", "texture", 100'000, 128U + 8U, bench_parse_dds);
SF_REGISTER_BENCH("timing::evaluate_flip_upgrade", "present", 200'000, 0,
                  bench_evaluate_flip_upgrade);
SF_REGISTER_BENCH("gfx::compute_easu_constants", "fsr1", 200'000, 0, bench_compute_easu_constants);
SF_REGISTER_BENCH("FixedStepStepper::advance", "timing", 200'000, 0, bench_fixed_step_advance);

} // namespace