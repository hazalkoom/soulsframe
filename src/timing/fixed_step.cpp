#include "timing/fixed_step.h"

#include <algorithm>
#include <cmath>

namespace sf::timing {

FpsStrategy parse_fps_strategy(std::string_view strategy) noexcept {
  if (strategy == "camera_only") {
    return FpsStrategy::CameraOnly;
  }
  if (strategy == "variable_dt") {
    return FpsStrategy::VariableDt;
  }
  return FpsStrategy::FixedStep;
}

FpsSafetyVerdict evaluate_fps_unlock_safety(const GameProfile& profile, const Config& cfg) {
  if (!cfg.modules.fps_unlock) {
    return FpsSafetyVerdict{.module_enabled = false,
                            .engine_patch_allowed = false,
                            .dry_run_only = true,
                            .reason = "modules.fps_unlock is disabled"};
  }
  if (!profile.engine_hooks_allowed) {
    return FpsSafetyVerdict{.module_enabled = true,
                            .engine_patch_allowed = false,
                            .dry_run_only = true,
                            .reason = "unverified executable build; running fixed-step accumulator "
                                      "in read-only dry-run mode"};
  }
  if (!profile.frame_dt_ptr.has_value() || !profile.frame_limiter.has_value()) {
    return FpsSafetyVerdict{.module_enabled = true,
                            .engine_patch_allowed = false,
                            .dry_run_only = true,
                            .reason = "missing frame_dt_ptr or frame_limiter signature; running "
                                      "fixed-step accumulator in read-only dry-run mode"};
  }
  return FpsSafetyVerdict{.module_enabled = true,
                          .engine_patch_allowed = true,
                          .dry_run_only = false,
                          .reason = "verified build with resolved frame_dt_ptr and frame_limiter"};
}

FixedStepStepper::FixedStepStepper(const Config::FpsUnlock& cfg) {
  configure(cfg);
}

void FixedStepStepper::configure(const Config::FpsUnlock& cfg) noexcept {
  cfg_ = cfg;
  cfg_.target_fps = std::clamp(cfg_.target_fps, 60U, 360U);
  cfg_.sim_tick_hz = std::clamp(cfg_.sim_tick_hz, 30U, 120U);
  cfg_.max_substeps = std::clamp(cfg_.max_substeps, 1U, 8U);
  strategy_ = parse_fps_strategy(cfg_.strategy);
  reset();
}

void FixedStepStepper::reset() noexcept {
  accumulator_ms_ = 0.0;
  stats_ = {};
}

SimStepResult FixedStepStepper::advance(double raw_delta_ms) noexcept {
  const double safe_delta_ms =
      (!std::isfinite(raw_delta_ms) || raw_delta_ms <= 0.0)
          ? (1000.0 / static_cast<double>(std::max<uint32_t>(1U, cfg_.target_fps)))
          : raw_delta_ms;

  const double fixed_dt_ms = 1000.0 / static_cast<double>(cfg_.sim_tick_hz);
  const float fixed_dt_sec = 1.0f / static_cast<float>(cfg_.sim_tick_hz);
  const float camera_dt_sec =
      std::clamp(static_cast<float>(safe_delta_ms / 1000.0), 1.0f / 360.0f, 1.0f / 15.0f);

  const double max_accum_ms = static_cast<double>(cfg_.max_substeps) * fixed_dt_ms;

  SimStepResult res{};
  res.camera_dt_sec = camera_dt_sec;

  if (strategy_ == FpsStrategy::VariableDt) {
    res.sim_ticks = 1U;
    res.sim_dt_sec = camera_dt_sec;
    res.interpolation_alpha = 1.0f;
    res.clamped_spiral = (safe_delta_ms > max_accum_ms);
  } else {
    accumulator_ms_ += safe_delta_ms;
    if (accumulator_ms_ > max_accum_ms) {
      accumulator_ms_ = max_accum_ms;
      res.clamped_spiral = true;
      ++stats_.spiral_clamps;
    }

    const uint32_t ticks = static_cast<uint32_t>(accumulator_ms_ / fixed_dt_ms);
    accumulator_ms_ -= static_cast<double>(ticks) * fixed_dt_ms;
    res.sim_ticks = ticks;
    res.sim_dt_sec = fixed_dt_sec;
    res.interpolation_alpha =
        std::clamp(static_cast<float>(accumulator_ms_ / fixed_dt_ms), 0.0f, 1.0f);
  }

  if (res.clamped_spiral && strategy_ == FpsStrategy::VariableDt) {
    ++stats_.spiral_clamps;
  }

  ++stats_.render_frames;
  stats_.total_sim_ticks += res.sim_ticks;
  if (res.sim_ticks == 0U) {
    ++stats_.interpolated_frames;
  } else if (res.sim_ticks > 1U) {
    ++stats_.multi_tick_frames;
  }
  return res;
}

} // namespace sf::timing
