#include "gfx/fx/fsr1_math.h"

#include <algorithm>
#include <cmath>

namespace sf::gfx::fx {

uint32_t resolve_preset_scale_percent(std::string_view preset,
                                      uint32_t custom_scale_percent) noexcept {
  if (preset == "native") {
    return 100U;
  }
  if (preset == "ultra_quality") {
    return 77U;
  }
  if (preset == "quality") {
    return 67U;
  }
  if (preset == "balanced") {
    return 59U;
  }
  if (preset == "performance") {
    return 50U;
  }
  return std::clamp(custom_scale_percent, 50U, 100U);
}

UpscaleDimensions compute_upscale_dimensions(uint32_t display_width, uint32_t display_height,
                                             const Config::Upscaler& cfg) noexcept {
  UpscaleDimensions out{};
  out.display_width = display_width;
  out.display_height = display_height;
  out.effective_scale_percent = (cfg.mode == "none" || cfg.mode == "rcas_only")
                                    ? 100U
                                    : resolve_preset_scale_percent(cfg.preset, cfg.scale_percent);

  if (display_width == 0U || display_height == 0U) {
    return out;
  }

  if (out.effective_scale_percent == 100U) {
    out.render_width = display_width;
    out.render_height = display_height;
  } else {
    out.render_width = std::max<uint32_t>(1U, (display_width * out.effective_scale_percent) / 100U);
    out.render_height =
        std::max<uint32_t>(1U, (display_height * out.effective_scale_percent) / 100U);
  }

  out.easu_active = (cfg.mode == "fsr1" || cfg.mode == "bilinear") &&
                    (out.render_width < display_width || out.render_height < display_height);
  out.rcas_active = (cfg.mode == "fsr1" || cfg.mode == "rcas_only");
  return out;
}

EasuConstants compute_easu_constants(uint32_t input_width, uint32_t input_height,
                                     uint32_t container_width, uint32_t container_height,
                                     uint32_t output_width, uint32_t output_height) noexcept {
  EasuConstants out{};
  if (input_width == 0U || input_height == 0U || container_width == 0U || container_height == 0U ||
      output_width == 0U || output_height == 0U) {
    return out;
  }

  const auto in_w = static_cast<float>(input_width);
  const auto in_h = static_cast<float>(input_height);
  const auto cont_w = static_cast<float>(container_width);
  const auto cont_h = static_cast<float>(container_height);
  const auto out_w = static_cast<float>(output_width);
  const auto out_h = static_cast<float>(output_height);

  // Mapping from output pixel center -> input texture space (AMD FsrEasuCon).
  out.con0 = {in_w / out_w, in_h / out_h, (0.5F * (in_w / out_w)) - 0.5F,
              (0.5F * (in_h / out_h)) - 0.5F};

  const float inv_w = 1.0F / cont_w;
  const float inv_h = 1.0F / cont_h;
  out.con1 = {inv_w, inv_h, 1.0F * inv_w, -1.0F * inv_h};
  out.con2 = {-1.0F * inv_w, 2.0F * inv_h, 1.0F * inv_w, 2.0F * inv_h};
  out.con3 = {0.0F, 4.0F * inv_h, 0.0F, 0.0F};
  return out;
}

RcasConstants compute_rcas_constants(uint32_t display_width, uint32_t display_height,
                                     uint32_t sharpness_percent) noexcept {
  RcasConstants out{};
  const uint32_t clamped = std::clamp(sharpness_percent, 0U, 100U);
  const float stops = 2.0F * (1.0F - (static_cast<float>(clamped) / 100.0F));
  const float sharpness = std::exp2(-stops);

  out.con0[0] = sharpness;
  out.con0[1] = (display_width > 0U) ? (1.0F / static_cast<float>(display_width)) : 0.0F;
  out.con0[2] = (display_height > 0U) ? (1.0F / static_cast<float>(display_height)) : 0.0F;
  out.con0[3] = stops;
  return out;
}

} // namespace sf::gfx::fx
