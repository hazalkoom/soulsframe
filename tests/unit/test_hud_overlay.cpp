#include "gfx/fx/hud_overlay.h"

#include <doctest/doctest.h>

TEST_CASE("format_hud_lines renders FPS telemetry and status badges") {
  sf::gfx::fx::HudSnapshot snap{};
  snap.fps = 59.8;
  snap.avg_ms = 16.7;
  snap.p1_low_ms = 17.4;
  snap.mod_active = true;
  snap.upscaler_configured = true;
  snap.upscaler_on = true;
  snap.particles_configured = false;
  snap.particles_on = false;
  snap.frame_cap_fps = 60U;
  snap.frame_cap_on = true;

  const auto lines = sf::gfx::fx::format_hud_lines(snap);
  REQUIRE(lines.size() == 2U);
  CHECK(lines[0].find("FPS") != std::string::npos);
  CHECK(lines[0].find("PAUSED") == std::string::npos);
  CHECK(lines[1].find("[F10] MOD:ON") != std::string::npos);
  CHECK(lines[1].find("[F11] FSR:ON") != std::string::npos);
  CHECK(lines[1].find("[F8] PT:OFF") != std::string::npos);
  CHECK(lines[1].find("[F12] CAP:60") != std::string::npos);

  snap.mod_active = false;
  snap.show_fps = true;
  snap.show_status = true;
  const auto paused = sf::gfx::fx::format_hud_lines(snap);
  REQUIRE(paused.size() == 2U);
  CHECK(paused[0].find("PAUSED |") != std::string::npos);
  CHECK(paused[1].find("MOD:OFF") != std::string::npos);
  CHECK(paused[1].find("FSR:PAUSED") != std::string::npos);
  CHECK(paused[1].find("CAP:PAUSED") != std::string::npos);

  snap.show_fps = false;
  snap.show_status = false;
  CHECK(sf::gfx::fx::format_hud_lines(snap).empty());
}

TEST_CASE("rasterize_hud produces crisp bordered RGBA8 panel") {
  CHECK(sf::gfx::fx::rasterize_hud({}, true, 75U).rgba.empty());
  CHECK(sf::gfx::fx::rasterize_hud({""}, true, 75U).rgba.empty());

  const auto active_bmp = sf::gfx::fx::rasterize_hud({"SF v0.10.0", "MOD:ON"}, true, 80U);
  CHECK(active_bmp.width == (10U * 8U) + 8U);
  CHECK(active_bmp.height > 16U);
  CHECK(active_bmp.rgba.size() == static_cast<size_t>(active_bmp.width) * active_bmp.height);

  const auto paused_bmp = sf::gfx::fx::rasterize_hud({"SF v0.10.0", "MOD:OFF"}, false, 80U);
  // Active and paused border accent colors differ on top-left corner pixel.
  CHECK(active_bmp.rgba[0] != paused_bmp.rgba[0]);
}

TEST_CASE("compute_hud_rect positions corners and clamps within small windows") {
  const auto tl = sf::gfx::fx::compute_hud_rect(1920U, 1080U, 200U, 28U, 2U, "top_left");
  CHECK(tl.x == doctest::Approx(12.0f));
  CHECK(tl.y == doctest::Approx(12.0f));
  CHECK(tl.width == doctest::Approx(400.0f));
  CHECK(tl.height == doctest::Approx(56.0f));

  const auto br = sf::gfx::fx::compute_hud_rect(1920U, 1080U, 200U, 28U, 2U, "bottom_right");
  CHECK(br.x == doctest::Approx(1920.0f - 400.0f - 12.0f));
  CHECK(br.y == doctest::Approx(1080.0f - 56.0f - 12.0f));

  // Small 320x240 testbed window must clamp width inside backbuffer bounds.
  const auto clamped = sf::gfx::fx::compute_hud_rect(320U, 240U, 450U, 28U, 2U, "top_right");
  CHECK(clamped.width <= 320.0f);
  CHECK(clamped.x >= 0.0f);
  CHECK(clamped.x + clamped.width <= 320.0f);

  // Zero backbuffer returns empty rect safely.
  CHECK(sf::gfx::fx::compute_hud_rect(0U, 240U, 100U, 20U, 2U, "top_left").width == 0.0f);
}
