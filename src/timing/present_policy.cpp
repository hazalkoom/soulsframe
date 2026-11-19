#include "timing/present_policy.h"

#include <algorithm>

namespace sf::timing {

FlipUpgradeDecision evaluate_flip_upgrade(const Config& cfg, const SwapChainRequest& req) {
  FlipUpgradeDecision out{};
  out.new_buffer_count = req.buffer_count;
  out.new_flags = req.flags;

  if (!cfg.modules.presentation) {
    out.reason = "modules.presentation is disabled";
    return out;
  }

  if (cfg.present.mode != "flip") {
    out.reason = "present.mode is '" + cfg.present.mode + "' (not 'flip')";
    return out;
  }

  if (req.swap_effect == SwapEffectKind::FlipModel) {
    out.tearing_enabled = (req.flags & kSwapChainFlagAllowTearing) != 0U;
    out.reason = "swap chain already uses flip-model";
    return out;
  }

  if (!req.windowed) {
    out.reason = "exclusive fullscreen swap chain left untouched for safety";
    return out;
  }

  if (req.sample_count > 1U) {
    out.reason = "MSAA swap chain (sample_count=" + std::to_string(req.sample_count) +
                 ") is incompatible with DXGI flip-model";
    return out;
  }

  if (req.format != SwapFormatClass::FlipCompatible) {
    out.reason = "back-buffer format is not compatible with DXGI flip-model";
    return out;
  }

  out.upgrade = true;
  out.new_buffer_count = std::max<uint32_t>(2U, req.buffer_count);
  out.new_flags = req.flags;
  if (req.tearing_supported) {
    out.new_flags |= kSwapChainFlagAllowTearing;
    out.tearing_enabled = true;
    out.reason = "upgraded to FLIP_DISCARD (buffers=" + std::to_string(out.new_buffer_count) +
                 ", allow_tearing=true)";
  } else {
    out.reason = "upgraded to FLIP_DISCARD (buffers=" + std::to_string(out.new_buffer_count) +
                 ", allow_tearing=false)";
  }
  return out;
}

PresentCallAdjustment resolve_present_call(uint32_t sync_interval, uint32_t flags, bool flip_active,
                                           bool tearing_enabled, bool windowed) noexcept {
  PresentCallAdjustment adj{};
  adj.sync_interval = sync_interval;
  adj.flags = flags;

  const bool is_test_present = (flags & kPresentFlagTest) != 0U;
  if (flip_active && tearing_enabled && windowed && sync_interval == 0U && !is_test_present) {
    adj.flags |= kPresentFlagAllowTearing;
  }
  return adj;
}

} // namespace sf::timing
