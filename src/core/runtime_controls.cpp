#include "core/runtime_controls.h"

#include "core/config.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace sf {

namespace {

struct KeyBindingDef {
  std::string_view name;
  uint32_t vk;
  std::string_view badge;
};

constexpr std::array<KeyBindingDef, 30> kKeyTable{{
    {.name = "f1", .vk = 0x70U, .badge = "F1"},
    {.name = "f2", .vk = 0x71U, .badge = "F2"},
    {.name = "f3", .vk = 0x72U, .badge = "F3"},
    {.name = "f4", .vk = 0x73U, .badge = "F4"},
    {.name = "f5", .vk = 0x74U, .badge = "F5"},
    {.name = "f6", .vk = 0x75U, .badge = "F6"},
    {.name = "f7", .vk = 0x76U, .badge = "F7"},
    {.name = "f8", .vk = 0x77U, .badge = "F8"},
    {.name = "f9", .vk = 0x78U, .badge = "F9"},
    {.name = "f10", .vk = 0x79U, .badge = "F10"},
    {.name = "f11", .vk = 0x7AU, .badge = "F11"},
    {.name = "f12", .vk = 0x7BU, .badge = "F12"},
    {.name = "insert", .vk = 0x2DU, .badge = "INS"},
    {.name = "delete", .vk = 0x2EU, .badge = "DEL"},
    {.name = "home", .vk = 0x24U, .badge = "HOME"},
    {.name = "end", .vk = 0x23U, .badge = "END"},
    {.name = "pageup", .vk = 0x21U, .badge = "PGUP"},
    {.name = "pagedown", .vk = 0x22U, .badge = "PGDN"},
    {.name = "pause", .vk = 0x13U, .badge = "PAUS"},
    {.name = "scrolllock", .vk = 0x91U, .badge = "SCRL"},
    {.name = "numpad0", .vk = 0x60U, .badge = "NUM0"},
    {.name = "numpad1", .vk = 0x61U, .badge = "NUM1"},
    {.name = "numpad2", .vk = 0x62U, .badge = "NUM2"},
    {.name = "numpad3", .vk = 0x63U, .badge = "NUM3"},
    {.name = "numpad4", .vk = 0x64U, .badge = "NUM4"},
    {.name = "numpad5", .vk = 0x65U, .badge = "NUM5"},
    {.name = "numpad6", .vk = 0x66U, .badge = "NUM6"},
    {.name = "numpad7", .vk = 0x67U, .badge = "NUM7"},
    {.name = "numpad8", .vk = 0x68U, .badge = "NUM8"},
    {.name = "numpad9", .vk = 0x69U, .badge = "NUM9"},
}};

std::string to_lower_ascii(std::string_view sv) {
  std::string out(sv);
  std::ranges::transform(out, out.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

const KeyBindingDef* find_key_binding(std::string_view lower) noexcept {
  const auto* const it = std::ranges::find_if(
      kKeyTable, [&](const KeyBindingDef& entry) { return entry.name == lower; });
  return (it != kKeyTable.end()) ? &(*it) : nullptr;
}

} // namespace

const char* hotkey_action_name(HotkeyAction action) noexcept {
  switch (action) {
  case HotkeyAction::ToggleOverlay:
    return "toggle_overlay";
  case HotkeyAction::ToggleMod:
    return "toggle_mod";
  case HotkeyAction::ToggleUpscaler:
    return "toggle_upscaler";
  case HotkeyAction::ToggleParticles:
    return "toggle_particles";
  case HotkeyAction::ToggleFrameCap:
    return "toggle_frame_cap";
  }
  return "unknown";
}

uint32_t parse_virtual_key(std::string_view name) noexcept {
  const std::string lower = to_lower_ascii(name);
  if (lower == "none") {
    return 0U;
  }
  const KeyBindingDef* entry = find_key_binding(lower);
  return (entry != nullptr) ? entry->vk : 0U;
}

bool is_valid_hotkey_name(std::string_view name) noexcept {
  const std::string lower = to_lower_ascii(name);
  if (lower == "none") {
    return true;
  }
  return parse_virtual_key(lower) != 0U;
}

std::string canonical_hotkey_label(std::string_view name) {
  const KeyBindingDef* entry = find_key_binding(to_lower_ascii(name));
  return (entry != nullptr) ? std::string(entry->badge) : "OFF";
}

void RuntimeControlState::reset_from_config(const Config& cfg) noexcept {
  mod_bypassed.store(false, std::memory_order_relaxed);
  overlay_visible.store(cfg.overlay.show_fps || cfg.overlay.show_status, std::memory_order_relaxed);
  upscaler_active.store(cfg.modules.upscaler, std::memory_order_relaxed);
  particles_active.store(cfg.modules.particles, std::memory_order_relaxed);
  frame_cap_active.store(cfg.modules.presentation && cfg.present.max_fps > 0U,
                         std::memory_order_relaxed);
}

bool RuntimeControlState::apply_action(HotkeyAction action) noexcept {
  auto flip = [](std::atomic<bool>& flag) noexcept {
    const bool prev = flag.load(std::memory_order_relaxed);
    flag.store(!prev, std::memory_order_relaxed);
    return !prev;
  };

  switch (action) {
  case HotkeyAction::ToggleOverlay:
    return flip(overlay_visible);
  case HotkeyAction::ToggleMod: {
    const bool bypassed = flip(mod_bypassed);
    return !bypassed;
  }
  case HotkeyAction::ToggleUpscaler:
    return flip(upscaler_active);
  case HotkeyAction::ToggleParticles:
    return flip(particles_active);
  case HotkeyAction::ToggleFrameCap:
    return flip(frame_cap_active);
  }
  return false;
}

RuntimeControlState& runtime_controls() noexcept {
  static RuntimeControlState state;
  return state;
}

HotkeyTracker::HotkeyTracker() noexcept {
  reset();
}

void HotkeyTracker::reset() noexcept {
  prev_down_.fill(false);
  last_trigger_ms_.fill(-1e9);
}

std::vector<HotkeyAction> HotkeyTracker::poll(const std::array<bool, kHotkeyActionCount>& down,
                                              double now_ms, double debounce_ms) {
  std::vector<HotkeyAction> triggered;
  for (size_t i = 0U; i < kHotkeyActionCount; ++i) {
    if (down[i] && !prev_down_[i] && (now_ms - last_trigger_ms_[i]) >= debounce_ms) {
      last_trigger_ms_[i] = now_ms;
      triggered.push_back(static_cast<HotkeyAction>(i));
    }
    prev_down_[i] = down[i];
  }
  return triggered;
}

} // namespace sf
