#pragma once

#include "core/config.h"
#include "core/module.h"
#include "timing/fixed_step.h"

#include <memory>

namespace sf::timing {

/// Milestone 8 experimental FPS unlock & fixed-step simulation accumulator module.
///
/// Controlled by `[modules] fps_unlock = true` and `[fps_unlock]`.
/// Enforces read-only dry-run telemetry whenever `profile.engine_hooks_allowed == false`
/// or required signature addresses (`frame_dt_ptr`, `frame_limiter`) are absent.
class FpsUnlockModule final : public sf::Module {
public:
  FpsUnlockModule();
  ~FpsUnlockModule() override;

  const char* name() const override { return "fps_unlock"; }
  bool is_enabled(const sf::Config& cfg) const override { return cfg.modules.fps_unlock; }
  bool init(const sf::GameProfile& profile, const sf::Config& cfg) override;
  void on_present(sf::PresentCtx& ctx) override;
  void shutdown() override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace sf::timing
