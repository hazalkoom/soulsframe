#pragma once

#include "core/config.h"
#include "core/module.h"

#include <memory>

namespace sf::gfx {

/// In-game D3D11 FPS viewer & hotkey status HUD (`[modules] overlay`, `[overlay]`, `[hotkeys]`).
///
/// Polls configured keyboard hotkeys once per `Present()` (debounced rising-edge detection) so
/// users can show/hide the HUD and pause/resume mod features live without restarting the game.
class OverlayModule final : public sf::Module {
public:
  OverlayModule();
  ~OverlayModule() override;

  const char* name() const override { return "overlay"; }
  bool is_enabled(const sf::Config& cfg) const override { return cfg.modules.overlay; }
  bool init(const sf::GameProfile& profile, const sf::Config& cfg) override;
  void on_present(sf::PresentCtx& ctx) override;
  void on_resize() override;
  void shutdown() override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace sf::gfx
