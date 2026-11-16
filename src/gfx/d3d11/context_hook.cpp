#include "gfx/d3d11/context_hook.h"

#include "core/log.h"

#include <MinHook.h>
#include <d3d11.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace sf::gfx {

namespace {

// Verified ID3D11DeviceContext vtable slot indices (x64 D3D11 SDK / MinGW d3d11.h):
//   12 DrawIndexed
//   13 Draw
//   20 DrawIndexedInstanced
//   21 DrawInstanced
//   41 Dispatch
//   50 ClearRenderTargetView
//   53 ClearDepthStencilView
constexpr size_t kSlotDrawIndexed = 12;
constexpr size_t kSlotDraw = 13;
constexpr size_t kSlotDrawIndexedInstanced = 20;
constexpr size_t kSlotDrawInstanced = 21;
constexpr size_t kSlotDispatch = 41;
constexpr size_t kSlotClearRtv = 50;
constexpr size_t kSlotClearDsv = 53;

using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using DrawFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT,
                                                        UINT);
using DrawInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
using DispatchFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT);
using ClearRtvFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RenderTargetView*,
                                            const FLOAT[4]);
using ClearDsvFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT,
                                            FLOAT, UINT8);

struct HookSlot {
  size_t vtable_index = 0;
  const char* name = "";
  void* target = nullptr;
  void* detour = nullptr;
  void** original = nullptr;
  bool created = false;
  bool enabled = false;
};

DrawIndexedFn g_orig_draw_indexed = nullptr;
DrawFn g_orig_draw = nullptr;
DrawIndexedInstancedFn g_orig_draw_indexed_instanced = nullptr;
DrawInstancedFn g_orig_draw_instanced = nullptr;
DispatchFn g_orig_dispatch = nullptr;
ClearRtvFn g_orig_clear_rtv = nullptr;
ClearDsvFn g_orig_clear_dsv = nullptr;

ContextHookCallbacks g_callbacks{};
std::atomic<bool> g_hooks_active{false};
bool g_hooks_installed = false;
bool g_owns_mh_init = false;
uintptr_t g_host_exe_base = 0;

thread_local bool t_in_hook = false;

/// RAII re-entrancy guard preventing accidental infinite recursion inside hooked COM calls.
struct ReentrancyGuard {
  bool entered = false;
  ReentrancyGuard() noexcept {
    if (!t_in_hook) {
      t_in_hook = true;
      entered = true;
    }
  }
  ~ReentrancyGuard() noexcept {
    if (entered) {
      t_in_hook = false;
    }
  }
};

/// Mixes a 64-bit integer or COM pointer into an FNV-1a running digest.
uint64_t fnv1a_mix(uint64_t h, uint64_t value) noexcept {
  for (int i = 0; i < 8; ++i) {
    h ^= (value & 0xffULL);
    h *= 1099511628211ULL;
    value >>= 8;
  }
  return h;
}

/// Converts a raw return address into a host-executable-relative RVA when inside the main module.
uint64_t compute_caller_rva(void* ret_addr) noexcept {
  if (!ret_addr || g_host_exe_base == 0) {
    return 0;
  }
  const auto addr = reinterpret_cast<uintptr_t>(ret_addr);
  if (addr < g_host_exe_base) {
    return 0;
  }
  const uintptr_t diff = addr - g_host_exe_base;
  // Cap at 512 MiB so external runtime DLL addresses are not mistaken for host RVAs.
  if (diff >= 0x20000000ULL) {
    return 0;
  }
  return static_cast<uint64_t>(diff);
}

/// Extracts Texture2D dimensions and emits an optional `ResourceSummary` for a bound view.
void inspect_view_resource(ID3D11View* view, uint16_t* out_width, uint16_t* out_height) {
  if (!view) {
    return;
  }
  ID3D11Resource* res = nullptr;
  view->GetResource(&res);
  if (!res) {
    return;
  }

  ID3D11Texture2D* tex = nullptr;
  if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex))) &&
      tex) {
    D3D11_TEXTURE2D_DESC desc{};
    tex->GetDesc(&desc);
    if (out_width && *out_width == 0 && desc.Width <= 65535U) {
      *out_width = static_cast<uint16_t>(desc.Width);
    }
    if (out_height && *out_height == 0 && desc.Height <= 65535U) {
      *out_height = static_cast<uint16_t>(desc.Height);
    }
    if (g_callbacks.trace_resources && g_callbacks.on_resource) {
      probe::ResourceSummary summary;
      summary.kind = "Texture2D";
      summary.id_hash = fnv1a_mix(14695981039346656037ULL, reinterpret_cast<uintptr_t>(res));
      summary.width = desc.Width;
      summary.height = desc.Height;
      summary.format_or_stride = static_cast<uint32_t>(desc.Format);
      summary.bind_flags = desc.BindFlags;
      summary.mip_levels = desc.MipLevels;
      summary.sample_count = desc.SampleDesc.Count;
      summary.is_render_target = (desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0;
      g_callbacks.on_resource(summary, g_callbacks.user);
    }
    tex->Release();
  }
  res->Release();
}

/// Populates active render-target, blend, depth-write, and shader hashes from the D3D11 context.
probe::DrawCallState capture_pipeline_state(ID3D11DeviceContext* ctx, probe::DrawApi api,
                                            uint32_t elem_count, uint32_t instance_count,
                                            void* ret_addr) {
  probe::DrawCallState state;
  state.api = api;
  state.vertex_or_index_count = elem_count;
  state.instance_count = instance_count;
  state.caller_rva = compute_caller_rva(ret_addr);

  ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
  ID3D11DepthStencilView* dsv = nullptr;
  ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);

  uint64_t rtv_sig = 14695981039346656037ULL;
  uint8_t rtv_count = 0;
  for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
    if (rtvs[i]) {
      ++rtv_count;
      rtv_sig = fnv1a_mix(rtv_sig, reinterpret_cast<uintptr_t>(rtvs[i]));
      if (i == 0) {
        inspect_view_resource(rtvs[i], &state.rt_width, &state.rt_height);
      }
      rtvs[i]->Release();
    }
  }
  if (dsv) {
    rtv_sig = fnv1a_mix(rtv_sig, reinterpret_cast<uintptr_t>(dsv) ^ 0x9e3779b97f4a7c15ULL);
    if (state.rt_width == 0 || state.rt_height == 0) {
      inspect_view_resource(dsv, &state.rt_width, &state.rt_height);
    }
    dsv->Release();
  }
  state.rtv_count = rtv_count;
  state.rtv_signature = (rtv_count > 0 || dsv != nullptr) ? rtv_sig : 0;

  ID3D11BlendState* bs = nullptr;
  FLOAT blend_factor[4]{};
  UINT sample_mask = 0;
  ctx->OMGetBlendState(&bs, blend_factor, &sample_mask);
  if (bs) {
    D3D11_BLEND_DESC bdesc{};
    bs->GetDesc(&bdesc);
    state.blend_enabled = (bdesc.RenderTarget[0].BlendEnable != FALSE);
    bs->Release();
  }

  ID3D11DepthStencilState* dss = nullptr;
  UINT stencil_ref = 0;
  ctx->OMGetDepthStencilState(&dss, &stencil_ref);
  if (dss) {
    D3D11_DEPTH_STENCIL_DESC ddesc{};
    dss->GetDesc(&ddesc);
    state.depth_write =
        (ddesc.DepthEnable != FALSE && ddesc.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL);
    dss->Release();
  } else {
    state.depth_write = true;
  }

  ID3D11PixelShader* ps = nullptr;
  ctx->PSGetShader(&ps, nullptr, nullptr);
  if (ps) {
    state.ps_hash = fnv1a_mix(14695981039346656037ULL, reinterpret_cast<uintptr_t>(ps));
    ps->Release();
  }

  ID3D11VertexShader* vs = nullptr;
  ctx->VSGetShader(&vs, nullptr, nullptr);
  if (vs) {
    state.vs_hash = fnv1a_mix(14695981039346656037ULL, reinterpret_cast<uintptr_t>(vs));
    vs->Release();
  }

  return state;
}

/// Emits a captured draw or dispatch call when active and on the immediate D3D11 context.
void maybe_record_draw(ID3D11DeviceContext* ctx, probe::DrawApi api, uint32_t elem_count,
                       uint32_t instance_count, void* ret_addr) {
  if (!g_hooks_active.load(std::memory_order_relaxed) || !g_callbacks.on_draw || !ctx) {
    return;
  }
  ReentrancyGuard guard;
  if (!guard.entered || ctx->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
    return;
  }
  const probe::DrawCallState state =
      capture_pipeline_state(ctx, api, elem_count, instance_count, ret_addr);
  g_callbacks.on_draw(state, g_callbacks.user);
}

/// Emits a `ClearRtv` or `ClearDsv` boundary event with the cleared view's resource digest.
void maybe_record_clear(ID3D11DeviceContext* ctx, probe::DrawApi api, ID3D11View* view,
                        void* ret_addr) {
  if (!g_hooks_active.load(std::memory_order_relaxed) || !g_callbacks.on_draw || !ctx) {
    return;
  }
  ReentrancyGuard guard;
  if (!guard.entered || ctx->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
    return;
  }
  probe::DrawCallState state;
  state.api = api;
  state.caller_rva = compute_caller_rva(ret_addr);
  if (view) {
    state.rtv_signature = fnv1a_mix(14695981039346656037ULL, reinterpret_cast<uintptr_t>(view));
    state.rtv_count = (api == probe::DrawApi::ClearRtv) ? 1 : 0;
    inspect_view_resource(view, &state.rt_width, &state.rt_height);
  }
  g_callbacks.on_draw(state, g_callbacks.user);
}

void STDMETHODCALLTYPE hooked_draw_indexed(ID3D11DeviceContext* ctx, UINT index_count,
                                           UINT start_index, INT base_vertex) {
  maybe_record_draw(ctx, probe::DrawApi::DrawIndexed, index_count, 1, __builtin_return_address(0));
  g_orig_draw_indexed(ctx, index_count, start_index, base_vertex);
}

void STDMETHODCALLTYPE hooked_draw(ID3D11DeviceContext* ctx, UINT vertex_count, UINT start_vertex) {
  maybe_record_draw(ctx, probe::DrawApi::Draw, vertex_count, 1, __builtin_return_address(0));
  g_orig_draw(ctx, vertex_count, start_vertex);
}

void STDMETHODCALLTYPE hooked_draw_indexed_instanced(ID3D11DeviceContext* ctx,
                                                     UINT index_count_per_inst, UINT instance_count,
                                                     UINT start_index, INT base_vertex,
                                                     UINT start_instance) {
  maybe_record_draw(ctx, probe::DrawApi::DrawIndexedInstanced, index_count_per_inst, instance_count,
                    __builtin_return_address(0));
  g_orig_draw_indexed_instanced(ctx, index_count_per_inst, instance_count, start_index, base_vertex,
                                start_instance);
}

void STDMETHODCALLTYPE hooked_draw_instanced(ID3D11DeviceContext* ctx, UINT vertex_count_per_inst,
                                             UINT instance_count, UINT start_vertex,
                                             UINT start_instance) {
  maybe_record_draw(ctx, probe::DrawApi::DrawInstanced, vertex_count_per_inst, instance_count,
                    __builtin_return_address(0));
  g_orig_draw_instanced(ctx, vertex_count_per_inst, instance_count, start_vertex, start_instance);
}

void STDMETHODCALLTYPE hooked_dispatch(ID3D11DeviceContext* ctx, UINT x, UINT y, UINT z) {
  const uint32_t groups = x * y * z;
  maybe_record_draw(ctx, probe::DrawApi::Dispatch, groups, 1, __builtin_return_address(0));
  g_orig_dispatch(ctx, x, y, z);
}

void STDMETHODCALLTYPE hooked_clear_rtv(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv,
                                        const FLOAT rgba[4]) {
  maybe_record_clear(ctx, probe::DrawApi::ClearRtv, rtv, __builtin_return_address(0));
  g_orig_clear_rtv(ctx, rtv, rgba);
}

void STDMETHODCALLTYPE hooked_clear_dsv(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv,
                                        UINT clear_flags, FLOAT depth, UINT8 stencil) {
  maybe_record_clear(ctx, probe::DrawApi::ClearDsv, dsv, __builtin_return_address(0));
  g_orig_clear_dsv(ctx, dsv, clear_flags, depth, stencil);
}

std::array<HookSlot, 7>& hook_slots() noexcept {
  static std::array<HookSlot, 7> slots{{
      {kSlotDrawIndexed, "DrawIndexed", nullptr, reinterpret_cast<void*>(&hooked_draw_indexed),
       reinterpret_cast<void**>(&g_orig_draw_indexed), false, false},
      {kSlotDraw, "Draw", nullptr, reinterpret_cast<void*>(&hooked_draw),
       reinterpret_cast<void**>(&g_orig_draw), false, false},
      {kSlotDrawIndexedInstanced, "DrawIndexedInstanced", nullptr,
       reinterpret_cast<void*>(&hooked_draw_indexed_instanced),
       reinterpret_cast<void**>(&g_orig_draw_indexed_instanced), false, false},
      {kSlotDrawInstanced, "DrawInstanced", nullptr,
       reinterpret_cast<void*>(&hooked_draw_instanced),
       reinterpret_cast<void**>(&g_orig_draw_instanced), false, false},
      {kSlotDispatch, "Dispatch", nullptr, reinterpret_cast<void*>(&hooked_dispatch),
       reinterpret_cast<void**>(&g_orig_dispatch), false, false},
      {kSlotClearRtv, "ClearRenderTargetView", nullptr, reinterpret_cast<void*>(&hooked_clear_rtv),
       reinterpret_cast<void**>(&g_orig_clear_rtv), false, false},
      {kSlotClearDsv, "ClearDepthStencilView", nullptr, reinterpret_cast<void*>(&hooked_clear_dsv),
       reinterpret_cast<void**>(&g_orig_clear_dsv), false, false},
  }};
  return slots;
}

/// Cleans up created/enabled context hooks while leaving unrelated hooks intact.
void rollback_slots() {
  for (auto& slot : hook_slots()) {
    if (slot.enabled && slot.target) {
      MH_DisableHook(slot.target);
      slot.enabled = false;
    }
    if (slot.created && slot.target) {
      MH_RemoveHook(slot.target);
      slot.created = false;
    }
    slot.target = nullptr;
  }
  if (g_owns_mh_init) {
    MH_Uninitialize();
    g_owns_mh_init = false;
  }
}

} // namespace

bool install_context_hooks(const ContextHookCallbacks& callbacks) {
  if (g_hooks_installed) {
    g_callbacks = callbacks;
    g_hooks_active.store(true, std::memory_order_release);
    return true;
  }

  g_host_exe_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  g_callbacks = callbacks;

  ID3D11Device* dev = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  D3D_FEATURE_LEVEL fl{};
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                 D3D11_SDK_VERSION, &dev, &fl, &ctx);
  if (FAILED(hr) || !ctx) {
    // Fall back to WARP if hardware creation fails in a restricted CI container.
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                           &dev, &fl, &ctx);
  }
  if (FAILED(hr) || !ctx) {
    SF_LOG("dummy D3D11CreateDevice failed for context hooks: 0x%08lx",
           static_cast<unsigned long>(hr));
    return false;
  }

  void** vtable = *reinterpret_cast<void***>(ctx);
  for (auto& slot : hook_slots()) {
    slot.target = vtable[slot.vtable_index];
  }
  ctx->Release();
  dev->Release();

  const MH_STATUS init_st = MH_Initialize();
  if (init_st != MH_OK && init_st != MH_ERROR_ALREADY_INITIALIZED) {
    SF_LOG("MH_Initialize failed for context hooks (%d)", static_cast<int>(init_st));
    return false;
  }
  g_owns_mh_init = (init_st == MH_OK);

  for (auto& slot : hook_slots()) {
    if (MH_CreateHook(slot.target, slot.detour, slot.original) != MH_OK) {
      SF_LOG("MH_CreateHook(%s) failed", slot.name);
      rollback_slots();
      return false;
    }
    slot.created = true;
  }

  for (auto& slot : hook_slots()) {
    if (MH_EnableHook(slot.target) != MH_OK) {
      SF_LOG("MH_EnableHook(%s) failed", slot.name);
      rollback_slots();
      return false;
    }
    slot.enabled = true;
  }

  g_hooks_installed = true;
  g_hooks_active.store(true, std::memory_order_release);
  SF_LOG("ID3D11DeviceContext hooks installed (7 slots)");
  return true;
}

void remove_context_hooks() {
  g_hooks_active.store(false, std::memory_order_release);
  if (!g_hooks_installed) {
    return;
  }
  rollback_slots();
  g_hooks_installed = false;
  g_callbacks = {};
  SF_LOG("ID3D11DeviceContext hooks removed");
}

} // namespace sf::gfx
