#include <doctest/doctest.h>

#include "timing/present_policy.h"

TEST_CASE("evaluate_flip_upgrade respects module kill switch and present.mode") {
  sf::Config cfg = sf::Config::defaults();
  sf::timing::SwapChainRequest req{};
  req.buffer_count = 1U;
  req.sample_count = 1U;
  req.windowed = true;
  req.swap_effect = sf::timing::SwapEffectKind::BitBlt;
  req.format = sf::timing::SwapFormatClass::FlipCompatible;

  // Disabled by default (modules.presentation = false).
  auto d = sf::timing::evaluate_flip_upgrade(cfg, req);
  CHECK_FALSE(d.upgrade);
  CHECK(d.reason.find("disabled") != std::string::npos);

  // Enabled module, but present.mode = "game".
  cfg.modules.presentation = true;
  cfg.present.mode = "game";
  d = sf::timing::evaluate_flip_upgrade(cfg, req);
  CHECK_FALSE(d.upgrade);
  CHECK(d.reason.find("game") != std::string::npos);
}

TEST_CASE("evaluate_flip_upgrade upgrades compatible windowed BitBlt swap chains") {
  sf::Config cfg = sf::Config::defaults();
  cfg.modules.presentation = true;
  cfg.present.mode = "flip";

  sf::timing::SwapChainRequest req{};
  req.buffer_count = 1U;
  req.sample_count = 1U;
  req.windowed = true;
  req.swap_effect = sf::timing::SwapEffectKind::BitBlt;
  req.format = sf::timing::SwapFormatClass::FlipCompatible;
  req.tearing_supported = false;

  const auto no_tear = sf::timing::evaluate_flip_upgrade(cfg, req);
  CHECK(no_tear.upgrade);
  CHECK(no_tear.new_buffer_count == 2U);
  CHECK_FALSE(no_tear.tearing_enabled);
  CHECK((no_tear.new_flags & sf::timing::kSwapChainFlagAllowTearing) == 0U);

  req.buffer_count = 3U;
  req.tearing_supported = true;
  const auto with_tear = sf::timing::evaluate_flip_upgrade(cfg, req);
  CHECK(with_tear.upgrade);
  CHECK(with_tear.new_buffer_count == 3U);
  CHECK(with_tear.tearing_enabled);
  CHECK((with_tear.new_flags & sf::timing::kSwapChainFlagAllowTearing) != 0U);
}

TEST_CASE("evaluate_flip_upgrade safely skips MSAA, fullscreen, unsupported format, and flip") {
  sf::Config cfg = sf::Config::defaults();
  cfg.modules.presentation = true;
  cfg.present.mode = "flip";

  sf::timing::SwapChainRequest base{};
  base.buffer_count = 2U;
  base.sample_count = 1U;
  base.windowed = true;
  base.swap_effect = sf::timing::SwapEffectKind::BitBlt;
  base.format = sf::timing::SwapFormatClass::FlipCompatible;

  // MSAA swapchain: DXGI forbids flip-model with SampleDesc.Count > 1.
  auto msaa = base;
  msaa.sample_count = 4U;
  const auto d_msaa = sf::timing::evaluate_flip_upgrade(cfg, msaa);
  CHECK_FALSE(d_msaa.upgrade);
  CHECK(d_msaa.reason.find("MSAA") != std::string::npos);

  // Exclusive fullscreen.
  auto fs = base;
  fs.windowed = false;
  const auto d_fs = sf::timing::evaluate_flip_upgrade(cfg, fs);
  CHECK_FALSE(d_fs.upgrade);
  CHECK(d_fs.reason.find("fullscreen") != std::string::npos);

  // Unsupported format.
  auto bad_fmt = base;
  bad_fmt.format = sf::timing::SwapFormatClass::Unsupported;
  const auto d_fmt = sf::timing::evaluate_flip_upgrade(cfg, bad_fmt);
  CHECK_FALSE(d_fmt.upgrade);
  CHECK(d_fmt.reason.find("format") != std::string::npos);

  // Already flip-model (preserving existing tearing flag detection).
  auto already = base;
  already.swap_effect = sf::timing::SwapEffectKind::FlipModel;
  already.flags = sf::timing::kSwapChainFlagAllowTearing;
  const auto d_already = sf::timing::evaluate_flip_upgrade(cfg, already);
  CHECK_FALSE(d_already.upgrade);
  CHECK(d_already.tearing_enabled);
}

TEST_CASE("resolve_present_call adds ALLOW_TEARING only when safe") {
  using namespace sf::timing;

  // Safe case: flip active, tearing enabled, windowed, sync_interval == 0.
  const auto on = resolve_present_call(0U, 0U, true, true, true);
  CHECK(on.sync_interval == 0U);
  CHECK((on.flags & kPresentFlagAllowTearing) != 0U);

  // VSync enabled (sync_interval == 1): ALLOW_TEARING is illegal in DXGI.
  const auto vsync = resolve_present_call(1U, 0U, true, true, true);
  CHECK((vsync.flags & kPresentFlagAllowTearing) == 0U);

  // DXGI_PRESENT_TEST probe: must not add ALLOW_TEARING.
  const auto test_flag = resolve_present_call(0U, kPresentFlagTest, true, true, true);
  CHECK((test_flag.flags & kPresentFlagAllowTearing) == 0U);
  CHECK((test_flag.flags & kPresentFlagTest) != 0U);

  // Exclusive fullscreen or tearing unsupported or non-flip: must not add ALLOW_TEARING.
  CHECK((resolve_present_call(0U, 0U, true, true, false).flags & kPresentFlagAllowTearing) == 0U);
  CHECK((resolve_present_call(0U, 0U, true, false, true).flags & kPresentFlagAllowTearing) == 0U);
  CHECK((resolve_present_call(0U, 0U, false, true, true).flags & kPresentFlagAllowTearing) == 0U);
}
