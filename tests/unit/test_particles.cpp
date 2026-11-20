#include <doctest/doctest.h>

#include "games/ds3/render.h"

TEST_CASE("is_particle_draw accurately separates DS3 particle passes from opaque/shadow/UI") {
  using namespace sf::games::ds3;
  const sf::RenderProfile profile{};

  ParticleDrawCandidate particle{};
  particle.rtv_count = 1U;
  particle.rt_width = 1920U;
  particle.rt_height = 1080U;
  particle.blend_enabled = true;
  particle.depth_enabled = true;
  particle.depth_write = false;
  particle.has_vertex_shader = true;
  particle.has_pixel_shader = true;
  CHECK(is_particle_draw(profile, particle));

  // Opaque draw (no blend, writes depth) -> not particle.
  auto opaque = particle;
  opaque.blend_enabled = false;
  opaque.depth_write = true;
  CHECK_FALSE(is_particle_draw(profile, opaque));

  // G-buffer MRT draw (>= 2 RTVs) -> not particle.
  auto mrt = particle;
  mrt.rtv_count = 3U;
  CHECK_FALSE(is_particle_draw(profile, mrt));

  // Shadow pass (0 color RTVs) -> not particle.
  auto shadow = particle;
  shadow.rtv_count = 0U;
  CHECK_FALSE(is_particle_draw(profile, shadow));

  // Small UI icon / sprite target -> excluded by min_rt_dimension.
  auto tiny = particle;
  tiny.rt_width = 64U;
  tiny.rt_height = 64U;
  CHECK_FALSE(is_particle_draw(profile, tiny));

  // Depth-writing transparent draw -> excluded when require_depth_read_only = true.
  auto depth_write_blend = particle;
  depth_write_blend.depth_write = true;
  CHECK_FALSE(is_particle_draw(profile, depth_write_blend));
}

TEST_CASE("compute_low_res_viewport computes scaled geometry and pixel savings") {
  using namespace sf::games::ds3;

  const auto half = compute_low_res_viewport(1920U, 1080U, 50U);
  CHECK(half.scaled_width == 960U);
  CHECK(half.scaled_height == 540U);
  CHECK(half.full_pixels == 1920ULL * 1080ULL);
  CHECK(half.scaled_pixels == 960ULL * 540ULL);
  CHECK(half.pixel_savings_ratio == doctest::Approx(0.75).epsilon(0.001));

  const auto full = compute_low_res_viewport(1920U, 1080U, 100U);
  CHECK(full.scaled_width == 1920U);
  CHECK(full.scaled_height == 1080U);
  CHECK(full.pixel_savings_ratio == doctest::Approx(0.0));

  // Scale below 25 clamps cleanly to 25%.
  const auto min_scale = compute_low_res_viewport(320U, 240U, 10U);
  CHECK(min_scale.scaled_width == 80U);
  CHECK(min_scale.scaled_height == 60U);

  // Zero-sized surface stays 0 safely without dividing by zero.
  const auto zero = compute_low_res_viewport(0U, 0U, 50U);
  CHECK(zero.scaled_width == 0U);
  CHECK(zero.scaled_height == 0U);
}

TEST_CASE("ParticlePassPolicy enforces auto, throttle, and half_res budgets across frames") {
  using namespace sf::games::ds3;

  const sf::RenderProfile profile{};
  sf::Config cfg = sf::Config::defaults();
  cfg.modules.particles = true;
  cfg.particles.mode = "auto";
  cfg.particles.scale_percent = 50U;
  cfg.particles.max_instances_per_draw = 4U;
  cfg.particles.max_draws_per_frame = 2U;

  ParticleDrawCandidate draw{};
  draw.rtv_count = 1U;
  draw.rt_width = 320U;
  draw.rt_height = 240U;
  draw.blend_enabled = true;
  draw.depth_enabled = true;
  draw.depth_write = false;
  draw.vertex_or_index_count = 6U;
  draw.instance_count = 8U;
  draw.is_instanced = true;

  ParticlePassPolicy policy;
  policy.begin_frame(1U);

  // Draw 1: 8 instances -> clamped to 4, scaled to 160x120.
  const auto a1 = policy.evaluate_draw(profile, cfg, draw);
  CHECK(a1.is_particle);
  CHECK_FALSE(a1.skip_draw);
  CHECK(a1.scale_viewport);
  CHECK(a1.effective_instance_count == 4U);
  CHECK(a1.instances_culled == 4U);
  CHECK(a1.viewport.scaled_width == 160U);
  CHECK(a1.viewport.scaled_height == 120U);

  // Draw 2: allowed within max_draws_per_frame = 2.
  const auto a2 = policy.evaluate_draw(profile, cfg, draw);
  CHECK_FALSE(a2.skip_draw);
  CHECK(a2.effective_instance_count == 4U);

  // Draw 3: exceeds max_draws_per_frame = 2 -> skipped!
  const auto a3 = policy.evaluate_draw(profile, cfg, draw);
  CHECK(a3.is_particle);
  CHECK(a3.skip_draw);
  CHECK(a3.effective_instance_count == 0U);
  CHECK(a3.instances_culled == 8U);

  // Frame 2 resets draw budget cleanly.
  policy.begin_frame(2U);
  CHECK(policy.current_frame() == 2U);
  CHECK(policy.frame_particle_draws() == 0U);
  const auto a4 = policy.evaluate_draw(profile, cfg, draw);
  CHECK_FALSE(a4.skip_draw);
  CHECK(a4.effective_instance_count == 4U);

  // half_res mode ignores instance caps while scaling resolution.
  cfg.particles.mode = "half_res";
  const auto a5 = policy.evaluate_draw(profile, cfg, draw);
  CHECK(a5.scale_viewport);
  CHECK(a5.effective_instance_count == 8U);

  // throttle mode on frame 3 disables low-res scaling while capping instances.
  policy.begin_frame(3U);
  cfg.particles.mode = "throttle";
  const auto a6 = policy.evaluate_draw(profile, cfg, draw);
  CHECK_FALSE(a6.scale_viewport);
  CHECK_FALSE(a6.skip_draw);
  CHECK(a6.effective_instance_count == 4U);

  const auto& st = policy.stats();
  CHECK(st.frames_observed == 3U);
  CHECK(st.particle_draws == 6U);
  CHECK(st.skipped_draws == 1U);
  CHECK(st.throttled_draws == 4U);
  CHECK(st.low_res_draws == 4U);
  CHECK(st.instances_culled == 24U);
  CHECK(st.estimated_pixels_saved > 0ULL);
}
