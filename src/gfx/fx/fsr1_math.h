#pragma once

#include "core/config.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace sf::gfx::fx {

/// Resolved internal render and final display dimensions alongside active FSR 1 stages.
struct UpscaleDimensions {
  uint32_t render_width = 0;
  uint32_t render_height = 0;
  uint32_t display_width = 0;
  uint32_t display_height = 0;
  uint32_t effective_scale_percent = 100;
  bool easu_active = false;
  bool rcas_active = false;
};

/// Constant-buffer payload for AMD FSR 1.0 Edge-Adaptive Spatial Upsampling (EASU).
struct EasuConstants {
  std::array<float, 4> con0{};
  std::array<float, 4> con1{};
  std::array<float, 4> con2{};
  std::array<float, 4> con3{};
};

/// Constant-buffer payload for AMD FSR 1.0 Robust Contrast-Adaptive Sharpening (RCAS).
struct RcasConstants {
  std::array<float, 4> con0{};
};

/// Resolves a named quality preset (`native`, `ultra_quality`, `quality`, `balanced`,
/// `performance`, or `custom`) to a clamped render-scale percentage in `[50, 100]`.
uint32_t resolve_preset_scale_percent(std::string_view preset,
                                      uint32_t custom_scale_percent) noexcept;

/// Computes internal render resolution and determines whether EASU and/or RCAS should run.
UpscaleDimensions compute_upscale_dimensions(uint32_t display_width, uint32_t display_height,
                                             const Config::Upscaler& cfg) noexcept;

/// Computes AMD FSR 1.0 EASU constant vectors from viewport and container dimensions.
EasuConstants compute_easu_constants(uint32_t input_width, uint32_t input_height,
                                     uint32_t container_width, uint32_t container_height,
                                     uint32_t output_width, uint32_t output_height) noexcept;

/// Computes AMD FSR 1.0 RCAS constant vector from output size and sharpness percentage `[0, 100]`.
RcasConstants compute_rcas_constants(uint32_t display_width, uint32_t display_height,
                                     uint32_t sharpness_percent) noexcept;

} // namespace sf::gfx::fx
