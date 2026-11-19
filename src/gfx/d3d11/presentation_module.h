#pragma once

#include "core/config.h"
#include "core/module.h"
#include "timing/frame_pacer.h"

#include <memory>

namespace sf::gfx {

/// Milestone 5 presentation & frame-pacing module.
///
/// Controlled by `[modules] presentation = true` and `[present]` (`mode`, `max_fps`):
/// - When `present.max_fps > 0`, paces `IDXGISwapChain::Present` with a hybrid sleep+spin
///   limiter (`sf::timing::FramePacer`) backed by `QueryPerformanceCounter` and
///   `timeBeginPeriod(1)`.
/// - When `present.mode = "flip"`, hooks `IDXGIFactory::CreateSwapChain` (vtable slot 10) to
///   upgrade compatible windowed BitBlt swap chains to `DXGI_SWAP_EFFECT_FLIP_DISCARD`, with an
///   automatic fallback to the game's original descriptor if DXGI rejects the upgrade.
class PresentationModule final : public sf::Module {
public:
  PresentationModule();
  ~PresentationModule() override;

  const char* name() const override { return "presentation"; }
  bool is_enabled(const sf::Config& cfg) const override { return cfg.modules.presentation; }
  bool init(const sf::GameProfile& profile, const sf::Config& cfg) override;
  void on_present(sf::PresentCtx& ctx) override;
  void shutdown() override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace sf::gfx
