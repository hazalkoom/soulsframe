#pragma once

#include "core/config.h"

#include <cstdint>
#include <string>

namespace sf::timing {

/// Pure, DXGI-header-free classification of back-buffer pixel formats for flip-model eligibility.
/// DXGI flip-model (`DXGI_SWAP_EFFECT_FLIP_DISCARD`) only supports a specific subset of formats
/// (`R8G8B8A8_UNORM`, `B8G8R8A8_UNORM`, `R16G16B16A16_FLOAT`, `R10G10B10A2_UNORM`).
enum class SwapFormatClass : uint8_t {
  Unsupported = 0,
  FlipCompatible = 1,
};

/// Pure, DXGI-header-free classification of the requested swap effect.
enum class SwapEffectKind : uint8_t {
  BitBlt = 0,    ///< Legacy `DXGI_SWAP_EFFECT_DISCARD` or `DXGI_SWAP_EFFECT_SEQUENTIAL`.
  FlipModel = 1, ///< Already `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL` or `FLIP_DISCARD`.
  Other = 2,     ///< Unknown or reserved swap effect.
};

/// DXGI swap-chain flag bits mirrored as pure constants so `src/timing/` never includes `<dxgi.h>`.
inline constexpr uint32_t kSwapChainFlagAllowTearing =
    0x00000800U; // DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
inline constexpr uint32_t kPresentFlagAllowTearing = 0x00000200U; // DXGI_PRESENT_ALLOW_TEARING
inline constexpr uint32_t kPresentFlagTest = 0x00000001U;         // DXGI_PRESENT_TEST

/// Pure summary of the game's `DXGI_SWAP_CHAIN_DESC` passed to `IDXGIFactory::CreateSwapChain`.
struct SwapChainRequest {
  uint32_t buffer_count = 1U;
  uint32_t sample_count = 1U;
  bool windowed = true;
  SwapEffectKind swap_effect = SwapEffectKind::BitBlt;
  SwapFormatClass format = SwapFormatClass::FlipCompatible;
  uint32_t flags = 0U;
  bool tearing_supported = false;
};

/// Outcome of `evaluate_flip_upgrade()`: instructs the D3D11 hook whether to rewrite the
/// swap-chain descriptor to `DXGI_SWAP_EFFECT_FLIP_DISCARD` or leave it untouched.
struct FlipUpgradeDecision {
  /// True when the swap-chain descriptor should be upgraded to `FLIP_DISCARD`.
  bool upgrade = false;

  /// Target `BufferCount` to use when `upgrade == true` (at least 2 for flip-model).
  uint32_t new_buffer_count = 1U;

  /// Target `Flags` bitmask to use when `upgrade == true`.
  uint32_t new_flags = 0U;

  /// True when tearing (`DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING`) was enabled on the swap chain.
  bool tearing_enabled = false;

  /// Short diagnostic explanation logged to `soulsframe.log`.
  std::string reason;
};

/// Adjusted `IDXGISwapChain::Present` arguments computed by `resolve_present_call()`.
struct PresentCallAdjustment {
  uint32_t sync_interval = 0U;
  uint32_t flags = 0U;
};

/// Evaluates whether a game's swap-chain creation request can be safely upgraded to
/// DXGI flip-model (`DXGI_SWAP_EFFECT_FLIP_DISCARD`) under the active `Config`.
FlipUpgradeDecision evaluate_flip_upgrade(const Config& cfg, const SwapChainRequest& req);

/// Computes safe `Present(sync_interval, flags)` parameters for an active swap chain.
/// When `flip_active && tearing_enabled && windowed && sync_interval == 0` (and not a
/// `DXGI_PRESENT_TEST` probe), adds `DXGI_PRESENT_ALLOW_TEARING` to unlock lowest latency.
PresentCallAdjustment resolve_present_call(uint32_t sync_interval, uint32_t flags, bool flip_active,
                                           bool tearing_enabled, bool windowed) noexcept;

} // namespace sf::timing
