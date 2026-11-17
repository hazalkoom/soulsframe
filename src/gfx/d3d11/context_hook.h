#pragma once

#include "gfx/d3d11/gpu_timer.h"
#include "probe/trace_types.h"

#include <cstdint>
#include <vector>

namespace sf::gfx {

/// Callback signature invoked on each traced immediate-context draw, dispatch, or clear call.
using DrawTraceCallback = void (*)(const probe::DrawCallState& state, void* user);

/// Callback signature invoked when a draw/clear references a D3D11 resource for capture cataloging.
using ResourceTraceCallback = void (*)(const probe::ResourceSummary& resource, void* user);

/// Returns the traced frame's running pass total so the GPU timer can stamp each new pass.
using PassCountCallback = uint32_t (*)(void* user);

/// Configuration and sink callbacks passed to `install_context_hooks()`.
struct ContextHookCallbacks {
  DrawTraceCallback on_draw = nullptr;
  ResourceTraceCallback on_resource = nullptr;
  PassCountCallback pass_count = nullptr;
  bool trace_resources = true;
  bool gpu_timing = false; ///< Enable D3D11 timestamp queries (needs `pass_count`).
  void* user = nullptr;
};

/// Discovers `ID3D11DeviceContext` virtual table slots via a temporary D3D11 device
/// and installs MinHook detours on draw, dispatch, and clear calls (probe builds only).
bool install_context_hooks(const ContextHookCallbacks& callbacks);

/// Closes the current GPU-timed frame on the swap chain's immediate context and returns every
/// earlier frame whose GPU results are now ready. Call once per Present from the render thread.
/// `swap_chain` is an opaque `IDXGISwapChain*`. Returns an empty list when GPU timing is off.
std::vector<GpuFrameResult> gpu_timing_present(void* swap_chain, uint64_t frame_index);

/// Disables and removes all `ID3D11DeviceContext` hooks installed by `install_context_hooks()`.
void remove_context_hooks();

} // namespace sf::gfx
