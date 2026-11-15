#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/config.h"

TEST_CASE("config defaults") {
  const auto cfg = sf::Config::defaults();
  CHECK_FALSE(cfg.general.offline_acknowledged);
  CHECK(cfg.general.mod_enabled);
  CHECK(cfg.general.log_level == "info");
  CHECK(cfg.present.mode == "game");
  CHECK(cfg.present.max_fps == 0);
  CHECK(cfg.modules.frame_telemetry);
  CHECK_FALSE(cfg.modules.presentation);
}

TEST_CASE("config parses valid TOML") {
  const auto res = sf::parse_config_toml(R"(
    [general]
    offline_acknowledged = true
    mod_enabled = true
    log_level = "debug"

    [present]
    mode = "flip"
    max_fps = 120

    [modules]
    frame_telemetry = false
    presentation = true
  )");
  CHECK(res.parsed_ok);
  CHECK(res.warnings.empty());
  CHECK(res.config.general.offline_acknowledged);
  CHECK(res.config.general.log_level == "debug");
  CHECK(res.config.present.mode == "flip");
  CHECK(res.config.present.max_fps == 120);
  CHECK_FALSE(res.config.modules.frame_telemetry);
  CHECK(res.config.modules.presentation);
}

TEST_CASE("config warns on unknown keys and clamps out of range") {
  const auto res = sf::parse_config_toml(R"(
    [general]
    offline_acknowledged = true
    typo_key = 42

    [present]
    max_fps = 9999
    mode = "badmode"
  )");
  CHECK(res.parsed_ok);
  CHECK(res.config.present.max_fps == 360);
  CHECK(res.config.present.mode == "game");
  CHECK(res.warnings.size() >= 3);
}

TEST_CASE("config invalid TOML falls back to defaults") {
  const auto res = sf::parse_config_toml("this = [unterminated");
  CHECK_FALSE(res.parsed_ok);
  CHECK_FALSE(res.config.general.offline_acknowledged);
  CHECK(res.config.present.mode == "game");
  CHECK_FALSE(res.warnings.empty());
}
