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
    bool particles = false;
    bool upscaler = false;
    bool fps_unlock = false;
    bool textures = false;
    bool overlay = false;
  } modules;

  /// Milestone 6 particle pass mitigation and overdraw budget settings.
  struct Particles {
    /// Mitigation strategy: "auto" (low-res viewport/RT + instance budget),
    /// "throttle" (instance/draw budget only), or "half_res" (low-res scaling).
    std::string mode = "auto";

    /// Resolution percentage for low-res particle passes (clamped to [25, 100], 50 = half-res).
    uint32_t scale_percent = 50;

    /// Maximum instances per instanced particle draw (0 = unlimited, clamped to [0, 4096]).
    uint32_t max_instances_per_draw = 0;

    /// Maximum particle draw calls per frame (0 = unlimited, clamped to [0, 16384]).
    uint32_t max_draws_per_frame = 0;
  } particles;

  /// Milestone 7 spatial upscaling (FSR 1 EASU + RCAS) and internal render-scale settings.
  struct Upscaler {
    /// Upscaling pipeline mode: "none", "bilinear", "fsr1" (EASU + RCAS), or "rcas_only".
    std::string mode = "fsr1";

    /// Quality preset: "native" (100%), "ultra_quality" (77%), "quality" (67%),
    /// "balanced" (59%), "performance" (50%), or "custom" (uses `scale_percent`).
    std::string preset = "custom";

    /// Internal render resolution percentage when `preset = "custom"` (clamped to [50, 100]).
    uint32_t scale_percent = 77;

    /// RCAS sharpening strength percentage (clamped to [0, 100], 80 = 0.4 stops attenuation).
    uint32_t sharpness_percent = 80;
  } upscaler;

  /// Milestone 8 experimental high-refresh / fixed-step simulation timing settings.
  struct FpsUnlock {
    /// Timing integration strategy: "fixed_step" (60 Hz simulation accumulator + interpolation
    /// alpha), "camera_only" (60 Hz world sim + high-rate camera dt), or "variable_dt"
    /// (clamped variable delta-time experiment).
    std::string strategy = "fixed_step";

    /// Target unlocked presentation frame rate in FPS (clamped to [60, 360]).
    uint32_t target_fps = 120;

    /// Fixed simulation tick frequency in Hz (clamped to [30, 120], DS3 canonical = 60).
    uint32_t sim_tick_hz = 60;

    /// Maximum simulation substeps per render frame to prevent spiral-of-death stalls ([1, 8]).
    uint32_t max_substeps = 4;
  } fps_unlock;

  /// Milestone 9 offline texture-pack replacement settings (`CreateTexture2D` hash swap).
  struct Textures {
    /// Pack folder name next to `dinput8.dll` containing `manifest.txt` and DDS files.
    std::string pack_dir = "soulsframe_textures";

    /// Maximum replacement bytes uploaded per session in MiB (clamped to [16, 4096]).
    uint32_t max_pack_mb = 512;

    /// Log keys of eligible textures (first 4096 unique) so packs can be authored offline.
    bool dump_keys = false;
  } textures;

  /// In-game D3D11 FPS viewer & module status HUD (`[modules] overlay`).
  struct Overlay {
    /// Display live FPS, average frame time, and 1% low line in the HUD.
    bool show_fps = true;

    /// Display active feature badges and configured hotkey hints in the HUD.
    bool show_status = true;

    /// Screen anchor corner: "top_left", "top_right", "bottom_left", or "bottom_right".
    std::string position = "top_left";

    /// Integer pixel scaling factor for the 8x8 bitmap HUD (clamped to [1, 4]).
    uint32_t scale = 2;

    /// Panel background opacity percentage (clamped to [0, 100]).
    uint32_t opacity_percent = 75;
  } overlay;

  /// Live in-game keyboard hotkeys (`[modules] overlay` must be enabled).
  struct Hotkeys {
    /// Master switch allowing in-game hotkey polling without restarting the game.
    bool enabled = true;

    /// Show or hide the on-screen FPS/status HUD (default "F9").
    std::string toggle_overlay = "F9";

    /// Bypass or resume active performance features without unloading the DLL (default "F10").
    std::string toggle_mod = "F10";

    /// Toggle FSR 1 / RCAS upscaling live during gameplay (default "F11").
    std::string toggle_upscaler = "F11";

    /// Toggle M6 particle overdraw mitigation live during gameplay (default "F8").
    std::string toggle_particles = "F8";

    /// Toggle M5 frame-rate cap (`present.max_fps`) live during gameplay (default "F12").
    std::string toggle_frame_cap = "F12";
  } hotkeys;

  /// Settings for `SF_PROBE` draw-call and pass JSONL capture builds.
  struct Probe {
    /// Maximum frames to write per capture session (clamped to [1, 36000]).
    uint32_t capture_frames = 120;

    /// Output JSONL filename written alongside `dinput8.dll`.
    std::string output_file = "soulsframe_capture.jsonl";

    /// Record unique active D3D11 textures/render-targets alongside draw sites.
    bool trace_resources = true;

    /// Measure per-pass and per-frame GPU time with D3D11 timestamp queries (M3). Kill switch:
    /// set false if a driver misbehaves with queries; capture then omits `gpu_ms` fields.
    bool gpu_timing = true;

    /// Dump a read-only, Ghidra-aligned copy of the host executable from memory once at startup
    /// (M4 RE helper). Off by default; never writes or alters game memory.
    bool dump_module = false;

    /// Filename written alongside `dinput8.dll` when `dump_module = true`.
    std::string dump_file = "soulsframe_mem_dump.exe";
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
