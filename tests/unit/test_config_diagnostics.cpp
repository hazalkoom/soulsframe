#include "core/config.h"
#include "core/config_diagnostics.h"
#include "core/version.h"

#include <algorithm>
#include <doctest/doctest.h>
#include <filesystem>
#include <string_view>

namespace {

bool has_code(const std::vector<sf::ConfigDiagnostic>& diags, std::string_view code,
              sf::DiagnosticSeverity sev) {
  return std::ranges::any_of(
      diags, [&](const sf::ConfigDiagnostic& d) { return d.code == code && d.severity == sev; });
}

} // namespace

TEST_CASE("version constants and format_config_summary are well formed") {
  CHECK(sf::kVersionMajor == 1U);
  CHECK(sf::kVersionMinor == 0U);
  CHECK(sf::kVersionPatch == 0U);
  CHECK(std::string_view(sf::kVersionString) == "1.0.0");
  CHECK(std::string_view(sf::kProductName) == "SoulsFrame");

  CHECK(std::string_view(sf::severity_name(sf::DiagnosticSeverity::Info)) == "INFO");
  CHECK(std::string_view(sf::severity_name(sf::DiagnosticSeverity::Warning)) == "WARN");
  CHECK(std::string_view(sf::severity_name(sf::DiagnosticSeverity::Error)) == "ERROR");

  sf::Config cfg = sf::Config::defaults();
  CHECK(sf::format_config_summary(cfg) ==
        "log_level=info present.mode=game max_fps=0 modules=[frame_telemetry]");

  cfg.modules.frame_telemetry = false;
  CHECK(sf::format_config_summary(cfg) ==
        "log_level=info present.mode=game max_fps=0 modules=[none]");

  cfg.modules.presentation = true;
  cfg.modules.upscaler = true;
  CHECK(sf::format_config_summary(cfg) ==
        "log_level=info present.mode=game max_fps=0 modules=[presentation,upscaler]");
}

TEST_CASE("diagnose_config stays quiet on valid acknowledged setup") {
  sf::Config cfg = sf::Config::defaults();
  cfg.general.offline_acknowledged = true;
  cfg.modules.presentation = true;
  cfg.present.mode = "flip";
  cfg.present.max_fps = 60U;
  cfg.modules.upscaler = true;
  cfg.upscaler.preset = "quality";
  cfg.upscaler.scale_percent = 67U;

  const auto diags = sf::diagnose_config(cfg);
  CHECK(diags.empty());
}

TEST_CASE("diagnose_config reports all mismatch and safety codes") {
  // 1. Disabled mod suppresses offline warning.
  {
    sf::Config cfg = sf::Config::defaults();
    cfg.general.mod_enabled = false;
    const auto d = sf::diagnose_config(cfg);
    CHECK(has_code(d, "SF_CFG_MOD_DISABLED", sf::DiagnosticSeverity::Info));
    CHECK_FALSE(has_code(d, "SF_CFG_OFFLINE_UNACK", sf::DiagnosticSeverity::Warning));
  }

  // 2. Unacknowledged offline + present.mode="flip" while presentation=false + telemetry off.
  {
    sf::Config cfg = sf::Config::defaults();
    cfg.modules.frame_telemetry = false;
    cfg.present.mode = "flip";
    cfg.modules.particles = true;
    cfg.particles.mode = "throttle";
    const auto d = sf::diagnose_config(cfg);
    CHECK(has_code(d, "SF_CFG_OFFLINE_UNACK", sf::DiagnosticSeverity::Warning));
    CHECK(has_code(d, "SF_CFG_PRESENT_INACTIVE", sf::DiagnosticSeverity::Warning));
    CHECK(has_code(d, "SF_CFG_TELEMETRY_OFF_WITH_PRESENT_MODS", sf::DiagnosticSeverity::Info));
    CHECK(has_code(d, "SF_CFG_PARTICLES_THROTTLE_NOOP", sf::DiagnosticSeverity::Warning));
  }

  // 3. Upscaler mode=none, preset override, FPS unlock (unverified & verified), cap conflict,
  // probe,
  //    and textures diagnostics.
  {
    sf::Config cfg = sf::Config::defaults();
    cfg.general.offline_acknowledged = true;
    cfg.modules.upscaler = true;
    cfg.upscaler.mode = "none";
    cfg.upscaler.preset = "balanced";
    cfg.upscaler.scale_percent = 77U;
    cfg.modules.fps_unlock = true;
    cfg.fps_unlock.target_fps = 120U;
    cfg.modules.presentation = true;
    cfg.present.max_fps = 60U;
    cfg.modules.probe_tracer = true;
    cfg.modules.textures = true;
    cfg.textures.dump_keys = true;
    cfg.textures.pack_dir = "/abs/path/textures";

    const auto rel_diags =
        sf::diagnose_config(cfg, {.is_probe_build = false, .exe_hooks_allowed = false});
    CHECK(has_code(rel_diags, "SF_CFG_UPSCALER_MODE_NONE", sf::DiagnosticSeverity::Warning));
    CHECK(has_code(rel_diags, "SF_CFG_UPSCALER_PRESET_OVERRIDES_SCALE",
                   sf::DiagnosticSeverity::Info));
    CHECK(has_code(rel_diags, "SF_CFG_FPS_UNLOCK_EXPERIMENTAL", sf::DiagnosticSeverity::Warning));
    CHECK(has_code(rel_diags, "SF_CFG_FPS_CAP_CONFLICT", sf::DiagnosticSeverity::Warning));
    CHECK(has_code(rel_diags, "SF_CFG_PROBE_ON_RELEASE", sf::DiagnosticSeverity::Warning));
    CHECK(has_code(rel_diags, "SF_CFG_TEXTURES_DUMP_KEYS", sf::DiagnosticSeverity::Info));
    CHECK(has_code(rel_diags, "SF_CFG_TEXTURES_PACK_DIR_ABS", sf::DiagnosticSeverity::Warning));

    const auto probe_diags =
        sf::diagnose_config(cfg, {.is_probe_build = true, .exe_hooks_allowed = true});
    CHECK_FALSE(has_code(probe_diags, "SF_CFG_PROBE_ON_RELEASE", sf::DiagnosticSeverity::Warning));
    CHECK(has_code(probe_diags, "SF_CFG_FPS_UNLOCK_EXPERIMENTAL", sf::DiagnosticSeverity::Warning));
  }
}

TEST_CASE("diagnose_config covers every diagnostic code and preset branch") {
  sf::Config cfg = sf::Config::defaults();
  cfg.general.mod_enabled = true;
  cfg.general.offline_acknowledged = false;
  cfg.modules.frame_telemetry = false;
  cfg.modules.presentation = true;
  cfg.present.mode = "flip";
  cfg.present.max_fps = 60U;
  cfg.modules.particles = true;
  cfg.particles.mode = "throttle";
  cfg.particles.max_instances_per_draw = 0U;
  cfg.particles.max_draws_per_frame = 0U;
  cfg.modules.upscaler = true;
  cfg.upscaler.mode = "none";
  cfg.upscaler.preset = "quality";
  cfg.upscaler.scale_percent = 77U;
  cfg.modules.fps_unlock = true;
  cfg.fps_unlock.target_fps = 120U;
  cfg.modules.probe_tracer = true;
  cfg.modules.textures = true;
  cfg.textures.dump_keys = true;
  cfg.textures.pack_dir = "C:\\textures";

  const auto verified =
      sf::diagnose_config(cfg, {.is_probe_build = false, .exe_hooks_allowed = true});
  CHECK(has_code(verified, "SF_CFG_OFFLINE_UNACK", sf::DiagnosticSeverity::Warning));
  CHECK(has_code(verified, "SF_CFG_TELEMETRY_OFF_WITH_PRESENT_MODS", sf::DiagnosticSeverity::Info));
  CHECK(has_code(verified, "SF_CFG_PARTICLES_THROTTLE_NOOP", sf::DiagnosticSeverity::Warning));
  CHECK(has_code(verified, "SF_CFG_UPSCALER_MODE_NONE", sf::DiagnosticSeverity::Warning));
  CHECK(has_code(verified, "SF_CFG_UPSCALER_PRESET_OVERRIDES_SCALE", sf::DiagnosticSeverity::Info));
  CHECK(has_code(verified, "SF_CFG_FPS_UNLOCK_EXPERIMENTAL", sf::DiagnosticSeverity::Warning));
  CHECK(has_code(verified, "SF_CFG_FPS_CAP_CONFLICT", sf::DiagnosticSeverity::Warning));
  CHECK(has_code(verified, "SF_CFG_PROBE_ON_RELEASE", sf::DiagnosticSeverity::Warning));
  CHECK(has_code(verified, "SF_CFG_TEXTURES_DUMP_KEYS", sf::DiagnosticSeverity::Info));
  CHECK(has_code(verified, "SF_CFG_TEXTURES_PACK_DIR_ABS", sf::DiagnosticSeverity::Warning));

  const auto unverified =
      sf::diagnose_config(cfg, {.is_probe_build = true, .exe_hooks_allowed = false});
  CHECK_FALSE(has_code(unverified, "SF_CFG_PROBE_ON_RELEASE", sf::DiagnosticSeverity::Warning));
  CHECK(has_code(unverified, "SF_CFG_FPS_UNLOCK_EXPERIMENTAL", sf::DiagnosticSeverity::Warning));

  cfg.upscaler.preset = "custom";
  cfg.upscaler.scale_percent = 53U;
  const auto custom = sf::diagnose_config(cfg);
  CHECK_FALSE(
      has_code(custom, "SF_CFG_UPSCALER_PRESET_OVERRIDES_SCALE", sf::DiagnosticSeverity::Info));

  cfg.upscaler.preset = "unknown-preset";
  const auto unknown = sf::diagnose_config(cfg);
  CHECK_FALSE(
      has_code(unknown, "SF_CFG_UPSCALER_PRESET_OVERRIDES_SCALE", sf::DiagnosticSeverity::Info));

  // Duplicate hotkey bindings and hidden overlay with hotkeys disabled.
  {
    sf::Config ov_cfg = sf::Config::defaults();
    ov_cfg.general.offline_acknowledged = true;
    ov_cfg.modules.overlay = true;
    ov_cfg.hotkeys.toggle_upscaler = "F9";
    const auto dup = sf::diagnose_config(ov_cfg);
    CHECK(has_code(dup, "SF_CFG_HOTKEYS_DUPLICATE", sf::DiagnosticSeverity::Warning));

    ov_cfg.overlay.show_fps = false;
    ov_cfg.overlay.show_status = false;
    ov_cfg.hotkeys.enabled = false;
    const auto hidden = sf::diagnose_config(ov_cfg);
    CHECK(has_code(hidden, "SF_CFG_OVERLAY_HIDDEN_NO_HOTKEYS", sf::DiagnosticSeverity::Info));
  }
}

TEST_CASE("bundled config template and presets stay safe and parse without warnings") {
  namespace fs = std::filesystem;
  const fs::path cwd = fs::current_path();
  fs::path repo_root;
  for (fs::path cur = cwd; !cur.empty(); cur = cur.parent_path()) {
    if (fs::exists(cur / "config" / "soulsframe.toml.example")) {
      repo_root = cur;
      break;
    }
    if (cur == cur.parent_path()) {
      break;
    }
  }

  if (!repo_root.empty()) {
    const fs::path tmpls[] = {
        repo_root / "config" / "soulsframe.toml.example",
        repo_root / "config" / "presets" / "safe-frame-pacing.toml",
        repo_root / "config" / "presets" / "low-end-gpu.toml",
        repo_root / "config" / "presets" / "probe-capture.toml",
    };
    for (const auto& p : tmpls) {
      REQUIRE(fs::exists(p));
      const auto res = sf::load_config_file(p.string());
      CHECK(res.parsed_ok);
      CHECK(res.warnings.empty());
      CHECK_FALSE(res.config.general.offline_acknowledged);
    }
  }
}
