#include "core/config.h"
#include "core/runtime_controls.h"

#include <array>
#include <doctest/doctest.h>
#include <string_view>

TEST_CASE("parse_virtual_key and hotkey labels handle supported and invalid keys") {
  CHECK(sf::parse_virtual_key("F8") == 0x77U);
  CHECK(sf::parse_virtual_key("f9") == 0x78U);
  CHECK(sf::parse_virtual_key("F10") == 0x79U);
  CHECK(sf::parse_virtual_key("F11") == 0x7AU);
  CHECK(sf::parse_virtual_key("F12") == 0x7BU);
  CHECK(sf::parse_virtual_key("Insert") == 0x2DU);
  CHECK(sf::parse_virtual_key("Delete") == 0x2EU);
  CHECK(sf::parse_virtual_key("Home") == 0x24U);
  CHECK(sf::parse_virtual_key("End") == 0x23U);
  CHECK(sf::parse_virtual_key("PageUp") == 0x21U);
  CHECK(sf::parse_virtual_key("PageDown") == 0x22U);
  CHECK(sf::parse_virtual_key("Pause") == 0x13U);
  CHECK(sf::parse_virtual_key("ScrollLock") == 0x91U);
  CHECK(sf::parse_virtual_key("Numpad0") == 0x60U);
  CHECK(sf::parse_virtual_key("numpad9") == 0x69U);
  CHECK(sf::parse_virtual_key("none") == 0U);
  CHECK(sf::parse_virtual_key("bad_key") == 0U);

  CHECK(sf::is_valid_hotkey_name("none"));
  CHECK(sf::is_valid_hotkey_name("F10"));
  CHECK_FALSE(sf::is_valid_hotkey_name("Escape"));

  CHECK(sf::canonical_hotkey_label("insert") == "INS");
  CHECK(sf::canonical_hotkey_label("PageUp") == "PGUP");
  CHECK(sf::canonical_hotkey_label("none") == "OFF");
  CHECK(sf::canonical_hotkey_label("invalid") == "OFF");

  CHECK(std::string_view(sf::hotkey_action_name(sf::HotkeyAction::ToggleOverlay)) ==
        "toggle_overlay");
  CHECK(std::string_view(sf::hotkey_action_name(sf::HotkeyAction::ToggleMod)) == "toggle_mod");
  CHECK(std::string_view(sf::hotkey_action_name(sf::HotkeyAction::ToggleUpscaler)) ==
        "toggle_upscaler");
  CHECK(std::string_view(sf::hotkey_action_name(sf::HotkeyAction::ToggleParticles)) ==
        "toggle_particles");
  CHECK(std::string_view(sf::hotkey_action_name(sf::HotkeyAction::ToggleFrameCap)) ==
        "toggle_frame_cap");
}

TEST_CASE("RuntimeControlState supports live toggles and master mod bypass") {
  sf::Config cfg = sf::Config::defaults();
  cfg.modules.overlay = true;
  cfg.modules.upscaler = true;
  cfg.modules.particles = false;
  cfg.modules.presentation = true;
  cfg.present.max_fps = 60U;

  auto& rc = sf::runtime_controls();
  rc.reset_from_config(cfg);

  CHECK(rc.is_mod_active());
  CHECK(rc.is_overlay_visible());
  CHECK(rc.should_run_upscaler());
  CHECK_FALSE(rc.should_run_particles());
  CHECK(rc.should_run_frame_cap());
  CHECK(rc.should_run_fps_unlock());

  // Toggle individual features live.
  CHECK(rc.apply_action(sf::HotkeyAction::ToggleParticles));
  CHECK(rc.should_run_particles());
  CHECK_FALSE(rc.apply_action(sf::HotkeyAction::ToggleUpscaler));
  CHECK_FALSE(rc.should_run_upscaler());
  CHECK_FALSE(rc.apply_action(sf::HotkeyAction::ToggleFrameCap));
  CHECK_FALSE(rc.should_run_frame_cap());

  // Master bypass pauses all optimization modules while preserving their saved toggle state.
  CHECK_FALSE(rc.apply_action(sf::HotkeyAction::ToggleMod));
  CHECK_FALSE(rc.is_mod_active());
  CHECK_FALSE(rc.should_run_particles());
  CHECK_FALSE(rc.should_run_upscaler());
  CHECK_FALSE(rc.should_run_frame_cap());
  CHECK_FALSE(rc.should_run_fps_unlock());

  // Un-bypassing restores active toggles cleanly.
  CHECK(rc.apply_action(sf::HotkeyAction::ToggleMod));
  CHECK(rc.is_mod_active());
  CHECK(rc.should_run_particles());
  CHECK_FALSE(rc.should_run_upscaler());

  // Overlay hide/show works independently.
  CHECK_FALSE(rc.apply_action(sf::HotkeyAction::ToggleOverlay));
  CHECK_FALSE(rc.is_overlay_visible());
  CHECK(rc.apply_action(sf::HotkeyAction::ToggleOverlay));
  CHECK(rc.is_overlay_visible());
}

TEST_CASE("HotkeyTracker enforces rising-edge triggering and debounce") {
  sf::HotkeyTracker tracker;
  const std::array<bool, sf::kHotkeyActionCount> idle{};
  const std::array<bool, sf::kHotkeyActionCount> press_mod{false, true, false, false, false};

  // First rising edge triggers immediately.
  auto fired = tracker.poll(press_mod, 0.0, 150.0);
  REQUIRE(fired.size() == 1U);
  CHECK(fired[0] == sf::HotkeyAction::ToggleMod);

  // Holding the key down across frames must NOT re-trigger.
  CHECK(tracker.poll(press_mod, 20.0, 150.0).empty());

  // Release then fast re-press inside debounce window (100 ms < 150 ms) is suppressed.
  CHECK(tracker.poll(idle, 50.0, 150.0).empty());
  CHECK(tracker.poll(press_mod, 100.0, 150.0).empty());

  // Release and press after debounce window elapsed (160 ms >= 150 ms) triggers again.
  CHECK(tracker.poll(idle, 120.0, 150.0).empty());
  fired = tracker.poll(press_mod, 160.0, 150.0);
  REQUIRE(fired.size() == 1U);
  CHECK(fired[0] == sf::HotkeyAction::ToggleMod);

  tracker.reset();
  fired = tracker.poll(press_mod, 460.0, 150.0);
  REQUIRE(fired.size() == 1U);
}
