#include "games/ds3/render.h"

#include <algorithm>

namespace sf::games::ds3 {

bool is_particle_draw(const RenderProfile& profile, const ParticleDrawCandidate& draw) noexcept {
  if (!profile.detect_particles_by_blend || !draw.blend_enabled) {
    return false;
  }
  if (!draw.has_vertex_shader || !draw.has_pixel_shader) {
    return false;
  }
  if (draw.rtv_count < profile.required_rtv_count || draw.rtv_count > 1U) {
    // G-buffer MRT passes bind >= 2 render targets; shadows bind 0 color RTVs.
    return false;
  }
  if (draw.rt_width < profile.min_rt_dimension || draw.rt_height < profile.min_rt_dimension) {
    return false;
  }
  if (profile.require_depth_read_only && (!draw.depth_enabled || draw.depth_write)) {
    return false;
  }
  return true;
}

LowResViewport compute_low_res_viewport(uint32_t width, uint32_t height,
                                        uint32_t scale_percent) noexcept {
  const uint32_t clamped_pct = std::clamp(scale_percent, 25U, 100U);
  LowResViewport out{};
  out.full_width = width;
  out.full_height = height;
  out.scale_factor = static_cast<float>(clamped_pct) / 100.0f;

  if (width == 0U || height == 0U) {
    return out;
  }

  if (clamped_pct == 100U) {
    out.scaled_width = width;
    out.scaled_height = height;
  } else {
    out.scaled_width = std::max<uint32_t>(1U, (width * clamped_pct) / 100U);
    out.scaled_height = std::max<uint32_t>(1U, (height * clamped_pct) / 100U);
  }

  out.full_pixels = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
  out.scaled_pixels =
      static_cast<uint64_t>(out.scaled_width) * static_cast<uint64_t>(out.scaled_height);
  if (out.full_pixels > 0ULL && out.scaled_pixels < out.full_pixels) {
    out.pixel_savings_ratio = static_cast<double>(out.full_pixels - out.scaled_pixels) /
                              static_cast<double>(out.full_pixels);
  }
  return out;
}

void ParticlePassPolicy::begin_frame(uint64_t frame_idx) noexcept {
  current_frame_ = frame_idx;
  frame_particle_draws_ = 0;
  ++stats_.frames_observed;
}

ParticleMitigationAction
ParticlePassPolicy::evaluate_draw(const RenderProfile& profile, const Config& cfg,
                                  const ParticleDrawCandidate& draw) noexcept {
  ParticleMitigationAction action{};
  action.effective_instance_count =
      draw.is_instanced ? std::max<uint32_t>(1U, draw.instance_count) : draw.instance_count;

  if (!cfg.modules.particles || !is_particle_draw(profile, draw)) {
    return action;
  }

  action.is_particle = true;
  ++stats_.particle_draws;
  ++frame_particle_draws_;

  const uint32_t raw_instances = action.effective_instance_count;
  stats_.instances_submitted += raw_instances;

  const bool allow_throttle = (cfg.particles.mode == "auto" || cfg.particles.mode == "throttle");
  const bool allow_low_res = (cfg.particles.mode == "auto" || cfg.particles.mode == "half_res");

  if (allow_throttle && cfg.particles.max_draws_per_frame > 0U &&
      frame_particle_draws_ > cfg.particles.max_draws_per_frame) {
    action.skip_draw = true;
    action.effective_instance_count = 0U;
    action.instances_culled = raw_instances;
    ++stats_.skipped_draws;
    stats_.instances_culled += raw_instances;
    return action;
  }

  if (allow_throttle && draw.is_instanced && cfg.particles.max_instances_per_draw > 0U &&
      raw_instances > cfg.particles.max_instances_per_draw) {
    action.effective_instance_count = cfg.particles.max_instances_per_draw;
    action.instances_culled = raw_instances - action.effective_instance_count;
    ++stats_.throttled_draws;
    stats_.instances_culled += action.instances_culled;
  }

  if (allow_low_res && cfg.particles.scale_percent < 100U) {
    action.viewport =
        compute_low_res_viewport(draw.rt_width, draw.rt_height, cfg.particles.scale_percent);
    if (action.viewport.scaled_width < action.viewport.full_width ||
        action.viewport.scaled_height < action.viewport.full_height) {
      action.scale_viewport = true;
      ++stats_.low_res_draws;
      if (action.viewport.full_pixels > action.viewport.scaled_pixels) {
        stats_.estimated_pixels_saved +=
            (action.viewport.full_pixels - action.viewport.scaled_pixels) *
            static_cast<uint64_t>(std::max<uint32_t>(1U, action.effective_instance_count));
      }
    }
  }

  return action;
}

} // namespace sf::games::ds3
