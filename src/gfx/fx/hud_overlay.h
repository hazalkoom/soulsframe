#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sf::gfx::fx {

/// Live telemetry + toggle state formatted into the on-screen HUD banner.
struct HudSnapshot {
  double fps = 0.0;
  double avg_ms = 0.0;
  double p1_low_ms = 0.0;
  uint32_t spikes = 0;

  bool mod_active = true;
  bool upscaler_configured = false;
  bool upscaler_on = false;
  bool particles_configured = false;
  bool particles_on = false;
  uint32_t frame_cap_fps = 0;
  bool frame_cap_on = false;

  std::string key_overlay = "F9";
  std::string key_mod = "F10";
  std::string key_upscaler = "F11";
  std::string key_particles = "F8";
  std::string key_cap = "F12";

  bool show_fps = true;
  bool show_status = true;
};

/// RGBA8 (`R8G8B8A8_UNORM`) rasterized overlay surface ready for `CreateTexture2D` upload.
struct HudBitmap {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint32_t> rgba;
};

/// Viewport coordinates (`D3D11_VIEWPORT` top-left + extent) for drawing the HUD quad.
struct HudRect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

/// Formats the FPS telemetry line and hotkey toggle legend enabled by `snap`.
std::vector<std::string> format_hud_lines(const HudSnapshot& snap);

/// Rasterizes ASCII text lines into a bordered translucent RGBA8 panel using a built-in 8x8 font.
HudBitmap rasterize_hud(const std::vector<std::string>& lines, bool mod_active,
                        uint32_t opacity_percent);

/// Computes an anchor-aligned screen rectangle (`"top_left"`, `"top_right"`, `"bottom_left"`,
/// `"bottom_right"`) clamped cleanly within small backbuffers.
HudRect compute_hud_rect(uint32_t backbuffer_w, uint32_t backbuffer_h, uint32_t bmp_w,
                         uint32_t bmp_h, uint32_t scale, std::string_view position,
                         uint32_t margin_px = 12U) noexcept;

} // namespace sf::gfx::fx
