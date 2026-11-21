#pragma once

#include "core/config.h"
#include "core/module.h"

#include <memory>

namespace sf::gfx {

/// Milestone 7 spatial upscaler & RCAS sharpening module (`[modules] upscaler`, `[upscaler]`).
///
/// Compiles a lightweight SM 4.0 fullscreen pass (EASU-inspired spatial filter + RCAS
/// contrast-adaptive sharpening) via `D3DCompile` at runtime and applies it safely during
/// `on_present()` on non-MSAA swap-chain back buffers.
class UpscalerModule final : public sf::Module {
public:
  UpscalerModule();
  ~UpscalerModule() override;

  const char* name() const override { return "upscaler"; }
  bool is_enabled(const sf::Config& cfg) const override { return cfg.modules.upscaler; }
  bool init(const sf::GameProfile& profile, const sf::Config& cfg) override;
  void on_present(sf::PresentCtx& ctx) override;
  void on_resize() override;
  void shutdown() override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace sf::gfx
