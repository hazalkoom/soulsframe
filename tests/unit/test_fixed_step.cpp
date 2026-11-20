#include <doctest/doctest.h>

#include "timing/fixed_step.h"

TEST_CASE("FixedStepStepper decouples 120 FPS rendering cleanly into 60 Hz simulation ticks") {
  using namespace sf::timing;

  sf::Config::FpsUnlock cfg{};
  cfg.strategy = "fixed_step";
  cfg.target_fps = 120U;
  cfg.sim_tick_hz = 60U;
  cfg.max_substeps = 4U;

  FixedStepStepper stepper(cfg);
  const double frame_ms = 1000.0 / 120.0; // 8.3333 ms

  // Frame 1: 8.333 ms < 16.667 ms -> 0 sim ticks, interpolation_alpha ~ 0.5
  const auto r1 = stepper.advance(frame_ms);
  CHECK(r1.sim_ticks == 0U);
  CHECK_FALSE(r1.clamped_spiral);
  CHECK(r1.interpolation_alpha == doctest::Approx(0.5f).epsilon(0.01));
  CHECK(r1.sim_dt_sec == doctest::Approx(1.0f / 60.0f));
  CHECK(r1.camera_dt_sec == doctest::Approx(1.0f / 120.0f).epsilon(0.001));

  // Frame 2: accumulated 16.667 ms -> 1 sim tick, alpha ~ 0.0
  const auto r2 = stepper.advance(frame_ms);
  CHECK(r2.sim_ticks == 1U);
  CHECK_FALSE(r2.clamped_spiral);
  CHECK(r2.interpolation_alpha == doctest::Approx(0.0f).epsilon(0.01));

  for (int i = 0; i < 8; ++i) {
    stepper.advance(frame_ms);
  }
  // Over 10 frames at 120 FPS, simulation ran ~5 ticks (60 Hz).
  CHECK(stepper.stats().render_frames == 10U);
  CHECK(stepper.stats().total_sim_ticks == 5U);
  CHECK(stepper.stats().interpolated_frames == 5U);
}

TEST_CASE("FixedStepStepper clamps spiral-of-death hitches to max_substeps") {
  using namespace sf::timing;

  sf::Config::FpsUnlock cfg{};
  cfg.strategy = "fixed_step";
  cfg.sim_tick_hz = 60U;
  cfg.max_substeps = 4U;

  FixedStepStepper stepper(cfg);
  const auto hitch = stepper.advance(250.0); // 250 ms hitch (~15 ticks uncapped)
  CHECK(hitch.clamped_spiral);
  CHECK(hitch.sim_ticks == 4U);
  CHECK(stepper.stats().multi_tick_frames == 1U);
  CHECK(stepper.stats().spiral_clamps == 1U);

  // CameraOnly follows identical accumulator dynamics while exposing variable camera_dt_sec.
  cfg.strategy = "camera_only";
  stepper.configure(cfg);
  const auto cam = stepper.advance(8.333);
  CHECK(cam.sim_ticks == 0U);
  CHECK(cam.camera_dt_sec == doctest::Approx(0.008333f).epsilon(0.0005));

  // VariableDt ticks once per render frame with clamped camera_dt_sec.
  cfg.strategy = "variable_dt";
  stepper.configure(cfg);
  const auto var = stepper.advance(8.333);
  CHECK(var.sim_ticks == 1U);
  CHECK(var.sim_dt_sec == doctest::Approx(var.camera_dt_sec));
  CHECK(var.interpolation_alpha == doctest::Approx(1.0f));
}

TEST_CASE("evaluate_fps_unlock_safety enforces dry-run mode on unverified builds") {
  using namespace sf::timing;

  sf::Config cfg = sf::Config::defaults();
  sf::GameProfile profile{};
  profile.id = "ds3";

  // Disabled module.
  auto v = evaluate_fps_unlock_safety(profile, cfg);
  CHECK_FALSE(v.module_enabled);
  CHECK_FALSE(v.engine_patch_allowed);
  CHECK(v.dry_run_only);

  // Enabled module, unverified build.
  cfg.modules.fps_unlock = true;
  v = evaluate_fps_unlock_safety(profile, cfg);
  CHECK(v.module_enabled);
  CHECK_FALSE(v.engine_patch_allowed);
  CHECK(v.dry_run_only);

  // Verified build, but missing resolved pointers.
  profile.engine_hooks_allowed = true;
  v = evaluate_fps_unlock_safety(profile, cfg);
  CHECK(v.module_enabled);
  CHECK_FALSE(v.engine_patch_allowed);
  CHECK(v.dry_run_only);

  // Verified build with both frame_dt_ptr and frame_limiter resolved.
  profile.frame_dt_ptr = 0x140123456ULL;
  profile.frame_limiter = 0x140654321ULL;
  v = evaluate_fps_unlock_safety(profile, cfg);
  CHECK(v.module_enabled);
  CHECK(v.engine_patch_allowed);
  CHECK_FALSE(v.dry_run_only);
}
