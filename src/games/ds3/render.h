#pragma once

#include "core/config.h"
#include "games/ds3/profile.h"

#include <cstdint>

namespace sf::games::ds3 {

/// Pure snapshot of active D3D11 pipeline state at a draw call for DS3 particle detection.
/// Zero Win32 or D3D11 header dependencies so pass heuristics remain testable on Linux.
struct ParticleDrawCandidate {
  uint8_t rtv_count = 0;
  uint32_t rt_width = 0;
  uint32_t rt_height = 0;
  bool blend_enabled = false;
  bool depth_enabled = true;
  bool depth_write = true;
  bool has_vertex_shader = true;
  bool has_pixel_shader = true;
  uint32_t vertex_or_index_count = 0;
  uint32_t instance_count = 1;
  bool is_instanced = false;
};

/// Downscaled viewport geometry computed from full render-target dimensions and `scale_percent`.
struct LowResViewport {
  uint32_t full_width = 0;
  uint32_t full_height = 0;
  uint32_t scaled_width = 0;
  uint32_t scaled_height = 0;
  float scale_factor = 1.0f;
  uint64_t full_pixels = 0;
  uint64_t scaled_pixels = 0;
  double pixel_savings_ratio = 0.0;
};

/// Mitigation action returned by `ParticlePassPolicy::evaluate_draw()`.
struct ParticleMitigationAction {
  bool is_particle = false;
  bool skip_draw = false;
  bool scale_viewport = false;
  uint32_t effective_instance_count = 1;
  uint32_t instances_culled = 0;
  LowResViewport viewport{};
};

/// Cumulative runtime telemetry accumulated by `ParticlePassPolicy`.
struct ParticleStats {
  uint64_t frames_observed = 0;
  uint64_t particle_draws = 0;
  uint64_t skipped_draws = 0;
  uint64_t throttled_draws = 0;
  uint64_t low_res_draws = 0;
  uint64_t instances_submitted = 0;
  uint64_t instances_culled = 0;
  uint64_t estimated_pixels_saved = 0;
};

/// Evaluates whether a draw call matches DS3 alpha/additive particle pass heuristics.
bool is_particle_draw(const RenderProfile& profile, const ParticleDrawCandidate& draw) noexcept;

/// Computes scaled viewport dimensions and estimated pixel-fill savings for `scale_percent` ([25,
/// 100]).
LowResViewport compute_low_res_viewport(uint32_t width, uint32_t height,
                                        uint32_t scale_percent) noexcept;

/// Stateful per-frame particle pass mitigation policy enforcing draw/instance budgets
/// and low-resolution viewport scaling without touching D3D11 types.
class ParticlePassPolicy {
public:
  /// Resets per-frame counters for `frame_idx`.
  void begin_frame(uint64_t frame_idx) noexcept;

  /// Evaluates `draw` against `profile` and `cfg`, updating cumulative statistics.
  ParticleMitigationAction evaluate_draw(const RenderProfile& profile, const Config& cfg,
                                         const ParticleDrawCandidate& draw) noexcept;

  /// Returns cumulative mitigation statistics across observed frames.
  const ParticleStats& stats() const noexcept { return stats_; }

  /// Returns the number of particle draws observed so far in the current frame.
  uint32_t frame_particle_draws() const noexcept { return frame_particle_draws_; }

  /// Returns the most recent frame index passed to `begin_frame()`.
  uint64_t current_frame() const noexcept { return current_frame_; }

private:
  uint64_t current_frame_ = 0;
  uint32_t frame_particle_draws_ = 0;
  ParticleStats stats_{};
};

} // namespace sf::games::ds3
