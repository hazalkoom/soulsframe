#pragma once

#include "core/config.h"
#include "games/ds3/profile.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace sf::timing {

/// High-refresh simulation timing strategy evaluated by `FixedStepStepper`.
enum class FpsStrategy : uint8_t {
  FixedStep = 0,
  CameraOnly = 1,
  VariableDt = 2,
};

/// Parses a `[fps_unlock] strategy` string into `FpsStrategy` (defaults to `FixedStep`).
FpsStrategy parse_fps_strategy(std::string_view strategy) noexcept;

/// Result of verifying whether engine timing hooks are safe to activate on the host build.
struct FpsSafetyVerdict {
  bool module_enabled = false;
  bool engine_patch_allowed = false;
  bool dry_run_only = true;
  std::string reason;
};

/// Evaluates whether the host executable profile permits active engine timing patches or
/// restricts `FpsUnlockModule` to read-only dry-run telemetry.
FpsSafetyVerdict evaluate_fps_unlock_safety(const GameProfile& profile, const Config& cfg);

/// Computed simulation and camera step parameters for a single rendered frame.
struct SimStepResult {
  uint32_t sim_ticks = 0;
  float sim_dt_sec = 1.0f / 60.0f;
  float camera_dt_sec = 1.0f / 60.0f;
  float interpolation_alpha = 0.0f;
  bool clamped_spiral = false;
};

/// Cumulative diagnostics recorded by `FixedStepStepper`.
struct FixedStepStats {
  uint64_t render_frames = 0;
  uint64_t total_sim_ticks = 0;
  uint64_t interpolated_frames = 0;
  uint64_t multi_tick_frames = 0;
  uint64_t spiral_clamps = 0;
};

/// Pure C++20 fixed-step simulation accumulator decoupling display refresh rate (e.g. 120 Hz)
/// from FromSoftware's canonical 60 Hz physics/animation tick rate, with spiral-of-death clamping.
class FixedStepStepper {
public:
  explicit FixedStepStepper(const Config::FpsUnlock& cfg = {});

  /// Updates stepper configuration and resets accumulator state.
  void configure(const Config::FpsUnlock& cfg) noexcept;

  /// Clears accumulated time and resets statistics.
  void reset() noexcept;

  /// Advances the accumulator by `raw_delta_ms` and returns simulation step/interpolation outputs.
  SimStepResult advance(double raw_delta_ms) noexcept;

  /// Returns cumulative stepper statistics.
  const FixedStepStats& stats() const noexcept { return stats_; }

private:
  Config::FpsUnlock cfg_{};
  FpsStrategy strategy_ = FpsStrategy::FixedStep;
  double accumulator_ms_ = 0.0;
  FixedStepStats stats_{};
};

} // namespace sf::timing
