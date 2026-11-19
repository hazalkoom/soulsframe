#pragma once

#include <cstdint>

namespace sf {

struct Config;
struct GameProfile;

/// Per-frame context passed to active modules on every `IDXGISwapChain::Present` call.
struct PresentCtx {
  /// Monotonically increasing frame counter starting at 1.
  uint64_t frame_index = 0;

  /// Elapsed wall-clock time since the previous `Present` call, in milliseconds.
  double delta_ms = 0.0;

  /// Opaque `IDXGISwapChain*` pointer kept untyped so `core/` has zero D3D11/DXGI deps.
  void* swap_chain = nullptr;

  /// `Present` sync interval (0 = immediate, 1+ = vsync); modules may inspect or adjust it.
  uint32_t sync_interval = 0;

  /// `Present` flags bitmask (e.g. `DXGI_PRESENT_ALLOW_TEARING`); modules may adjust it.
  uint32_t present_flags = 0;
};

/// Lifecycle interface implemented by every SoulsFrame subsystem (telemetry, pacing, FSR, etc.).
struct Module {
  virtual ~Module() = default;

  /// Unique short identifier used in logs and status tables.
  virtual const char* name() const = 0;

  /// Returns true if the module patches/hooks game-specific code and must therefore
  /// remain disabled on unverified executable builds.
  virtual bool requires_engine_hooks() const { return false; }

  /// Evaluates the module's config kill switch.
  virtual bool is_enabled(const Config& cfg) const {
    (void)cfg;
    return true;
  }

  /// Installs hooks/state for this module. Returning false disables only this module.
  virtual bool init(const GameProfile& profile, const Config& cfg) = 0;

  /// Executed once per presented frame while the module is active.
  virtual void on_present(PresentCtx& ctx) { (void)ctx; }

  /// Removes hooks and frees resources during teardown.
  virtual void shutdown() = 0;
};

} // namespace sf
