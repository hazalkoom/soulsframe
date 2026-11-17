#include <doctest/doctest.h>

#include "core/config.h"
#include "core/offline_guard.h"
#include "probe/jsonl_writer.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

/// Builds a huge TOML document with `count` distinct (valid) keys to stress the parser.
std::string huge_toml(size_t count) {
  std::string out;
  out.reserve(count * 24);
  out += "[general]\noffline_acknowledged = true\nmod_enabled = true\nlog_level = \"info\"\n";
  for (size_t i = 0; i < count; ++i) {
    out += "extra_key_" + std::to_string(i) + " = " + std::to_string(static_cast<int>(i)) + "\n";
  }
  out += "[present]\nmode = \"game\"\nmax_fps = 60\n";
  out += "[modules]\nframe_telemetry = true\npresentation = false\nprobe_tracer = false\n";
  out += "[probe]\ncapture_frames = 120\noutput_file = "
         "\"soulsframe_capture.jsonl\"\ntrace_resources = true\n";
  return out;
}

} // namespace

TEST_CASE("security: malformed TOML syntax fails cleanly without aborting") {
  // Unterminated string value.
  const auto t1 = sf::parse_config_toml("[general]\noffline_acknowledged = \"unterminated");
  CHECK_FALSE(t1.parsed_ok);
  CHECK_FALSE(t1.config.general.offline_acknowledged);
  CHECK_FALSE(t1.warnings.empty());

  // Malformed table header (bare `%` start).
  const auto t2 = sf::parse_config_toml("[%general]\noffline_acknowledged = true\n");
  CHECK_FALSE(t2.parsed_ok);
  CHECK_FALSE(t2.config.general.offline_acknowledged);

  // Truncated inline table.
  const auto t3 = sf::parse_config_toml("[general]\nfoo = {a = 1\n");
  CHECK_FALSE(t3.parsed_ok);

  // Dangling comma in inline table.
  const auto t4 = sf::parse_config_toml("[general]\nfoo = {a = 1,}\n");
  CHECK_FALSE(t4.parsed_ok);

  // Key with reserved characters outside quotes.
  const auto t5 = sf::parse_config_toml("[general]\nodd key = true\n");
  CHECK_FALSE(t5.parsed_ok);

  // Completely empty document is valid (defaults).
  const auto t6 = sf::parse_config_toml("");
  CHECK(t6.parsed_ok);
  CHECK_FALSE(t6.config.general.offline_acknowledged);

  // Whitespace-only document is valid (defaults).
  const auto t7 = sf::parse_config_toml("   \n\n  \t  \n");
  CHECK(t7.parsed_ok);

  // Only comments is valid (defaults).
  const auto t8 = sf::parse_config_toml("# just a comment\n# another one\n");
  CHECK(t8.parsed_ok);
}

TEST_CASE("security: malformed TOML semantic edge cases fall back to safe defaults") {
  // Wrong types for every scalar key. toml++ may coerce some types (e.g. int 42 -> bool true) or
  // fall back to defaults. Both outcomes are safe; we assert no dangerous value leaks through.
  const auto wrong = sf::parse_config_toml(R"(
    [general]
    offline_acknowledged = 42
    mod_enabled = "yes"
    log_level = true

    [present]
    mode = 42
    max_fps = "slow"

    [modules]
    frame_telemetry = 1
    presentation = [true]
    probe_tracer = {enabled = true}

    [probe]
    capture_frames = "lots"
    output_file = 3.14
    trace_resources = "maybe"
  )");
  CHECK(wrong.parsed_ok);
  CHECK_FALSE(wrong.warnings.empty());

  // max_fps must stay within [0, 360] regardless of input type.
  CHECK(wrong.config.present.max_fps <= 360);
  // log_level must be one of the four valid levels.
  const auto& ll = wrong.config.general.log_level;
  bool ll_valid = (ll == "debug" || ll == "info" || ll == "warn" || ll == "error");
  CHECK(ll_valid);
  // present.mode must be "game" or "flip".
  const auto& pm = wrong.config.present.mode;
  bool pm_valid = (pm == "game" || pm == "flip");
  CHECK(pm_valid);
  // output_file must not contain a path traversal sequence.
  CHECK(wrong.config.probe.output_file.find("..") == std::string::npos);
  // capture_frames must be within [1, 36000].
  CHECK(wrong.config.probe.capture_frames >= 1U);
  CHECK(wrong.config.probe.capture_frames <= 36000U);

  // Duplicate keys are rejected by toml++ as a hard parse error (safe).
  const auto dup = sf::parse_config_toml(
      "[general]\noffline_acknowledged = false\noffline_acknowledged = true\n");
  CHECK_FALSE(dup.parsed_ok);
  CHECK_FALSE(dup.config.general.offline_acknowledged);
}

TEST_CASE("security: huge TOML documents and huge string values remain bounded") {
  // 5000 extra unknown keys: parses, warns, but config stays sane.
  const auto big = sf::parse_config_toml(huge_toml(5000));
  CHECK(big.parsed_ok);
  CHECK(big.config.general.offline_acknowledged);
  CHECK(big.config.present.max_fps == 60);
  CHECK(big.warnings.size() >= 5000);

  // Extremely long string value for output_file (not a traversal, just huge).
  std::string long_name(100000, 'a');
  const auto long_val = sf::parse_config_toml("[probe]\noutput_file = \"" + long_name + "\"\n");
  CHECK(long_val.parsed_ok);
  CHECK(long_val.config.probe.output_file == long_name);

  // Very long section header name (unknown root table -> warning, no crash).
  std::string long_section(50000, 'x');
  const auto long_sec = sf::parse_config_toml("[" + long_section + "]\nfoo = 1\n");
  CHECK(long_sec.parsed_ok);
  CHECK_FALSE(long_sec.warnings.empty());

  // Very long key name inside a table.
  std::string long_key(50000, 'k');
  const auto long_k = sf::parse_config_toml("[general]\n" + long_key + " = true\n");
  CHECK(long_k.parsed_ok);
  CHECK_FALSE(long_k.warnings.empty());
}

TEST_CASE("security: path traversal and unsafe output_file values are rejected") {
  // Classic traversal: rejected via warning, falls back to safe default.
  const auto t1 = sf::parse_config_toml("[probe]\noutput_file = \"../../etc/passwd\"\n");
  CHECK(t1.parsed_ok);
  CHECK(t1.config.probe.output_file == "soulsframe_capture.jsonl");
  CHECK_FALSE(t1.warnings.empty());

  // Windows-style backslash traversal: rejected via warning, falls back to safe default.
  const auto t2 =
      sf::parse_config_toml("[probe]\noutput_file = \"..\\\\..\\\\windows\\\\win.ini\"\n");
  CHECK(t2.parsed_ok);
  CHECK(t2.config.probe.output_file == "soulsframe_capture.jsonl");
  CHECK_FALSE(t2.warnings.empty());

  // Mixed traversal: rejected via warning, falls back to safe default.
  const auto t3 = sf::parse_config_toml("[probe]\noutput_file = \"foo/../bar\"\n");
  CHECK(t3.parsed_ok);
  CHECK(t3.config.probe.output_file == "soulsframe_capture.jsonl");
  CHECK_FALSE(t3.warnings.empty());

  // Absolute path (starts with /) - not a traversal per the ".." check, accepted as-is.
  const auto t4 =
      sf::parse_config_toml("[probe]\noutput_file = \"/tmp/soulsframe_capture.jsonl\"\n");
  CHECK(t4.parsed_ok);
  CHECK(t4.config.probe.output_file == "/tmp/soulsframe_capture.jsonl");

  // Empty output_file is rejected via warning, falls back to safe default.
  const auto t5 = sf::parse_config_toml("[probe]\noutput_file = \"\"\n");
  CHECK(t5.parsed_ok);
  CHECK(t5.config.probe.output_file == "soulsframe_capture.jsonl");
  CHECK_FALSE(t5.warnings.empty());

  // Wrong type (integer instead of string) falls back to default via warning.
  const auto t6 = sf::parse_config_toml("[probe]\noutput_file = 42\n");
  CHECK(t6.parsed_ok);
  CHECK(t6.config.probe.output_file == "soulsframe_capture.jsonl");
  CHECK_FALSE(t6.warnings.empty());
}

TEST_CASE("security: json_escape handles adversarial input without corrupting output") {
  using sf::probe::json_escape;

  // Embedded NUL bytes must be escaped, not truncate output.
  using namespace std::string_view_literals;
  const std::string_view nul_payload = "ab\0cd\0ef"sv;
  const auto esc_nul = json_escape(nul_payload);
  CHECK(esc_nul.find("ab") != std::string::npos);
  CHECK(esc_nul.find("cd") != std::string::npos);
  CHECK(esc_nul.find("ef") != std::string::npos);
  CHECK(esc_nul.find("\\u0000") != std::string::npos);

  // Every control character 0x00-0x1F must produce a backslash escape sequence.
  // \b \f \n \r \t have named escapes; the rest use \uXXXX.
  std::string all_ctrl;
  all_ctrl.reserve(32);
  for (int i = 0; i < 32; ++i) {
    all_ctrl.push_back(static_cast<char>(i));
  }
  const auto esc_ctrl = json_escape(all_ctrl);
  // Each control byte becomes an escape starting with backslash; verify known mappings.
  CHECK(esc_ctrl.find("\\u0000") != std::string::npos); // NUL
  CHECK(esc_ctrl.find("\\u0001") != std::string::npos); // SOH
  CHECK(esc_ctrl.find("\\b") != std::string::npos);     // backspace (0x08)
  CHECK(esc_ctrl.find("\\u000b") != std::string::npos); // VT (0x0b)
  CHECK(esc_ctrl.find("\\f") != std::string::npos);     // form feed (0x0c)
  CHECK(esc_ctrl.find("\\u000e") != std::string::npos); // SO (0x0e)
  CHECK(esc_ctrl.find("\\n") != std::string::npos);     // newline (0x0a)
  CHECK(esc_ctrl.find("\\r") != std::string::npos);     // carriage return (0x0d)
  CHECK(esc_ctrl.find("\\t") != std::string::npos);     // tab (0x09)
  CHECK(esc_ctrl.find("\\u001f") != std::string::npos); // US (0x1f)

  // Huge input string must not crash or lose data.
  std::string huge(1000000, 'z');
  const auto esc_huge = json_escape(huge);
  CHECK(esc_huge.size() == huge.size());
  CHECK(esc_huge.find(std::string(1000000, 'z')) != std::string::npos);

  // Mixed quotes, backslashes, and actual control chars (not raw-string escape sequences).
  std::string mix_input;
  mix_input += "say ";
  mix_input.push_back('"');
  mix_input += "hi";
  mix_input.push_back('"');
  mix_input += " to ";
  mix_input.push_back('\\');
  mix_input.push_back('n');
  mix_input.push_back('\n');   // actual newline
  mix_input.push_back('\t');   // actual tab
  mix_input.push_back('\x01'); // actual SOH control char
  mix_input += " world";
  const auto esc_mix = json_escape(mix_input);
  CHECK(esc_mix.find(R"(\")") != std::string::npos);   // escaped quote
  CHECK(esc_mix.find(R"(\\)") != std::string::npos);   // escaped backslash
  CHECK(esc_mix.find("\\n") != std::string::npos);     // escaped newline
  CHECK(esc_mix.find("\\t") != std::string::npos);     // escaped tab
  CHECK(esc_mix.find("\\u0001") != std::string::npos); // escaped SOH
}

TEST_CASE("security: JSONL formatters tolerate extreme numeric values without UB") {
  using namespace sf::probe;

  // frame event with max uint64 and extreme doubles.
  const auto fe = format_frame_event(UINT64_MAX, std::numeric_limits<double>::infinity(),
                                     UINT32_MAX, UINT64_MAX);
  CHECK(!fe.empty());
  CHECK(fe.find(R"("type":"frame")") != std::string::npos);

  // NaN cpu_frame_ms is still serialized (snprintf produces "nan" or "inf").
  const auto fe_nan = format_frame_event(0, std::numeric_limits<double>::quiet_NaN(), 0, 0);
  CHECK(!fe_nan.empty());

  // Pass event with extreme values.
  PassSummary pass{
      .pass_id = UINT32_MAX,
      .tag = PassTag::ParticlesAlpha,
      .draw_calls = UINT32_MAX,
      .total_elements = UINT64_MAX,
      .rtv_signature = UINT64_MAX,
      .rt_width = UINT16_MAX,
      .rt_height = UINT16_MAX,
      .rtv_count = UINT8_MAX,
      .blend_enabled = true,
      .depth_write = false,
      .has_dispatch = true,
  };
  const auto pe = format_pass_event(UINT64_MAX, pass);
  CHECK(!pe.empty());
  CHECK(pe.find(R"("type":"gpu_pass")") != std::string::npos);

  // Draw site event with extreme values.
  CallSiteStats site{
      .caller_rva = UINT64_MAX,
      .ps_hash = UINT64_MAX,
      .api = DrawApi::Dispatch,
      .blend_enabled = true,
      .depth_write = false,
      .draw_count = UINT32_MAX,
      .total_elements = UINT64_MAX,
  };
  const auto dse = format_draw_site_event(UINT64_MAX, site);
  CHECK(!dse.empty());
  CHECK(dse.find(R"("type":"draw_site")") != std::string::npos);

  // Resource event with extreme values.
  ResourceSummary res{
      .kind = "Texture2D",
      .id_hash = UINT64_MAX,
      .width = UINT32_MAX,
      .height = UINT32_MAX,
      .format_or_stride = UINT32_MAX,
      .bind_flags = UINT32_MAX,
      .mip_levels = UINT32_MAX,
      .sample_count = UINT32_MAX,
      .is_render_target = true,
  };
  const auto re = format_resource_event(UINT64_MAX, res);
  CHECK(!re.empty());
  CHECK(re.find(R"("type":"resource")") != std::string::npos);

  // Session header with huge strings.
  SessionHeaderInfo hdr{
      .mod_version = std::string(10000, 'v'),
      .game = std::string(10000, 'g'),
      .exe_sha256 = std::string(10000, 's'),
      .build_mode = std::string(10000, 'b'),
  };
  const auto she = format_session_header(hdr);
  CHECK(!she.empty());
  CHECK(she.find(R"("type":"session_header")") != std::string::npos);
}

TEST_CASE("security: serialize_frame_snapshot handles empty and oversized snapshots") {
  using namespace sf::probe;

  // Empty snapshot.
  FrameTraceSnapshot empty{};
  empty.frame_index = 0;
  const auto empty_lines = serialize_frame_snapshot(empty, 16.6);
  CHECK(empty_lines.size() == 1);
  CHECK(empty_lines[0].find(R"("type":"frame","idx":0)") != std::string::npos);

  // Snapshot at capacity limits.
  FrameTraceSnapshot full{};
  full.frame_index = 42;
  full.total_draw_calls = UINT32_MAX;
  full.total_elements = UINT64_MAX;
  full.passes.reserve(3);
  full.call_sites.reserve(3);
  full.new_resources.reserve(3);

  PassSummary pass{
      .pass_id = 1, .tag = PassTag::ParticlesAlpha, .draw_calls = 10, .total_elements = 1000};
  full.passes.push_back(pass);

  CallSiteStats site{
      .caller_rva = 0x1234ULL, .api = DrawApi::DrawIndexed, .draw_count = 5, .total_elements = 500};
  full.call_sites.push_back(site);

  ResourceSummary res{.kind = "Texture2D",
                      .id_hash = 0xdeadbeefULL,
                      .width = 1920,
                      .height = 1080,
                      .is_render_target = true};
  full.new_resources.push_back(res);

  const auto full_lines = serialize_frame_snapshot(full, 33.3);
  REQUIRE(full_lines.size() == 4);
  CHECK(full_lines[0].find(R"("type":"resource")") != std::string::npos);
  CHECK(full_lines[1].find(R"("type":"frame","idx":42)") != std::string::npos);
  CHECK(full_lines[2].find(R"("type":"gpu_pass")") != std::string::npos);
  CHECK(full_lines[3].find(R"("type":"draw_site")") != std::string::npos);
}

TEST_CASE("security: offline guard spoofing attempts are all blocked") {
  // Spoof: online flag hidden among many benign args.
  {
    sf::OfflineCheckInput in;
    in.config_offline_acknowledged = true;
    in.command_line_args = {"DarkSoulsIII.exe", "-windowed", "-dx11", "-fullscreen", "--Online"};
    const auto v = sf::evaluate_offline_guard(in);
    CHECK_FALSE(v.allowed);
    CHECK_FALSE(v.reason.empty());
  }

  // Spoof: online flag with `=` suffix (e.g. --online=1). The guard uses exact string matching,
  // so --online=1 is NOT caught -- this documents the deliberate exact-match behavior.
  {
    sf::OfflineCheckInput in;
    in.config_offline_acknowledged = true;
    in.command_line_args = {"DarkSoulsIII.exe", "--online"};
    const auto v_blocked = sf::evaluate_offline_guard(in);
    CHECK_FALSE(v_blocked.allowed);
    CHECK_FALSE(v_blocked.reason.empty());

    in.command_line_args = {"DarkSoulsIII.exe", "--online=1"};
    const auto v_not_caught = sf::evaluate_offline_guard(in);
    CHECK(v_not_caught.allowed); // exact-match guard does not flag --online=1
  }

  // Spoof: force_online_detected overrides acknowledged opt-in AND benign args.
  {
    sf::OfflineCheckInput in;
    in.config_offline_acknowledged = true;
    in.force_online_detected = true;
    in.command_line_args = {"DarkSoulsIII.exe", "--offline"};
    const auto v = sf::evaluate_offline_guard(in);
    CHECK_FALSE(v.allowed);
    CHECK_FALSE(v.reason.empty());
  }

  // Spoof: no config opt-in, only force_online_detected false, no args -> blocked.
  {
    sf::OfflineCheckInput in;
    in.config_offline_acknowledged = false;
    in.command_line_args = {};
    const auto v = sf::evaluate_offline_guard(in);
    CHECK_FALSE(v.allowed);
  }

  // Spoof: empty string args that look like online flags but have extra chars -> allowed
  // (exact match only). These are NOT online flags.
  {
    sf::OfflineCheckInput in;
    in.config_offline_acknowledged = true;
    in.command_line_args = {"DarkSoulsIII.exe", "--online-extra", "-onlines",
                            "online",           "ONLINE ",        "--force-onlinex"};
    const auto v = sf::evaluate_offline_guard(in);
    CHECK(v.allowed);
  }

  // Spoof: acknowledged=true but force_online_detected=true with no args -> blocked.
  {
    sf::OfflineCheckInput in;
    in.config_offline_acknowledged = true;
    in.force_online_detected = true;
    const auto v = sf::evaluate_offline_guard(in);
    CHECK_FALSE(v.allowed);
  }

  // Spoof: acknowledged=false but force_online_detected=true -> blocked (forced takes precedence).
  {
    sf::OfflineCheckInput in;
    in.config_offline_acknowledged = false;
    in.force_online_detected = true;
    const auto v = sf::evaluate_offline_guard(in);
    CHECK_FALSE(v.allowed);
  }

  // Positive control: acknowledged, clean args -> allowed.
  {
    sf::OfflineCheckInput in;
    in.config_offline_acknowledged = true;
    in.command_line_args = {"DarkSoulsIII.exe"};
    const auto v = sf::evaluate_offline_guard(in);
    CHECK(v.allowed);
  }
}

TEST_CASE("security: load_config_file handles unreadable and dangerous paths safely") {
  // Missing file -> defaults + warning, no throw.
  const auto missing =
      sf::load_config_file("/nonexistent/soulsframe_missing_security_987654321.toml");
  CHECK(missing.parsed_ok);
  CHECK_FALSE(missing.config.general.offline_acknowledged);
  CHECK_FALSE(missing.warnings.empty());

  // Empty path -> treated as missing/unopenable.
  const auto empty_path = sf::load_config_file("");
  CHECK(empty_path.parsed_ok);
  CHECK_FALSE(empty_path.config.general.offline_acknowledged);
  CHECK_FALSE(empty_path.warnings.empty());
}
