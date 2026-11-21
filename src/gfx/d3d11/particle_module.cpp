#include "gfx/d3d11/particle_module.h"

#include "core/log.h"
#include "core/runtime_controls.h"
#include "games/ds3/render.h"

#include <MinHook.h>
#include <d3d11.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace sf::gfx {

namespace {

constexpr size_t kSlotDrawIndexed = 12;
constexpr size_t kSlotDraw = 13;
constexpr size_t kSlotDrawIndexedInstanced = 20;
constexpr size_t kSlotDrawInstanced = 21;

using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using DrawFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT,
                                                        UINT);
using DrawInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);

struct StandaloneHookSlot {
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

std::array<StandaloneHookSlot, 4> g_slots{};
bool g_hooks_installed = false;
bool g_owns_mh_init = false;
bool g_shared_context_hooks = false;

std::atomic<bool> g_active{false};
std::mutex g_policy_mu;
sf::RenderProfile g_profile{};
sf::Config g_cfg = sf::Config::defaults();
sf::games::ds3::ParticlePassPolicy g_policy;

thread_local UINT t_saved_vp_count = 0;
thread_local std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE>
    t_saved_vps{};

void inspect_rtv_dims(ID3D11RenderTargetView* rtv, uint32_t* out_w, uint32_t* out_h) noexcept {
  if (rtv == nullptr) {
    return;
  }
  ID3D11Resource* res = nullptr;
  rtv->GetResource(&res);
  if (res == nullptr) {
    return;
  }
  D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
  res->GetType(&dim);
  if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
    ID3D11Texture2D* tex = nullptr;
    if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex))) &&
        tex != nullptr) {
      D3D11_TEXTURE2D_DESC desc{};
      tex->GetDesc(&desc);
      *out_w = static_cast<uint32_t>(desc.Width);
      *out_h = static_cast<uint32_t>(desc.Height);
      tex->Release();
    }
  }
  res->Release();
}

sf::games::ds3::ParticleDrawCandidate capture_candidate(ID3D11DeviceContext* ctx,
                                                        uint32_t vertex_or_index_count,
                                                        uint32_t instance_count,
                                                        bool is_instanced) noexcept {
  sf::games::ds3::ParticleDrawCandidate cand{};
  cand.vertex_or_index_count = vertex_or_index_count;
  cand.instance_count = instance_count;
  cand.is_instanced = is_instanced;

  std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs{};
  ID3D11DepthStencilView* dsv = nullptr;
  ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs.data(), &dsv);

  uint8_t rtv_count = 0;
  for (size_t i = 0; i < rtvs.size(); ++i) {
    if (rtvs[i] != nullptr) {
      ++rtv_count;
      if (i == 0U) {
        inspect_rtv_dims(rtvs[i], &cand.rt_width, &cand.rt_height);
      }
      rtvs[i]->Release();
    }
  }
  if (dsv != nullptr) {
    dsv->Release();
  }
  cand.rtv_count = rtv_count;

  ID3D11BlendState* bs = nullptr;
  std::array<FLOAT, 4> blend_factor{};
  UINT sample_mask = 0;
  ctx->OMGetBlendState(&bs, blend_factor.data(), &sample_mask);
  if (bs != nullptr) {
    D3D11_BLEND_DESC bdesc{};
    bs->GetDesc(&bdesc);
    cand.blend_enabled = (bdesc.RenderTarget[0].BlendEnable != FALSE);
    bs->Release();
  }

  ID3D11DepthStencilState* dss = nullptr;
  UINT stencil_ref = 0;
  ctx->OMGetDepthStencilState(&dss, &stencil_ref);
  if (dss != nullptr) {
    D3D11_DEPTH_STENCIL_DESC ddesc{};
    dss->GetDesc(&ddesc);
    cand.depth_enabled = (ddesc.DepthEnable != FALSE);
    cand.depth_write = (ddesc.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL);
    dss->Release();
  }

  ID3D11VertexShader* vs = nullptr;
  ctx->VSGetShader(&vs, nullptr, nullptr);
  cand.has_vertex_shader = (vs != nullptr);
  if (vs != nullptr) {
    vs->Release();
  }

  ID3D11PixelShader* ps = nullptr;
  ctx->PSGetShader(&ps, nullptr, nullptr);
  cand.has_pixel_shader = (ps != nullptr);
  if (ps != nullptr) {
    ps->Release();
  }

  return cand;
}

void STDMETHODCALLTYPE hooked_draw_indexed(ID3D11DeviceContext* ctx, UINT index_count,
                                           UINT start_index, INT base_vertex) {
  const auto decision = filter_d3d11_draw(ctx, static_cast<uint32_t>(index_count), 1U, false);
  if (decision.skip_draw) {
    return;
  }
  g_orig_draw_indexed(ctx, index_count, start_index, base_vertex);
  finish_d3d11_draw(ctx, decision);
}

void STDMETHODCALLTYPE hooked_draw(ID3D11DeviceContext* ctx, UINT vertex_count, UINT start_vertex) {
  const auto decision = filter_d3d11_draw(ctx, static_cast<uint32_t>(vertex_count), 1U, false);
  if (decision.skip_draw) {
    return;
  }
  g_orig_draw(ctx, vertex_count, start_vertex);
  finish_d3d11_draw(ctx, decision);
}

void STDMETHODCALLTYPE hooked_draw_indexed_instanced(ID3D11DeviceContext* ctx,
                                                     UINT index_count_per_inst, UINT instance_count,
                                                     UINT start_index, INT base_vertex,
                                                     UINT start_instance) {
  const auto decision = filter_d3d11_draw(ctx, static_cast<uint32_t>(index_count_per_inst),
                                          static_cast<uint32_t>(instance_count), true);
  if (decision.skip_draw) {
    return;
  }
  g_orig_draw_indexed_instanced(ctx, index_count_per_inst,
                                static_cast<UINT>(decision.effective_instance_count), start_index,
                                base_vertex, start_instance);
  finish_d3d11_draw(ctx, decision);
}

void STDMETHODCALLTYPE hooked_draw_instanced(ID3D11DeviceContext* ctx, UINT vertex_count_per_inst,
                                             UINT instance_count, UINT start_vertex,
                                             UINT start_instance) {
  const auto decision = filter_d3d11_draw(ctx, static_cast<uint32_t>(vertex_count_per_inst),
                                          static_cast<uint32_t>(instance_count), true);
  if (decision.skip_draw) {
    return;
  }
  g_orig_draw_instanced(ctx, vertex_count_per_inst,
                        static_cast<UINT>(decision.effective_instance_count), start_vertex,
                        start_instance);
  finish_d3d11_draw(ctx, decision);
}

bool install_standalone_hooks() {
  if (g_hooks_installed) {
    return true;
  }

  ID3D11Device* dev = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  D3D_FEATURE_LEVEL fl{};
  const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                       D3D11_SDK_VERSION, &dev, &fl, &ctx);
  if (FAILED(hr) || ctx == nullptr) {
    SF_LOG("ParticleModule: dummy D3D11CreateDevice failed (hr=0x%08lx)",
           static_cast<unsigned long>(hr));
    if (dev != nullptr) {
      dev->Release();
    }
    return false;
  }

  void** vtable = *reinterpret_cast<void***>(ctx);
  g_slots = {{
      {kSlotDrawIndexed, "DrawIndexed", vtable[kSlotDrawIndexed],
       reinterpret_cast<void*>(&hooked_draw_indexed),
       reinterpret_cast<void**>(&g_orig_draw_indexed), false, false},
      {kSlotDraw, "Draw", vtable[kSlotDraw], reinterpret_cast<void*>(&hooked_draw),
       reinterpret_cast<void**>(&g_orig_draw), false, false},
      {kSlotDrawIndexedInstanced, "DrawIndexedInstanced", vtable[kSlotDrawIndexedInstanced],
       reinterpret_cast<void*>(&hooked_draw_indexed_instanced),
       reinterpret_cast<void**>(&g_orig_draw_indexed_instanced), false, false},
      {kSlotDrawInstanced, "DrawInstanced", vtable[kSlotDrawInstanced],
       reinterpret_cast<void*>(&hooked_draw_instanced),
       reinterpret_cast<void**>(&g_orig_draw_instanced), false, false},
  }};

  ctx->Release();
  dev->Release();

  const MH_STATUS mh = MH_Initialize();
  if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED) {
    SF_LOG("ParticleModule: MH_Initialize failed (%d)", static_cast<int>(mh));
    return false;
  }
  g_owns_mh_init = (mh == MH_OK);

  size_t already_hooked = 0;
  for (auto& slot : g_slots) {
    const MH_STATUS status = MH_CreateHook(slot.target, slot.detour, slot.original);
    if (status == MH_ERROR_ALREADY_CREATED) {
      // Probe context_hook already detours these slots and invokes filter_d3d11_draw().
      ++already_hooked;
      continue;
    }
    if (status != MH_OK) {
      SF_LOG("ParticleModule: MH_CreateHook(%s) failed (%d)", slot.name, static_cast<int>(status));
      for (auto& rollback : g_slots) {
        if (rollback.enabled) {
          MH_DisableHook(rollback.target);
        }
        if (rollback.created) {
          MH_RemoveHook(rollback.target);
        }
        rollback = {};
      }
      if (g_owns_mh_init) {
        MH_Uninitialize();
        g_owns_mh_init = false;
      }
      return false;
    }
    slot.created = true;
    if (MH_EnableHook(slot.target) == MH_OK) {
      slot.enabled = true;
    }
  }

  g_shared_context_hooks = (already_hooked == g_slots.size());
  g_hooks_installed = true;
  if (g_shared_context_hooks) {
    SF_LOG("ParticleModule: cooperating via SF_PROBE shared ID3D11DeviceContext hooks");
  } else {
    SF_LOG("ParticleModule: standalone ID3D11DeviceContext draw hooks installed (4 slots)");
  }
  return true;
}

void remove_standalone_hooks() {
  if (!g_hooks_installed) {
    return;
  }
  for (auto& slot : g_slots) {
    if (slot.enabled && slot.target != nullptr) {
      MH_DisableHook(slot.target);
    }
    if (slot.created && slot.target != nullptr) {
      MH_RemoveHook(slot.target);
    }
    slot = {};
  }
  if (g_owns_mh_init) {
    MH_Uninitialize();
    g_owns_mh_init = false;
  }
  g_hooks_installed = false;
  g_shared_context_hooks = false;
}

} // namespace

DrawFilterDecision filter_d3d11_draw(void* device_context, uint32_t vertex_or_index_count,
                                     uint32_t instance_count, bool is_instanced) noexcept {
  DrawFilterDecision out{};
  out.effective_instance_count =
      is_instanced ? std::max<uint32_t>(1U, instance_count) : instance_count;
  t_saved_vp_count = 0;

  if (!g_active.load(std::memory_order_relaxed) || !sf::runtime_controls().should_run_particles() ||
      device_context == nullptr) {
    return out;
  }

  auto* ctx = static_cast<ID3D11DeviceContext*>(device_context);
  if (ctx->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
    return out;
  }

  const auto cand = capture_candidate(ctx, vertex_or_index_count, instance_count, is_instanced);

  sf::games::ds3::ParticleMitigationAction action{};
  {
    std::lock_guard lk(g_policy_mu);
    action = g_policy.evaluate_draw(g_profile, g_cfg, cand);
  }

  out.skip_draw = action.skip_draw;
  out.effective_instance_count = action.effective_instance_count;
  if (action.skip_draw || !action.scale_viewport) {
    return out;
  }

  UINT num_vps = static_cast<UINT>(t_saved_vps.size());
  ctx->RSGetViewports(&num_vps, t_saved_vps.data());
  if (num_vps > 0U && num_vps <= t_saved_vps.size()) {
    t_saved_vp_count = num_vps;
    auto scaled_vps = t_saved_vps;
    scaled_vps[0].Width = static_cast<FLOAT>(action.viewport.scaled_width);
    scaled_vps[0].Height = static_cast<FLOAT>(action.viewport.scaled_height);
    ctx->RSSetViewports(num_vps, scaled_vps.data());
    out.viewport_scaled = true;
  }
  return out;
}

void finish_d3d11_draw(void* device_context, const DrawFilterDecision& decision) noexcept {
  if (!decision.viewport_scaled || device_context == nullptr || t_saved_vp_count == 0U) {
    t_saved_vp_count = 0;
    return;
  }
  auto* ctx = static_cast<ID3D11DeviceContext*>(device_context);
  ctx->RSSetViewports(t_saved_vp_count, t_saved_vps.data());
  t_saved_vp_count = 0;
}

struct ParticleModule::Impl {
  bool active = false;
};

ParticleModule::ParticleModule() : impl_(std::make_unique<Impl>()) {}

ParticleModule::~ParticleModule() = default;

bool ParticleModule::init(const sf::GameProfile& profile, const sf::Config& cfg) {
  {
    std::lock_guard lk(g_policy_mu);
    g_profile = profile.render;
    g_cfg = cfg;
    g_policy = sf::games::ds3::ParticlePassPolicy{};
  }

  if (!install_standalone_hooks()) {
    return false;
  }

  g_active.store(true, std::memory_order_release);
  impl_->active = true;
  SF_LOG("ParticleModule active (mode=%s, scale_percent=%u, max_instances=%u, max_draws=%u)",
         cfg.particles.mode.c_str(), cfg.particles.scale_percent,
         cfg.particles.max_instances_per_draw, cfg.particles.max_draws_per_frame);
  return true;
}

void ParticleModule::on_present(sf::PresentCtx& ctx) {
  if (!impl_->active) {
    return;
  }
  std::lock_guard lk(g_policy_mu);
  g_policy.begin_frame(ctx.frame_index + 1U);
}

void ParticleModule::shutdown() {
  if (!impl_->active) {
    return;
  }
  impl_->active = false;
  g_active.store(false, std::memory_order_release);

  remove_standalone_hooks();

  sf::games::ds3::ParticleStats st{};
  {
    std::lock_guard lk(g_policy_mu);
    st = g_policy.stats();
  }
  SF_LOG("ParticleModule shutdown (frames=%llu, particle_draws=%llu, low_res=%llu, throttled=%llu, "
         "skipped=%llu, instances_culled=%llu, est_pixels_saved=%llu)",
         static_cast<unsigned long long>(st.frames_observed),
         static_cast<unsigned long long>(st.particle_draws),
         static_cast<unsigned long long>(st.low_res_draws),
         static_cast<unsigned long long>(st.throttled_draws),
         static_cast<unsigned long long>(st.skipped_draws),
         static_cast<unsigned long long>(st.instances_culled),
         static_cast<unsigned long long>(st.estimated_pixels_saved));
}

} // namespace sf::gfx
