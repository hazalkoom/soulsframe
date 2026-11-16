#pragma once

#include "probe/trace_types.h"

namespace sf::gfx {

/// Callback signature invoked on each traced immediate-context draw, dispatch, or clear call.
using DrawTraceCallback = void (*)(const probe::DrawCallState& state, void* user);

/// Callback signature invoked when a draw/clear references a D3D11 resource for capture cataloging.
using ResourceTraceCallback = void (*)(const probe::ResourceSummary& resource, void* user);

/// Configuration and sink callbacks passed to `install_context_hooks()`.
struct ContextHookCallbacks {
  DrawTraceCallback on_draw = nullptr;
  ResourceTraceCallback on_resource = nullptr;
  bool trace_resources = true;
  void* user = nullptr;
};

/// Discovers `ID3D11DeviceContext` virtual table slots via a temporary D3D11 device
/// and installs MinHook detours on draw, dispatch, and clear calls (probe builds only).
bool install_context_hooks(const ContextHookCallbacks& callbacks);

/// Disables and removes all `ID3D11DeviceContext` hooks installed by `install_context_hooks()`.
void remove_context_hooks();

} // namespace sf::gfx
