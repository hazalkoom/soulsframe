#include "core/config_diagnostics.h"

#include "core/runtime_controls.h"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <utility>

namespace sf {

namespace {

uint32_t expected_preset_scale_percent(std::string_view preset) noexcept {
  struct PresetScale {
    std::string_view name;
    uint32_t scale;
  };
  constexpr std::array<PresetScale, 5> kTable{{
      {.name = "native", .scale = 100U},
      {.name = "ultra_quality", .scale = 77U},
      {.name = "quality", .scale = 67U},
      {.name = "balanced", .scale = 59U},
      {.name = "performance", .scale = 50U},
  }};
  const auto it =
      std::ranges::find_if(kTable, [&](const PresetScale& entry) { return entry.name == preset; });
  return (it != kTable.end()) ? it->scale : 0U;
}

} // namespace

const char* severity_name(DiagnosticSeverity s) noexcept {
  switch (s) {
  case DiagnosticSeverity::Info:
    return "INFO";
  case DiagnosticSeverity::Warning:
    return "WARN";
  case DiagnosticSeverity::Error:
    return "ERROR";
  }
  return "INFO";
}

std::vector<ConfigDiagnostic> diagnose_config(const Config& cfg, const DiagnosticContext& ctx) {
  std::vector<ConfigDiagnostic> diags;

  if (!cfg.general.mod_enabled) {
    diags.push_back({.severity = DiagnosticSeverity::Info,
                     .code = "SF_CFG_MOD_DISABLED",
                     .message = "general.mod_enabled is false; all SoulsFrame modules remain "
                                "inactive."});
  }

  if (cfg.general.mod_enabled && !cfg.general.offline_acknowledged) {
    diags.push_back({.severity = DiagnosticSeverity::Warning,
                     .code = "SF_CFG_OFFLINE_UNACK",
                     .message = "general.offline_acknowledged is false; runtime hooks will be "
                                "blocked until you explicitly opt in for offline play."});
  }

  if (!cfg.modules.presentation && (cfg.present.mode == "flip" || cfg.present.max_fps > 0U)) {
    diags.push_back({.severity = DiagnosticSeverity::Warning,
                     .code = "SF_CFG_PRESENT_INACTIVE",
                     .message = "[present] configures flip/max_fps, but modules.presentation is "
                                "false so presentation policy and frame pacing will not run."});
  }

  const bool any_present_consumer = cfg.modules.presentation || cfg.modules.particles ||
                                    cfg.modules.upscaler || cfg.modules.fps_unlock ||
                                    cfg.modules.probe_tracer || cfg.modules.overlay;
  if (!cfg.modules.frame_telemetry && any_present_consumer) {
    diags.push_back({.severity = DiagnosticSeverity::Info,
                     .code = "SF_CFG_TELEMETRY_OFF_WITH_PRESENT_MODS",
                     .message = "modules.frame_telemetry is false while other Present()-driven "
                                "modules are enabled; swapchain hooks remain active silently."});
  }

  if (cfg.modules.particles && cfg.particles.mode == "throttle" &&
      cfg.particles.max_instances_per_draw == 0U && cfg.particles.max_draws_per_frame == 0U) {
    diags.push_back({.severity = DiagnosticSeverity::Warning,
                     .code = "SF_CFG_PARTICLES_THROTTLE_NOOP",
                     .message = "particles.mode is \"throttle\" with both max_instances_per_draw "
                                "and max_draws_per_frame set to 0 (unlimited); no throttling will "
                                "occur."});
  }

  if (cfg.modules.upscaler && cfg.upscaler.mode == "none") {
    diags.push_back({.severity = DiagnosticSeverity::Warning,
                     .code = "SF_CFG_UPSCALER_MODE_NONE",
                     .message = "modules.upscaler is true while upscaler.mode is \"none\"; "
                                "upscaling pass will be a no-op."});
  }

  if (cfg.modules.upscaler && cfg.upscaler.preset != "custom") {
    const uint32_t preset_scale = expected_preset_scale_percent(cfg.upscaler.preset);
    if (preset_scale != 0U && cfg.upscaler.scale_percent != preset_scale) {
      diags.push_back({.severity = DiagnosticSeverity::Info,
                       .code = "SF_CFG_UPSCALER_PRESET_OVERRIDES_SCALE",
                       .message = "upscaler.preset \"" + cfg.upscaler.preset + "\" uses " +
                                  std::to_string(preset_scale) + "% scale and overrides " +
                                  "upscaler.scale_percent (" +
                                  std::to_string(cfg.upscaler.scale_percent) + "%)."});
    }
  }

  if (cfg.modules.fps_unlock) {
    diags.push_back({.severity = DiagnosticSeverity::Warning,
                     .code = "SF_CFG_FPS_UNLOCK_EXPERIMENTAL",
                     .message = ctx.exe_hooks_allowed
                                    ? "modules.fps_unlock is experimental and may affect 60 Hz "
                                      "game simulation behavior."
                                    : "modules.fps_unlock is enabled on an unverified executable "
                                      "(running in safe dry-run mode without engine patches)."});
  }

  if (cfg.modules.fps_unlock && cfg.modules.presentation && cfg.present.max_fps > 0U &&
      cfg.present.max_fps < cfg.fps_unlock.target_fps) {
    diags.push_back({.severity = DiagnosticSeverity::Warning,
                     .code = "SF_CFG_FPS_CAP_CONFLICT",
                     .message = "present.max_fps (" + std::to_string(cfg.present.max_fps) +
                                ") caps output below fps_unlock.target_fps (" +
                                std::to_string(cfg.fps_unlock.target_fps) + ")."});
  }

  if (cfg.modules.probe_tracer && !ctx.is_probe_build) {
    diags.push_back({.severity = DiagnosticSeverity::Warning,
                     .code = "SF_CFG_PROBE_ON_RELEASE",
                     .message = "modules.probe_tracer is true, but this is not an SF_PROBE build; "
                                "draw-call tracing is unavailable in the release DLL."});
  }

  if (cfg.modules.textures && cfg.textures.dump_keys) {
    diags.push_back({.severity = DiagnosticSeverity::Info,
                     .code = "SF_CFG_TEXTURES_DUMP_KEYS",
                     .message = "textures.dump_keys is true; eligible texture hashes will be "
                                "logged to soulsframe.log (recommended off during normal play)."});
  }

  if (cfg.modules.textures && !cfg.textures.pack_dir.empty() &&
      (cfg.textures.pack_dir.front() == '/' || cfg.textures.pack_dir.front() == '\\' ||
       cfg.textures.pack_dir.find(':') != std::string::npos)) {
    diags.push_back({.severity = DiagnosticSeverity::Warning,
                     .code = "SF_CFG_TEXTURES_PACK_DIR_ABS",
                     .message = "textures.pack_dir appears to be an absolute/drive path (\"" +
                                cfg.textures.pack_dir +
                                "\"); use a folder name relative to dinput8.dll."});
  }

  if (cfg.modules.overlay && cfg.hotkeys.enabled) {
    const std::array<uint32_t, kHotkeyActionCount> vks{{
        parse_virtual_key(cfg.hotkeys.toggle_overlay),
        parse_virtual_key(cfg.hotkeys.toggle_mod),
        parse_virtual_key(cfg.hotkeys.toggle_upscaler),
        parse_virtual_key(cfg.hotkeys.toggle_particles),
        parse_virtual_key(cfg.hotkeys.toggle_frame_cap),
    }};
    bool dup_found = false;
    const auto vk_span = std::span{vks};
    for (size_t i = 0U; i < vks.size() && !dup_found; ++i) {
      if (vks[i] == 0U) {
        continue;
      }
      dup_found =
          std::ranges::any_of(vk_span.subspan(i + 1U), [&](uint32_t vk) { return vk == vks[i]; });
    }
    if (dup_found) {
      diags.push_back({.severity = DiagnosticSeverity::Warning,
                       .code = "SF_CFG_HOTKEYS_DUPLICATE",
                       .message = "Two or more [hotkeys] actions share the same key binding; "
                                  "both actions will fire on a single key press."});
    }
  }

  if (cfg.modules.overlay && !cfg.overlay.show_fps && !cfg.overlay.show_status &&
      !cfg.hotkeys.enabled) {
    diags.push_back({.severity = DiagnosticSeverity::Info,
                     .code = "SF_CFG_OVERLAY_HIDDEN_NO_HOTKEYS",
                     .message = "modules.overlay is true while show_fps, show_status, and "
                                "hotkeys.enabled are all false; overlay has no visible UI or "
                                "input."});
  }

  return diags;
}

std::string format_config_summary(const Config& cfg) {
  struct ModFlag {
    std::string_view name;
    bool enabled;
  };
  const std::array<ModFlag, 8> flags{{
      {.name = "frame_telemetry", .enabled = cfg.modules.frame_telemetry},
      {.name = "presentation", .enabled = cfg.modules.presentation},
      {.name = "probe_tracer", .enabled = cfg.modules.probe_tracer},
      {.name = "particles", .enabled = cfg.modules.particles},
      {.name = "upscaler", .enabled = cfg.modules.upscaler},
      {.name = "fps_unlock", .enabled = cfg.modules.fps_unlock},
      {.name = "textures", .enabled = cfg.modules.textures},
      {.name = "overlay", .enabled = cfg.modules.overlay},
  }};

  std::string mods;
  for (const auto& f : flags) {
    if (!f.enabled) {
      continue;
    }
    if (!mods.empty()) {
      mods += ',';
    }
    mods += f.name;
  }
  if (mods.empty()) {
    mods = "none";
  }

  return "log_level=" + cfg.general.log_level + " present.mode=" + cfg.present.mode +
         " max_fps=" + std::to_string(cfg.present.max_fps) + " modules=[" + mods + "]";
}

} // namespace sf
