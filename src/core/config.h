#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

/// Runtime configuration loaded from `soulsframe.toml` next to `dinput8.dll`.
/// Pure C++ data structure: must remain independent of Win32 and D3D11 headers
/// so it can be parsed, validated, and unit-tested natively on Linux.
struct Config {
  /// Global safety, enablement, and logging settings.
  struct General {
    /// Explicit user opt-in confirming offline-only usage. Mod initialization
    /// is blocked by `evaluate_offline_guard()` while this remains false.
    bool offline_acknowledged = false;

    /// Master kill switch for the entire mod.
    bool mod_enabled = true;

    /// Log verbosity threshold ("debug", "info", "warn", or "error").
    std::string log_level = "info";
  } general;

  /// Presentation and frame-pacing settings.
  struct Present {
    /// Swap-chain presentation strategy: "game" (leave DS3 untouched) or
    /// "flip" (planned M5 flip-model experiment).
    std::string mode = "game";

    /// Optional frame-rate cap in FPS (0 = disabled, clamped to [0, 360]).
    uint32_t max_fps = 0;
  } present;

  /// Per-module kill switches allowing individual features to be disabled.
  struct Modules {
    bool frame_telemetry = true;
    bool presentation = false;
    bool probe_tracer = false;
  } modules;

  /// Settings for `SF_PROBE` draw-call and pass JSONL capture builds.
  struct Probe {
    /// Maximum frames to write per capture session (clamped to [1, 36000]).
    uint32_t capture_frames = 120;

    /// Output JSONL filename written alongside `dinput8.dll`.
    std::string output_file = "soulsframe_capture.jsonl";

    /// Record unique active D3D11 textures/render-targets alongside draw sites.
    bool trace_resources = true;
  } probe;

  /// Returns a `Config` populated with safe, conservative defaults.
  static Config defaults();
};

/// Outcome of parsing a TOML configuration document.
struct ConfigParseResult {
  /// Parsed configuration (falls back to defaults on invalid syntax or fields).
  Config config;

  /// Non-fatal warnings (unknown keys, clamped bounds, invalid types).
  std::vector<std::string> warnings;

  /// False when TOML syntax parsing failed; `config` still holds safe defaults.
  bool parsed_ok = true;
};

/// Parses TOML text into a `Config`, validating ranges and collecting warnings.
ConfigParseResult parse_config_toml(std::string_view toml_text);

/// Reads and parses a TOML file from disk. Missing files yield defaults + warning.
ConfigParseResult load_config_file(const std::string& path);

} // namespace sf
