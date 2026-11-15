#pragma once

namespace sf {
class ModuleRegistry;
}

namespace sf::gfx {

/// Locates `IDXGISwapChain::Present` (vtable slot 8) and `ResizeBuffers` (slot 13) via
/// a temporary dummy device, hooks them with MinHook, and dispatches frame events to `registry`.
bool install_swapchain_hooks(ModuleRegistry* registry);

/// Disables and unhooks the DXGI swap-chain hooks installed by `install_swapchain_hooks()`.
void remove_swapchain_hooks();

} // namespace sf::gfx
