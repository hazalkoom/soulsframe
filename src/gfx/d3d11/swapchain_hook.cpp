#include "gfx/d3d11/swapchain_hook.h"

#include "core/log.h"
#include "core/module_registry.h"
#include "core/win_helpers.h"
#include "timing/frame_stats.h"

#include <MinHook.h>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>

namespace sf::gfx {

namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT,
                                                    UINT);

PresentFn g_orig_present = nullptr;
ResizeBuffersFn g_orig_resize = nullptr;
void* g_present_target = nullptr;
void* g_resize_target = nullptr;
ModuleRegistry* g_registry = nullptr;

std::mutex g_stats_mu;
WinClock g_clock;
timing::FrameStats g_stats(120);
std::atomic<uint64_t> g_frame_index{0};
bool g_hooks_installed = false;

/// Intercepts `IDXGISwapChain::Present` (slot 8) to update rolling frame telemetry
/// and drive registered per-frame modules before forwarding to DXGI.
HRESULT STDMETHODCALLTYPE hooked_present(IDXGISwapChain* sc, UINT sync_interval, UINT flags) {
  double dt_ms = 0.0;
  uint64_t frame = 0;

  {
    std::lock_guard lk(g_stats_mu);
    dt_ms = g_stats.tick(g_clock);
    frame = ++g_frame_index;
    if (frame % 120 == 0 && g_stats.count() > 0) {
      const auto m = g_stats.compute_metrics();
      SF_LOG("avg frame time %.3f ms (%.1f fps) | p1_low %.3f ms | spikes %u", m.avg_ms, m.avg_fps,
             m.p1_low_ms, m.spike_count);
    }
  }

  if (g_registry) {
    PresentCtx ctx{frame, dt_ms, sc};
    g_registry->on_present(ctx);
  }

  return g_orig_present(sc, sync_interval, flags);
}

/// Intercepts `IDXGISwapChain::ResizeBuffers` (slot 13) so future modules can
/// release and recreate resolution-dependent render targets across resizes.
HRESULT STDMETHODCALLTYPE hooked_resize_buffers(IDXGISwapChain* sc, UINT buffer_count, UINT width,
                                                UINT height, DXGI_FORMAT new_format,
                                                UINT swapchain_flags) {
  SF_LOG("IDXGISwapChain::ResizeBuffers(%ux%u, buffers=%u, fmt=%u, flags=0x%x)", width, height,
         buffer_count, static_cast<unsigned>(new_format), swapchain_flags);
  return g_orig_resize(sc, buffer_count, width, height, new_format, swapchain_flags);
}

} // namespace

bool install_swapchain_hooks(ModuleRegistry* registry) {
  g_registry = registry;

  // Create a minimal hidden window + throwaway D3D11 device/swapchain solely to
  // discover DXGI's IDXGISwapChain vtable addresses before the game creates its own.
  WNDCLASSEXA wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_CLASSDC;
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = "sf_dummy_dxgi";
  RegisterClassExA(&wc);

  HWND hwnd = CreateWindowA(wc.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr,
                            nullptr, wc.hInstance, nullptr);
  if (!hwnd) {
    SF_LOG("CreateWindowA failed for dummy DXGI window (%lu)", GetLastError());
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return false;
  }

  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 1;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  IDXGISwapChain* sc = nullptr;
  ID3D11Device* dev = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  D3D_FEATURE_LEVEL fl{};

  const HRESULT hr =
      D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                    D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);

  if (FAILED(hr) || !sc) {
    SF_LOG("dummy D3D11CreateDeviceAndSwapChain failed: 0x%08lx", static_cast<unsigned long>(hr));
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return false;
  }

  // IDXGISwapChain inherits IUnknown (0..2) -> IDXGIObject (3..6) -> IDXGIDeviceSubObject (7)
  // -> Present is slot 8, ResizeBuffers is slot 13.
  void** vtable = *reinterpret_cast<void***>(sc);
  g_present_target = vtable[8];
  g_resize_target = vtable[13];

  sc->Release();
  ctx->Release();
  dev->Release();
  DestroyWindow(hwnd);
  UnregisterClassA(wc.lpszClassName, wc.hInstance);

  if (MH_Initialize() != MH_OK) {
    SF_LOG("MH_Initialize failed");
    return false;
  }

  if (MH_CreateHook(g_present_target, reinterpret_cast<void*>(&hooked_present),
                    reinterpret_cast<void**>(&g_orig_present)) != MH_OK) {
    SF_LOG("MH_CreateHook(Present) failed");
    MH_Uninitialize();
    return false;
  }

  if (MH_CreateHook(g_resize_target, reinterpret_cast<void*>(&hooked_resize_buffers),
                    reinterpret_cast<void**>(&g_orig_resize)) != MH_OK) {
    SF_LOG("MH_CreateHook(ResizeBuffers) failed");
    MH_RemoveHook(g_present_target);
    MH_Uninitialize();
    return false;
  }

  if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
    SF_LOG("MH_EnableHook failed");
    MH_RemoveHook(g_resize_target);
    MH_RemoveHook(g_present_target);
    MH_Uninitialize();
    return false;
  }

  g_hooks_installed = true;
  SF_LOG("Present hook installed at %p (ResizeBuffers at %p)", g_present_target, g_resize_target);
  return true;
}

void remove_swapchain_hooks() {
  if (!g_hooks_installed) {
    return;
  }
  MH_DisableHook(MH_ALL_HOOKS);
  if (g_resize_target) {
    MH_RemoveHook(g_resize_target);
  }
  if (g_present_target) {
    MH_RemoveHook(g_present_target);
  }
  MH_Uninitialize();
  g_hooks_installed = false;
  g_registry = nullptr;
}

} // namespace sf::gfx
