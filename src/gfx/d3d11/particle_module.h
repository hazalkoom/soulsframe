#pragma once

#include "core/config.h"
#include "core/module.h"
#include "games/ds3/profile.h"

#include <cstdint>
#include <memory>

namespace sf::gfx {

/// Result of inspecting and applying Milestone 6 particle mitigation to a D3D11 draw call.
struct DrawFilterDecision {
  bool skip_draw = false;
  uint32_t effective_instance_count = 1;
  bool viewport_scaled = false;
};

/// Evaluates an active `ID3D11DeviceContext*` draw call against `ParticleModule` if enabled.
/// Used both by `ParticleModule`'s standalone MinHook detours and by `context_hook.cpp` in
/// `SF_PROBE` builds so `ProbeModule` and `ParticleModule` share one hook chain cleanly.
DrawFilterDecision filter_d3d11_draw(void* device_context, uint32_t vertex_or_index_count,
                                     uint32_t instance_count, bool is_instanced) noexcept;

/// Restores any temporary viewport scaling applied by `filter_d3d11_draw()` after the draw
/// completes.
void finish_d3d11_draw(void* device_context, const DrawFilterDecision& decision) noexcept;

/// Milestone 6 particle pass mitigation module (`[modules] particles`, `[particles]`).
///
/// Detects DS3-style transparent/additive particle passes (`BlendEnable && !DepthWriteMask`),
/// clamps instanced overdraw budgets, caps runaway particle draw counts per frame, and applies
/// low-resolution viewport scaling (`scale_percent`).
class ParticleModule final : public sf::Module {
public:
  ParticleModule();
  ~ParticleModule() override;

  const char* name() const override { return "particles"; }
  bool is_enabled(const sf::Config& cfg) const override {
    return cfg.modules.particles || (cfg.modules.overlay && cfg.hotkeys.enabled);
  }
  bool init(const sf::GameProfile& profile, const sf::Config& cfg) override;
  void on_present(sf::PresentCtx& ctx) override;
  void shutdown() override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace sf::gfx
