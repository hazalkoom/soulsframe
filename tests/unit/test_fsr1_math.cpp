#include <doctest/doctest.h>

#include "gfx/fx/fsr1_math.h"

TEST_CASE("resolve_preset_scale_percent maps all FSR 1 presets accurately") {
  using namespace sf::gfx::fx;

  CHECK(resolve_preset_scale_percent("native", 77U) == 100U);
  CHECK(resolve_preset_scale_percent("ultra_quality", 60U) == 77U);
  CHECK(resolve_preset_scale_percent("quality", 60U) == 67U);
  CHECK(resolve_preset_scale_percent("balanced", 60U) == 59U);
  CHECK(resolve_preset_scale_percent("performance", 60U) == 50U);
  CHECK(resolve_preset_scale_percent("custom", 85U) == 85U);
  CHECK(resolve_preset_scale_percent("custom", 10U) == 50U);
  CHECK(resolve_preset_scale_percent("custom", 200U) == 100U);
}

TEST_CASE("compute_upscale_dimensions handles all modes and zero dimensions safely") {
  using namespace sf::gfx::fx;

  sf::Config::Upscaler cfg{};
  cfg.mode = "fsr1";
  cfg.preset = "ultra_quality";

  const auto uq = compute_upscale_dimensions(1920U, 1080U, cfg);
  CHECK(uq.effective_scale_percent == 77U);
  CHECK(uq.render_width == (1920U * 77U) / 100U);
  CHECK(uq.render_height == (1080U * 77U) / 100U);
  CHECK(uq.easu_active);
  CHECK(uq.rcas_active);

  cfg.mode = "bilinear";
  cfg.preset = "performance";
  const auto bil = compute_upscale_dimensions(1920U, 1080U, cfg);
  CHECK(bil.render_width == 960U);
  CHECK(bil.render_height == 540U);
  CHECK(bil.easu_active);
  CHECK_FALSE(bil.rcas_active);

  cfg.mode = "rcas_only";
  cfg.preset = "performance";
  const auto rcas = compute_upscale_dimensions(1920U, 1080U, cfg);
  CHECK(rcas.effective_scale_percent == 100U);
  CHECK(rcas.render_width == 1920U);
  CHECK_FALSE(rcas.easu_active);
  CHECK(rcas.rcas_active);

  cfg.mode = "none";
  const auto off = compute_upscale_dimensions(1920U, 1080U, cfg);
  CHECK_FALSE(off.easu_active);
  CHECK_FALSE(off.rcas_active);

  const auto zero = compute_upscale_dimensions(0U, 0U, cfg);
  CHECK(zero.render_width == 0U);
  CHECK_FALSE(zero.easu_active);
}

TEST_CASE("compute_easu_constants and compute_rcas_constants compute valid GPU constants") {
  using namespace sf::gfx::fx;

  const auto zero_easu = compute_easu_constants(0U, 1080U, 1920U, 1080U, 1920U, 1080U);
  CHECK(zero_easu.con0[0] == doctest::Approx(0.0f));

  const auto easu = compute_easu_constants(960U, 540U, 960U, 540U, 1920U, 1080U);
  CHECK(easu.con0[0] == doctest::Approx(0.5f));
  CHECK(easu.con0[1] == doctest::Approx(0.5f));
  CHECK(easu.con1[0] == doctest::Approx(1.0f / 960.0f));
  CHECK(easu.con1[1] == doctest::Approx(1.0f / 540.0f));

  const auto rcas_max = compute_rcas_constants(1920U, 1080U, 100U);
  CHECK(rcas_max.con0[0] == doctest::Approx(1.0f));
  CHECK(rcas_max.con0[3] == doctest::Approx(0.0f));
  CHECK(rcas_max.con0[1] == doctest::Approx(1.0f / 1920.0f));
  CHECK(rcas_max.con0[2] == doctest::Approx(1.0f / 1080.0f));

  const auto rcas_min = compute_rcas_constants(1920U, 1080U, 0U);
  CHECK(rcas_min.con0[0] == doctest::Approx(0.25f));
  CHECK(rcas_min.con0[3] == doctest::Approx(2.0f));
  CHECK(rcas_max.con0[0] > rcas_min.con0[0]);
}
