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

  // Non-ASCII bytes (e.g. U+00C2) must fail cleanly without hitting upstream UTF-8 UB,
  // while a leading UTF-8 BOM before valid ASCII TOML is tolerated.
  const auto non_ascii = sf::parse_config_toml("[general]\noff\xC3\x82ne = true\n");
  CHECK_FALSE(non_ascii.parsed_ok);
  CHECK_FALSE(non_ascii.config.general.offline_acknowledged);

  const auto bom_ok = sf::parse_config_toml("\xEF\xBB\xBF[present]\nmax_fps = 90\n");
  CHECK(bom_ok.parsed_ok);
  CHECK(bom_ok.config.present.max_fps == 90);
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

TEST_CASE("probe.gpu_timing defaults on, can be disabled, and rejects wrong types") {
  CHECK(sf::Config::defaults().probe.gpu_timing);

  const auto off = sf::parse_config_toml("[probe]\ngpu_timing = false\n");
  CHECK(off.parsed_ok);
  CHECK_FALSE(off.config.probe.gpu_timing);
  CHECK(off.warnings.empty());

  const auto bad = sf::parse_config_toml("[probe]\ngpu_timing = \"yes\"\n");
  CHECK(bad.config.probe.gpu_timing); // falls back to the safe default
  CHECK_FALSE(bad.warnings.empty());
}

TEST_CASE("probe.dump_module defaults off and dump_file blocks traversal") {
  const auto def = sf::Config::defaults();
  CHECK_FALSE(def.probe.dump_module);
  CHECK(def.probe.dump_file == "soulsframe_mem_dump.exe");

  const auto valid = sf::parse_config_toml(R"(
    [probe]
    dump_module = true
    dump_file = "ds3_dump.exe"
  )");
  CHECK(valid.parsed_ok);
  CHECK(valid.warnings.empty());
  CHECK(valid.config.probe.dump_module);
  CHECK(valid.config.probe.dump_file == "ds3_dump.exe");

  const auto bad = sf::parse_config_toml(R"(
    [probe]
    dump_module = "bad"
    dump_file = "../evil.exe"
  )");
  CHECK(bad.parsed_ok);
  CHECK_FALSE(bad.config.probe.dump_module);
  CHECK(bad.config.probe.dump_file == "soulsframe_mem_dump.exe");
  CHECK(bad.warnings.size() == 2);
}

TEST_CASE("config validates [particles] settings, modes, and bounds") {
  const auto def = sf::Config::defaults();
  CHECK_FALSE(def.modules.particles);
  CHECK(def.particles.mode == "auto");
  CHECK(def.particles.scale_percent == 50U);
  CHECK(def.particles.max_instances_per_draw == 0U);
  CHECK(def.particles.max_draws_per_frame == 0U);

  const auto valid = sf::parse_config_toml(R"(
    [modules]
    particles = true

    [particles]
    mode = "half_res"
    scale_percent = 50
    max_instances_per_draw = 128
    max_draws_per_frame = 250
  )");
  CHECK(valid.parsed_ok);
  CHECK(valid.warnings.empty());
  CHECK(valid.config.modules.particles);
  CHECK(valid.config.particles.mode == "half_res");
  CHECK(valid.config.particles.scale_percent == 50U);
  CHECK(valid.config.particles.max_instances_per_draw == 128U);
  CHECK(valid.config.particles.max_draws_per_frame == 250U);

  const auto throttle = sf::parse_config_toml(R"(
    [particles]
    mode = "throttle"
    scale_percent = 10
    max_instances_per_draw = 999999
    max_draws_per_frame = -5
  )");
  CHECK(throttle.parsed_ok);
  CHECK(throttle.config.particles.mode == "throttle");
  CHECK(throttle.config.particles.scale_percent == 25U);
  CHECK(throttle.config.particles.max_instances_per_draw == 4096U);
  CHECK(throttle.config.particles.max_draws_per_frame == 0U);
  CHECK(throttle.warnings.size() == 3);

  const auto bad = sf::parse_config_toml(R"(
    [particles]
    mode = "bogus"
    scale_percent = 200
    unknown_field = 1
  )");
  CHECK(bad.parsed_ok);
  CHECK(bad.config.particles.mode == "auto");
  CHECK(bad.config.particles.scale_percent == 100U);
  CHECK(bad.warnings.size() == 3);
}

TEST_CASE("config validates [upscaler] modes, presets, and bounds") {
  const auto def = sf::Config::defaults();
  CHECK_FALSE(def.modules.upscaler);
  CHECK(def.upscaler.mode == "fsr1");
  CHECK(def.upscaler.preset == "custom");
  CHECK(def.upscaler.scale_percent == 77U);
  CHECK(def.upscaler.sharpness_percent == 80U);

  const auto valid = sf::parse_config_toml(R"(
    [modules]
    upscaler = true

    [upscaler]
    mode = "fsr1"
    preset = "quality"
    scale_percent = 70
    sharpness_percent = 90
  )");
  CHECK(valid.parsed_ok);
  CHECK(valid.warnings.empty());
  CHECK(valid.config.modules.upscaler);
  CHECK(valid.config.upscaler.mode == "fsr1");
  CHECK(valid.config.upscaler.preset == "quality");
  CHECK(valid.config.upscaler.scale_percent == 70U);
  CHECK(valid.config.upscaler.sharpness_percent == 90U);

  const auto clamped = sf::parse_config_toml(R"(
    [upscaler]
    mode = "bilinear"
    preset = "performance"
    scale_percent = 10
    sharpness_percent = 250
  )");
  CHECK(clamped.parsed_ok);
  CHECK(clamped.config.upscaler.mode == "bilinear");
  CHECK(clamped.config.upscaler.preset == "performance");
  CHECK(clamped.config.upscaler.scale_percent == 50U);
  CHECK(clamped.config.upscaler.sharpness_percent == 100U);
  CHECK(clamped.warnings.size() == 2);

  const auto bad = sf::parse_config_toml(R"(
    [upscaler]
    mode = "dlss_fake"
    preset = "super_duper"
    extra_key = 42
  )");
  CHECK(bad.parsed_ok);
  CHECK(bad.config.upscaler.mode == "fsr1");
  CHECK(bad.config.upscaler.preset == "custom");
  CHECK(bad.warnings.size() == 3);
}
