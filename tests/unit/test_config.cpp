#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/config.h"

#include <string>
#include <string_view>

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

  // Malformed table header characters must return a parse error cleanly, never assert/abort.
  const auto bad_hdr = sf::parse_config_toml("[%general]\noffline_acknowledged = true\n");
  CHECK_FALSE(bad_hdr.parsed_ok);
  CHECK_FALSE(bad_hdr.config.general.offline_acknowledged);
}

TEST_CASE("config handles adversarial types, negative bounds, null bytes, and large comments") {
  // Wrong field types for every key must produce warnings and preserve safe defaults.
  const auto wrong_types = sf::parse_config_toml(R"(
    unknown_section = true

    [general]
    offline_acknowledged = "yes"
    mod_enabled = [1]
    log_level = 99

    [present]
    mode = true
    max_fps = "fast"

    [modules]
    frame_telemetry = "on"
    presentation = "nope"
  )");
  CHECK(wrong_types.parsed_ok);
  CHECK_FALSE(wrong_types.config.general.offline_acknowledged);
  CHECK(wrong_types.config.general.mod_enabled);
  CHECK(wrong_types.config.general.log_level == "info");
  CHECK(wrong_types.config.present.mode == "game");
  CHECK(wrong_types.config.present.max_fps == 0);
  CHECK(wrong_types.config.modules.frame_telemetry);
  CHECK_FALSE(wrong_types.config.modules.presentation);
  CHECK(wrong_types.warnings.size() >= 8);

  // Negative and overflowing max_fps must clamp cleanly to [0, 360].
  const auto neg = sf::parse_config_toml("[present]\nmax_fps = -100\n");
  CHECK(neg.parsed_ok);
  CHECK(neg.config.present.max_fps == 0);
  CHECK_FALSE(neg.warnings.empty());

  const auto huge = sf::parse_config_toml("[present]\nmax_fps = 1000000\n");
  CHECK(huge.parsed_ok);
  CHECK(huge.config.present.max_fps == 360);
  CHECK_FALSE(huge.warnings.empty());

  // Embedded NUL bytes must fail cleanly without throwing out of parse_config_toml.
  using namespace std::string_view_literals;
  const std::string_view nul_payload = "[general]\noffline_acknowledged = true\0bad"sv;
  const auto nul_res = sf::parse_config_toml(nul_payload);
  CHECK_FALSE(nul_res.parsed_ok);
  CHECK_FALSE(nul_res.config.general.offline_acknowledged);

  const std::string_view nul_header = "[general]\n[\0present]\n"sv;
  const auto nul_header_res = sf::parse_config_toml(nul_header);
  CHECK_FALSE(nul_header_res.parsed_ok);
  CHECK_FALSE(nul_header_res.config.general.offline_acknowledged);

  // Empty and 64 KB comment/whitespace stress payloads remain valid and safe.
  const auto empty_res = sf::parse_config_toml("");
  CHECK(empty_res.parsed_ok);
  CHECK_FALSE(empty_res.config.general.offline_acknowledged);

  std::string large_doc(65536, ' ');
  large_doc += "\n# padding comment\n[present]\nmax_fps = 60\n";
  const auto large_res = sf::parse_config_toml(large_doc);
  CHECK(large_res.parsed_ok);
  CHECK(large_res.config.present.max_fps == 60);
  CHECK_FALSE(large_res.config.general.offline_acknowledged);
}

TEST_CASE("config load_config_file handles missing path safely") {
  const auto missing = sf::load_config_file("/nonexistent/soulsframe_missing_12345.toml");
  CHECK(missing.parsed_ok);
  CHECK_FALSE(missing.config.general.offline_acknowledged);
  CHECK_FALSE(missing.warnings.empty());
}

TEST_CASE("config validates [probe] settings and rejects unsafe output paths") {
  const auto valid = sf::parse_config_toml(R"(
    [modules]
    probe_tracer = true

    [probe]
    capture_frames = 240
    output_file = "ds3_trace.jsonl"
    trace_resources = false
  )");
  CHECK(valid.parsed_ok);
  CHECK(valid.warnings.empty());
  CHECK(valid.config.modules.probe_tracer);
  CHECK(valid.config.probe.capture_frames == 240);
  CHECK(valid.config.probe.output_file == "ds3_trace.jsonl");
  CHECK_FALSE(valid.config.probe.trace_resources);

  const auto bad = sf::parse_config_toml(R"(
    [probe]
    capture_frames = 0
    output_file = "../../etc/passwd"
    trace_resources = "yes"
    extra_key = 1
  )");
  CHECK(bad.parsed_ok);
  CHECK(bad.config.probe.capture_frames == 1);
  CHECK(bad.config.probe.output_file == "soulsframe_capture.jsonl");
  CHECK(bad.config.probe.trace_resources);
  CHECK(bad.warnings.size() == 4);

  const auto wrong_file_type = sf::parse_config_toml(R"(
    [probe]
    capture_frames = 999999
    output_file = 123
  )");
  CHECK(wrong_file_type.parsed_ok);
  CHECK(wrong_file_type.config.probe.capture_frames == 36000);
  CHECK(wrong_file_type.config.probe.output_file == "soulsframe_capture.jsonl");
  CHECK(wrong_file_type.warnings.size() == 2);
}
