#include "gfx/d3d11/overlay_module.h"

#include "core/log.h"
#include "core/runtime_controls.h"
#include "gfx/fx/hud_overlay.h"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace sf::gfx {

namespace {

constexpr const char* kOverlayVsHlsl = R"(
struct VSOut {
  float4 pos : SV_POSITION;
  float2 uv  : TEXCOORD0;
};
VSOut main(uint id : SV_VertexID) {
  VSOut o;
  float2 p = float2((id << 1) & 2, id & 2);
  o.pos = float4(p * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
  o.uv  = p;
  return o;
}
)";

constexpr const char* kOverlayPsHlsl = R"(
Texture2D<float4> hud_tex : register(t0);
SamplerState hud_samp : register(s0);
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
  return hud_tex.Sample(hud_samp, uv);
}
)";

template <typename T> void safe_release(T*& ptr) noexcept {
  if (ptr != nullptr) {
    ptr->Release();
    ptr = nullptr;
  }
}

using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR,
                                      UINT, UINT, ID3DBlob**, ID3DBlob**);

} // namespace

struct OverlayModule::Impl {
  sf::Config cfg = sf::Config::defaults();
  std::array<uint32_t, sf::kHotkeyActionCount> hotkeys_vk{};
  sf::HotkeyTracker tracker;
  double elapsed_ms = 0.0;

  bool active = false;
  bool pipeline_failed = false;
  bool hud_dirty = true;

  ID3D11Device* device = nullptr;
  ID3D11VertexShader* vs = nullptr;
  ID3D11PixelShader* ps = nullptr;
  ID3D11SamplerState* point_sampler = nullptr;
  ID3D11BlendState* alpha_blend = nullptr;
  ID3D11DepthStencilState* no_depth = nullptr;
  ID3D11RasterizerState* cull_none = nullptr;

  ID3D11Texture2D* hud_tex = nullptr;
  ID3D11ShaderResourceView* hud_srv = nullptr;
  uint32_t hud_w = 0;
  uint32_t hud_h = 0;

  ID3D11RenderTargetView* back_rtv = nullptr;
  ID3D11Texture2D* last_backbuffer = nullptr;
  uint32_t cached_width = 0;
  uint32_t cached_height = 0;

  uint64_t frames_seen = 0;
  uint64_t hud_draws = 0;
  uint64_t hotkey_events = 0;
  uint64_t resizes = 0;

  void release_targets() noexcept {
    safe_release(back_rtv);
    safe_release(last_backbuffer);
    cached_width = 0;
    cached_height = 0;
  }

  void release_hud_texture() noexcept {
    safe_release(hud_srv);
    safe_release(hud_tex);
    hud_w = 0;
    hud_h = 0;
  }

  void release_all() noexcept {
    release_targets();
    release_hud_texture();
    safe_release(cull_none);
    safe_release(no_depth);
    safe_release(alpha_blend);
    safe_release(point_sampler);
    safe_release(ps);
    safe_release(vs);
    safe_release(device);
  }

  bool ensure_pipeline(ID3D11Device* dev) {
    if (vs != nullptr && ps != nullptr && point_sampler != nullptr && alpha_blend != nullptr &&
        no_depth != nullptr && cull_none != nullptr) {
      return true;
    }
    if (pipeline_failed) {
      return false;
    }

    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    if (compiler == nullptr) {
      compiler = LoadLibraryA("d3dcompiler_43.dll");
    }
    if (compiler == nullptr) {
      SF_LOG_WARN("OverlayModule: d3dcompiler DLL not available; disabling HUD draw");
      pipeline_failed = true;
      return false;
    }

    D3DCompileFn compile_fn = nullptr;
    const FARPROC raw = GetProcAddress(compiler, "D3DCompile");
    std::memcpy(&compile_fn, &raw, sizeof(compile_fn));
    if (compile_fn == nullptr) {
      FreeLibrary(compiler);
      pipeline_failed = true;
      return false;
    }

    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
    const HRESULT vs_hr = compile_fn(kOverlayVsHlsl, std::strlen(kOverlayVsHlsl), nullptr, nullptr,
                                     nullptr, "main", "vs_4_0", 0, 0, &vs_blob, nullptr);
    const HRESULT ps_hr = compile_fn(kOverlayPsHlsl, std::strlen(kOverlayPsHlsl), nullptr, nullptr,
                                     nullptr, "main", "ps_4_0", 0, 0, &ps_blob, nullptr);

    if (FAILED(vs_hr) || FAILED(ps_hr) || vs_blob == nullptr || ps_blob == nullptr) {
      safe_release(vs_blob);
      safe_release(ps_blob);
      FreeLibrary(compiler);
      SF_LOG_WARN("OverlayModule: HLSL compilation failed (vs=0x%08lx, ps=0x%08lx)",
                  static_cast<unsigned long>(vs_hr), static_cast<unsigned long>(ps_hr));
      pipeline_failed = true;
      return false;
    }

    dev->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vs);
    dev->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &ps);
    safe_release(vs_blob);
    safe_release(ps_blob);
    // Keep compiler loaded until both ID3DBlob COM objects are fully released.
    FreeLibrary(compiler);

    D3D11_SAMPLER_DESC sdesc{};
    sdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sdesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sdesc, &point_sampler);

    D3D11_BLEND_DESC bdesc{};
    bdesc.RenderTarget[0].BlendEnable = TRUE;
    bdesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bdesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bdesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bdesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bdesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bdesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bdesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&bdesc, &alpha_blend);

    D3D11_DEPTH_STENCIL_DESC ddesc{};
    ddesc.DepthEnable = FALSE;
    ddesc.StencilEnable = FALSE;
    dev->CreateDepthStencilState(&ddesc, &no_depth);

    D3D11_RASTERIZER_DESC rdesc{};
    rdesc.FillMode = D3D11_FILL_SOLID;
    rdesc.CullMode = D3D11_CULL_NONE;
    rdesc.DepthClipEnable = TRUE;
    dev->CreateRasterizerState(&rdesc, &cull_none);

    device = dev;
    device->AddRef();

    if (vs == nullptr || ps == nullptr || point_sampler == nullptr || alpha_blend == nullptr ||
        no_depth == nullptr || cull_none == nullptr) {
      release_all();
      pipeline_failed = true;
      return false;
    }
    return true;
  }

  bool ensure_target(ID3D11Device* dev, ID3D11Texture2D* back, const D3D11_TEXTURE2D_DESC& desc) {
    if (back_rtv != nullptr && cached_width == desc.Width && cached_height == desc.Height &&
        last_backbuffer == back) {
      return true;
    }
    const bool resized = (cached_width != 0U || cached_height != 0U);
    release_targets();
    if (resized) {
      ++resizes;
    }
    if (FAILED(dev->CreateRenderTargetView(back, nullptr, &back_rtv)) || back_rtv == nullptr) {
      release_targets();
      return false;
    }
    last_backbuffer = back;
    last_backbuffer->AddRef();
    cached_width = desc.Width;
    cached_height = desc.Height;
    return true;
  }

  bool ensure_hud_texture(ID3D11Device* dev, const sf::PresentCtx& ctx) {
    if (!hud_dirty && hud_tex != nullptr && hud_srv != nullptr && ctx.frame_index > 1U &&
        (ctx.frame_index % 15U) != 0U) {
      return true;
    }

    auto& rc = sf::runtime_controls();
    fx::HudSnapshot snap{};
    snap.fps = ctx.avg_fps;
    snap.avg_ms = ctx.avg_frame_ms;
    snap.p1_low_ms = ctx.p1_low_ms;
    snap.spikes = ctx.spike_count;
    snap.mod_active = rc.is_mod_active();
    snap.upscaler_configured = cfg.modules.upscaler;
    snap.upscaler_on = rc.upscaler_active.load(std::memory_order_relaxed);
    snap.particles_configured = cfg.modules.particles;
    snap.particles_on = rc.particles_active.load(std::memory_order_relaxed);
    snap.frame_cap_fps = (cfg.modules.presentation) ? cfg.present.max_fps : 0U;
    snap.frame_cap_on = rc.frame_cap_active.load(std::memory_order_relaxed);
    snap.key_overlay = sf::canonical_hotkey_label(cfg.hotkeys.toggle_overlay);
    snap.key_mod = sf::canonical_hotkey_label(cfg.hotkeys.toggle_mod);
    snap.key_upscaler = sf::canonical_hotkey_label(cfg.hotkeys.toggle_upscaler);
    snap.key_particles = sf::canonical_hotkey_label(cfg.hotkeys.toggle_particles);
    snap.key_cap = sf::canonical_hotkey_label(cfg.hotkeys.toggle_frame_cap);
    snap.show_fps = cfg.overlay.show_fps;
    snap.show_status = cfg.overlay.show_status;

    const auto lines = fx::format_hud_lines(snap);
    const auto bmp = fx::rasterize_hud(lines, snap.mod_active, cfg.overlay.opacity_percent);
    if (bmp.width == 0U || bmp.height == 0U || bmp.rgba.empty()) {
      return false;
    }

    release_hud_texture();

    D3D11_TEXTURE2D_DESC tdesc{};
    tdesc.Width = bmp.width;
    tdesc.Height = bmp.height;
    tdesc.MipLevels = 1U;
    tdesc.ArraySize = 1U;
    tdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tdesc.SampleDesc.Count = 1U;
    tdesc.Usage = D3D11_USAGE_IMMUTABLE;
    tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = bmp.rgba.data();
    init.SysMemPitch = bmp.width * 4U;

    if (FAILED(dev->CreateTexture2D(&tdesc, &init, &hud_tex)) || hud_tex == nullptr) {
      release_hud_texture();
      return false;
    }
    if (FAILED(dev->CreateShaderResourceView(hud_tex, nullptr, &hud_srv)) || hud_srv == nullptr) {
      release_hud_texture();
      return false;
    }

    hud_w = bmp.width;
    hud_h = bmp.height;
    hud_dirty = false;
    return true;
  }
};

OverlayModule::OverlayModule() : impl_(std::make_unique<Impl>()) {}

OverlayModule::~OverlayModule() = default;

bool OverlayModule::init(const sf::GameProfile&, const sf::Config& cfg) {
  impl_->cfg = cfg;
  impl_->hotkeys_vk = {{
      sf::parse_virtual_key(cfg.hotkeys.toggle_overlay),
      sf::parse_virtual_key(cfg.hotkeys.toggle_mod),
      sf::parse_virtual_key(cfg.hotkeys.toggle_upscaler),
      sf::parse_virtual_key(cfg.hotkeys.toggle_particles),
      sf::parse_virtual_key(cfg.hotkeys.toggle_frame_cap),
  }};
  impl_->tracker.reset();
  impl_->elapsed_ms = 0.0;
  impl_->hud_dirty = true;
  impl_->active = true;

  SF_LOG("OverlayModule active (position=%s, scale=%u, hotkeys=%s)", cfg.overlay.position.c_str(),
         cfg.overlay.scale, cfg.hotkeys.enabled ? "on" : "off");
  return true;
}

void OverlayModule::on_present(sf::PresentCtx& ctx) {
  if (!impl_->active) {
    return;
  }
  ++impl_->frames_seen;
  impl_->elapsed_ms += (ctx.delta_ms > 0.0) ? ctx.delta_ms : 1.0;

  if (impl_->cfg.hotkeys.enabled) {
    std::array<bool, sf::kHotkeyActionCount> down{};
    for (size_t i = 0U; i < sf::kHotkeyActionCount; ++i) {
      const uint32_t vk = impl_->hotkeys_vk[i];
      if (vk != 0U) {
        down[i] = (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
      }
    }
    for (const auto action : impl_->tracker.poll(down, impl_->elapsed_ms)) {
      const bool next = sf::runtime_controls().apply_action(action);
      ++impl_->hotkey_events;
      impl_->hud_dirty = true;
      SF_LOG("Hotkey %s -> %s", sf::hotkey_action_name(action), next ? "ON" : "OFF");
    }
  }

  if (!sf::runtime_controls().is_overlay_visible() ||
      (!impl_->cfg.overlay.show_fps && !impl_->cfg.overlay.show_status) ||
      ctx.swap_chain == nullptr) {
    return;
  }

  auto* sc = static_cast<IDXGISwapChain*>(ctx.swap_chain);
  ID3D11Texture2D* back = nullptr;
  if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) ||
      back == nullptr) {
    return;
  }

  D3D11_TEXTURE2D_DESC back_desc{};
  back->GetDesc(&back_desc);
  if (back_desc.Width == 0U || back_desc.Height == 0U) {
    back->Release();
    return;
  }

  ID3D11Device* dev = nullptr;
  back->GetDevice(&dev);
  if (dev == nullptr) {
    back->Release();
    return;
  }

  ID3D11DeviceContext* d3d_ctx = nullptr;
  dev->GetImmediateContext(&d3d_ctx);
  if (d3d_ctx == nullptr) {
    dev->Release();
    back->Release();
    return;
  }

  if (impl_->ensure_pipeline(dev) && impl_->ensure_target(dev, back, back_desc) &&
      impl_->ensure_hud_texture(dev, ctx)) {
    const auto rect = fx::compute_hud_rect(
        static_cast<uint32_t>(back_desc.Width), static_cast<uint32_t>(back_desc.Height),
        impl_->hud_w, impl_->hud_h, impl_->cfg.overlay.scale, impl_->cfg.overlay.position);

    std::array<ID3D11RenderTargetView*, 1> prev_rtv{};
    ID3D11DepthStencilView* prev_dsv = nullptr;
    d3d_ctx->OMGetRenderTargets(1U, prev_rtv.data(), &prev_dsv);

    UINT prev_vp_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> prev_vps{};
    d3d_ctx->RSGetViewports(&prev_vp_count, prev_vps.data());

    ID3D11VertexShader* prev_vs = nullptr;
    ID3D11PixelShader* prev_ps = nullptr;
    d3d_ctx->VSGetShader(&prev_vs, nullptr, nullptr);
    d3d_ctx->PSGetShader(&prev_ps, nullptr, nullptr);

    ID3D11InputLayout* prev_il = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY prev_topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    d3d_ctx->IAGetInputLayout(&prev_il);
    d3d_ctx->IAGetPrimitiveTopology(&prev_topo);

    ID3D11BlendState* prev_blend = nullptr;
    std::array<FLOAT, 4> prev_blend_factor{};
    UINT prev_sample_mask = 0;
    d3d_ctx->OMGetBlendState(&prev_blend, prev_blend_factor.data(), &prev_sample_mask);

    ID3D11DepthStencilState* prev_dss = nullptr;
    UINT prev_stencil = 0;
    d3d_ctx->OMGetDepthStencilState(&prev_dss, &prev_stencil);

    ID3D11RasterizerState* prev_rs = nullptr;
    d3d_ctx->RSGetState(&prev_rs);

    std::array<ID3D11ShaderResourceView*, 1> prev_srv{};
    std::array<ID3D11SamplerState*, 1> prev_samp{};
    d3d_ctx->PSGetShaderResources(0U, 1U, prev_srv.data());
    d3d_ctx->PSGetSamplers(0U, 1U, prev_samp.data());

    D3D11_VIEWPORT vp{rect.x, rect.y, rect.width, rect.height, 0.0f, 1.0f};
    d3d_ctx->RSSetViewports(1U, &vp);
    d3d_ctx->RSSetState(impl_->cull_none);
    d3d_ctx->OMSetRenderTargets(1U, &impl_->back_rtv, nullptr);
    d3d_ctx->OMSetBlendState(impl_->alpha_blend, nullptr, 0xffffffffU);
    d3d_ctx->OMSetDepthStencilState(impl_->no_depth, 0U);
    d3d_ctx->IASetInputLayout(nullptr);
    d3d_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d3d_ctx->VSSetShader(impl_->vs, nullptr, 0U);
    d3d_ctx->PSSetShader(impl_->ps, nullptr, 0U);
    d3d_ctx->PSSetShaderResources(0U, 1U, &impl_->hud_srv);
    d3d_ctx->PSSetSamplers(0U, 1U, &impl_->point_sampler);
    d3d_ctx->Draw(3U, 0U);

    ID3D11ShaderResourceView* null_srv = nullptr;
    d3d_ctx->PSSetShaderResources(0U, 1U, &null_srv);

    d3d_ctx->OMSetRenderTargets(1U, prev_rtv.data(), prev_dsv);
    if (prev_vp_count > 0U) {
      d3d_ctx->RSSetViewports(prev_vp_count, prev_vps.data());
    }
    d3d_ctx->RSSetState(prev_rs);
    d3d_ctx->VSSetShader(prev_vs, nullptr, 0U);
    d3d_ctx->PSSetShader(prev_ps, nullptr, 0U);
    d3d_ctx->IASetInputLayout(prev_il);
    d3d_ctx->IASetPrimitiveTopology(prev_topo);
    d3d_ctx->OMSetBlendState(prev_blend, prev_blend_factor.data(), prev_sample_mask);
    d3d_ctx->OMSetDepthStencilState(prev_dss, prev_stencil);
    d3d_ctx->PSSetShaderResources(0U, 1U, prev_srv.data());
    d3d_ctx->PSSetSamplers(0U, 1U, prev_samp.data());

    safe_release(prev_rtv[0]);
    safe_release(prev_dsv);
    safe_release(prev_vs);
    safe_release(prev_ps);
    safe_release(prev_il);
    safe_release(prev_blend);
    safe_release(prev_dss);
    safe_release(prev_rs);
    safe_release(prev_srv[0]);
    safe_release(prev_samp[0]);

    ++impl_->hud_draws;
  }

  d3d_ctx->Release();
  dev->Release();
  back->Release();
}

void OverlayModule::on_resize() {
  if (!impl_->active) {
    return;
  }
  if (impl_->cached_width != 0U || impl_->cached_height != 0U) {
    ++impl_->resizes;
  }
  impl_->release_targets();
}

void OverlayModule::shutdown() {
  if (!impl_->active) {
    return;
  }
  impl_->active = false;
  impl_->release_all();
  SF_LOG("OverlayModule shutdown (frames=%llu, hud_draws=%llu, hotkey_events=%llu, resizes=%llu)",
         static_cast<unsigned long long>(impl_->frames_seen),
         static_cast<unsigned long long>(impl_->hud_draws),
         static_cast<unsigned long long>(impl_->hotkey_events),
         static_cast<unsigned long long>(impl_->resizes));
}

} // namespace sf::gfx
