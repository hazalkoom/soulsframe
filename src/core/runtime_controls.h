#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

struct Config;

/// Logical live-toggle actions controlled by in-game keyboard hotkeys.
enum class HotkeyAction : uint8_t {
  ToggleOverlay = 0,
  ToggleMod = 1,
  ToggleUpscaler = 2,
  ToggleParticles = 3,
  ToggleFrameCap = 4,
};

/// Total number of distinct hotkey actions in `HotkeyAction`.
inline constexpr size_t kHotkeyActionCount = 5U;

/// Returns a short human-readable identifier for a `HotkeyAction` (for logs and tests).
const char* hotkey_action_name(HotkeyAction action) noexcept;

/// Parses a key name (e.g. `"F9"`, `"Insert"`, `"Numpad0"`, `"none"`) case-insensitively into
/// its Win32 virtual-key code (`0` when `"none"` or unrecognized). Pure C++ (no `<windows.h>`).
uint32_t parse_virtual_key(std::string_view name) noexcept;

/// Returns true when `name` is `"none"` or a recognized hotkey name.
bool is_valid_hotkey_name(std::string_view name) noexcept;

/// Returns a compact uppercase HUD badge label for `name` (e.g. `"F9"`, `"INS"`, `"OFF"`).
std::string canonical_hotkey_label(std::string_view name);

/// Lock-free runtime state toggled live by keyboard hotkeys and polled by active modules.
struct RuntimeControlState {
  std::atomic<bool> mod_bypassed{false};
  std::atomic<bool> overlay_visible{true};
  std::atomic<bool> upscaler_active{false};
  std::atomic<bool> particles_active{false};
  std::atomic<bool> frame_cap_active{false};

  /// Initializes live flags from the loaded startup configuration.
  void reset_from_config(const Config& cfg) noexcept;

  /// Flips the flag corresponding to `action` and returns the new effective enabled/visible state.
  bool apply_action(HotkeyAction action) noexcept;

  /// True while the global mod bypass (`ToggleMod`) is NOT engaged.
  bool is_mod_active() const noexcept { return !mod_bypassed.load(std::memory_order_relaxed); }

  /// True when the in-game FPS/status HUD should currently be drawn.
  bool is_overlay_visible() const noexcept {
    return overlay_visible.load(std::memory_order_relaxed);
  }

  /// True when `UpscalerModule` should run its GPU pass on this frame.
  bool should_run_upscaler() const noexcept {
    return is_mod_active() && upscaler_active.load(std::memory_order_relaxed);
  }

  /// True when `ParticleModule` should apply draw filtering/low-res scaling.
  bool should_run_particles() const noexcept {
    return is_mod_active() && particles_active.load(std::memory_order_relaxed);
  }

  /// True when `PresentationModule` should apply the configured FPS limiter.
  bool should_run_frame_cap() const noexcept {
    return is_mod_active() && frame_cap_active.load(std::memory_order_relaxed);
  }

  /// True when `FpsUnlockModule` should advance its simulation stepper.
  bool should_run_fps_unlock() const noexcept { return is_mod_active(); }
};

/// Returns the process-wide `RuntimeControlState` singleton shared across hook modules.
RuntimeControlState& runtime_controls() noexcept;

/// Rising-edge + debounce tracker turning per-frame key-down polls into single toggle events.
class HotkeyTracker {
public:
  HotkeyTracker() noexcept;

  /// Clears pressed state and debounce timers.
  void reset() noexcept;

  /// Feeds current key-down states and returns newly triggered actions (debounced).
  std::vector<HotkeyAction> poll(const std::array<bool, kHotkeyActionCount>& down, double now_ms,
                                 double debounce_ms = 150.0);

private:
  std::array<bool, kHotkeyActionCount> prev_down_{};
  std::array<double, kHotkeyActionCount> last_trigger_ms_{};
};

} // namespace sf
