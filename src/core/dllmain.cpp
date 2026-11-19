#include <windows.h>

#include "core/config.h"
#include "core/log.h"
#include "core/module_registry.h"
#include "core/offline_guard.h"
#include "core/win_helpers.h"
#include "games/ds3/profile.h"
#include "gfx/d3d11/presentation_module.h"
#include "gfx/d3d11/swapchain_hook.h"

#ifdef SF_PROBE
#include "probe/probe_module.h"
#endif

namespace {

HMODULE g_self = nullptr;
sf::ModuleRegistry g_registry;

/// Built-in Step-1 module responsible for installing the DXGI `Present` /
/// `ResizeBuffers` hooks and reporting rolling frame-time summaries.
class FrameTelemetryModule final : public sf::Module {
public:
  const char* name() const override { return "frame_telemetry"; }
  bool is_enabled(const sf::Config& cfg) const override { return cfg.modules.frame_telemetry; }
  bool init(const sf::GameProfile&, const sf::Config&) override {
    return sf::gfx::install_swapchain_hooks(&g_registry);
  }
  void shutdown() override { sf::gfx::remove_swapchain_hooks(); }
};

/// Background startup routine executed outside the Windows loader lock.
/// Doing D3D11/DXGI device creation or LoadLibrary calls inside DllMain directly
/// risks deadlocks; spawning a short-lived init thread avoids loader-lock contention.
DWORD WINAPI init_thread(LPVOID) {
  sf::log::init(g_self);
#ifdef SF_PROBE
  SF_LOG("SoulsFrame starting (probe build)");
#else
  SF_LOG("SoulsFrame starting (release build)");
#endif

  const std::string cfg_path = sf::log::module_dir(g_self) + "\\soulsframe.toml";
  const auto cfg_res = sf::load_config_file(cfg_path);
  for (const auto& w : cfg_res.warnings) {
    SF_LOG("config warning: %s", w.c_str());
  }
  const sf::Config& cfg = cfg_res.config;

  if (!cfg.general.mod_enabled) {
    SF_LOG("Mod disabled by general.mod_enabled = false");
    return 0;
  }

  // Enforce explicit offline acknowledgement before installing any hooks.
  sf::OfflineCheckInput offline_in;
  offline_in.config_offline_acknowledged = cfg.general.offline_acknowledged;
  offline_in.command_line_args = sf::get_process_args();
  const auto verdict = sf::evaluate_offline_guard(offline_in);
  if (!verdict.allowed) {
    SF_LOG("OFFLINE GUARD BLOCKED MOD INIT: %s", verdict.reason.c_str());
    return 0;
  }

  for (const auto& overlay : sf::detect_known_overlays()) {
    SF_LOG("warning: detected overlay/injector module loaded: %s", overlay.c_str());
  }

  const std::string exe_sha = sf::compute_host_exe_sha256();
  SF_LOG("host executable SHA-256: %s", exe_sha.empty() ? "<unknown>" : exe_sha.c_str());

  const auto profile = sf::games::ds3::resolve_profile(exe_sha);
  SF_LOG("profile=%s engine_hooks_allowed=%s", profile.id.c_str(),
         profile.engine_hooks_allowed ? "true" : "false");

  g_registry.register_module(std::make_unique<FrameTelemetryModule>());
  g_registry.register_module(std::make_unique<sf::gfx::PresentationModule>());
#ifdef SF_PROBE
  g_registry.register_module(
      std::make_unique<sf::probe::ProbeModule>(sf::log::module_dir(g_self), exe_sha));
#endif
  g_registry.init_all(profile, cfg);

  for (const auto& st : g_registry.statuses()) {
    SF_LOG("module %-20s : %s (%s)", st.name.c_str(), st.active ? "ACTIVE" : "INACTIVE",
           st.detail.c_str());
  }

  return 0;
}

} // namespace

/// Windows DLL entry point. Spawns `init_thread` on attach and tears down hooks on detach.
BOOL APIENTRY DllMain(HINSTANCE h, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_self = h;
    // Suppress DLL_THREAD_ATTACH/DETACH notifications to avoid per-thread overhead.
    DisableThreadLibraryCalls(h);
    HANDLE th = CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr);
    if (th) {
      CloseHandle(th);
    }
  } else if (reason == DLL_PROCESS_DETACH) {
    g_registry.shutdown_all();
    sf::log::shutdown();
  }
  return TRUE;
}
