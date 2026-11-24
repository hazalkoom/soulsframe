#include "gfx/d3d11/upscaler_module.h"

#include "core/log.h"
#include "core/runtime_controls.h"
#include "gfx/d3d11/d3d11_helpers.h"
#include "gfx/fx/fsr1_math.h"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace sf::gfx {

namespace {

struct alignas(16) UpscalerCbData {
  fx::EasuConstants easu{};
  fx::RcasConstants rcas{};
  std::array<uint32_t, 4> flags{};
};

constexpr const char* kFullscreenVsHlsl = R"(
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

constexpr const char* kFsr1RcasPsHlsl = R"(
Texture2D<float4> src_tex : register(t0);
SamplerState src_samp : register(s0);

cbuffer UpscalerCb : register(b0) {
  float4 easu_con0;
  float4 easu_con1;
  float4 easu_con2;
  float4 easu_con3;
  float4 rcas_con0;
  uint4  uflags;
};

float luma(float3 c) {
  return dot(c, float3(0.299f, 0.587f, 0.114f));
}

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
  float3 center = src_tex.SampleLevel(src_samp, uv, 0).rgb;
  if (uflags.y == 0u) {
    return float4(center, 1.0f);
  }
  float2 texel = rcas_con0.yz;
  float3 n = src_tex.SampleLevel(src_samp, uv + float2(0.0f, -texel.y), 0).rgb;
  float3 s = src_tex.SampleLevel(src_samp, uv + float2(0.0f,  texel.y), 0).rgb;
  float3 w = src_tex.SampleLevel(src_samp, uv + float2(-texel.x, 0.0f), 0).rgb;
  float3 e = src_tex.SampleLevel(src_samp, uv + float2( texel.x, 0.0f), 0).rgb;

  float lc = luma(center);
  float ln = luma(n);
  float ls = luma(s);
  float lw = luma(w);
  float le = luma(e);

  float mn = min(lc, min(min(ln, ls), min(lw, le)));
  float mx = max(lc, max(max(ln, ls), max(lw, le)));
  float hit_min = -mn / max(4.0f * mx, 1e-5f);
  float hit_max = (1.0f - mx) / min(4.0f * mn - 4.0f, -1e-5f);
  float lobe = max(-0.1875f, min(max(hit_min, hit_max), 0.0f)) * rcas_con0.x;
  float3 color = (center + lobe * (n + s + w + e)) / max(1.0f + 4.0f * lobe, 1e-4f);
  return float4(saturate(color), 1.0f);
}
)";

using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR,
                                      UINT, UINT, ID3DBlob**, ID3DBlob**);

} // namespace

struct UpscalerModule::Impl {
  sf::Config::Upscaler cfg{};
  bool active = false;
  bool pipeline_failed = false;

  ID3D11Device* device = nullptr;
  ID3D11VertexShader* vs = nullptr;
  ID3D11PixelShader* ps = nullptr;
  ID3D11SamplerState* linear_sampler = nullptr;
  ID3D11Buffer* cbuffer = nullptr;
  ID3D11RasterizerState* cull_none = nullptr;

  ID3D11Texture2D* src_copy = nullptr;
  ID3D11ShaderResourceView* src_srv = nullptr;
  uint32_t cached_width = 0;
  uint32_t cached_height = 0;
  DXGI_FORMAT cached_format = DXGI_FORMAT_UNKNOWN;

  uint64_t frames_processed = 0;
  uint64_t easu_passes = 0;
  uint64_t rcas_passes = 0;
  uint64_t resizes = 0;

  void release_targets() noexcept {
    safe_release(src_srv);
    safe_release(src_copy);
    cached_width = 0;
    cached_height = 0;
    cached_format = DXGI_FORMAT_UNKNOWN;
  }

  void release_all() noexcept {
    release_targets();
    safe_release(cull_none);
    safe_release(cbuffer);
    safe_release(linear_sampler);
    safe_release(ps);
    safe_release(vs);
    safe_release(device);
  }

  bool ensure_shaders(ID3D11Device* dev) {
    if (vs != nullptr && ps != nullptr && linear_sampler != nullptr && cbuffer != nullptr &&
        cull_none != nullptr) {
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
      SF_LOG("UpscalerModule: d3dcompiler DLL not available; disabling GPU pass");
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
    const HRESULT vs_hr = compile_fn(kFullscreenVsHlsl, std::strlen(kFullscreenVsHlsl), nullptr,
                                     nullptr, nullptr, "main", "vs_4_0", 0, 0, &vs_blob, nullptr);
    const HRESULT ps_hr = compile_fn(kFsr1RcasPsHlsl, std::strlen(kFsr1RcasPsHlsl), nullptr,
                                     nullptr, nullptr, "main", "ps_4_0", 0, 0, &ps_blob, nullptr);

    if (FAILED(vs_hr) || FAILED(ps_hr) || vs_blob == nullptr || ps_blob == nullptr) {
      safe_release(vs_blob);
      safe_release(ps_blob);
      FreeLibrary(compiler);
      SF_LOG("UpscalerModule: HLSL compilation failed (vs=0x%08lx, ps=0x%08lx)",
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
    sdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sdesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sdesc, &linear_sampler);

    D3D11_BUFFER_DESC bdesc{};
    bdesc.ByteWidth = sizeof(UpscalerCbData);
    bdesc.Usage = D3D11_USAGE_DEFAULT;
    bdesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    dev->CreateBuffer(&bdesc, nullptr, &cbuffer);

    D3D11_RASTERIZER_DESC rdesc{};
    rdesc.FillMode = D3D11_FILL_SOLID;
    rdesc.CullMode = D3D11_CULL_NONE;
    rdesc.ScissorEnable = FALSE;
    rdesc.DepthClipEnable = TRUE;
    dev->CreateRasterizerState(&rdesc, &cull_none);

    device = dev;
    device->AddRef();

    if (vs == nullptr || ps == nullptr || linear_sampler == nullptr || cbuffer == nullptr ||
        cull_none == nullptr) {
      release_all();
      pipeline_failed = true;
      return false;
    }
    return true;
  }

  bool ensure_targets(ID3D11Device* dev, const D3D11_TEXTURE2D_DESC& desc) {
    if (src_copy != nullptr && src_srv != nullptr && cached_width == desc.Width &&
        cached_height == desc.Height && cached_format == desc.Format) {
      return true;
    }

    const bool resized = (cached_width != 0U || cached_height != 0U);
    release_targets();
    if (resized) {
      ++resizes;
    }

    D3D11_TEXTURE2D_DESC copy_desc = desc;
    copy_desc.Usage = D3D11_USAGE_DEFAULT;
    copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    copy_desc.CPUAccessFlags = 0;
    copy_desc.MiscFlags = 0;

    if (FAILED(dev->CreateTexture2D(&copy_desc, nullptr, &src_copy)) || src_copy == nullptr) {
      release_targets();
      return false;
    }
    if (FAILED(dev->CreateShaderResourceView(src_copy, nullptr, &src_srv)) || src_srv == nullptr) {
      release_targets();
      return false;
    }

    cached_width = desc.Width;
    cached_height = desc.Height;
    cached_format = desc.Format;
    return true;
  }
};

UpscalerModule::UpscalerModule() : impl_(std::make_unique<Impl>()) {}

UpscalerModule::~UpscalerModule() = default;

bool UpscalerModule::init(const sf::GameProfile&, const sf::Config& cfg) {
  impl_->cfg = cfg.upscaler;
  impl_->active = true;
  const uint32_t eff_scale =
      fx::resolve_preset_scale_percent(cfg.upscaler.preset, cfg.upscaler.scale_percent);
  SF_LOG("UpscalerModule active (mode=%s, preset=%s, scale_percent=%u, sharpness=%u)",
         cfg.upscaler.mode.c_str(), cfg.upscaler.preset.c_str(), eff_scale,
         cfg.upscaler.sharpness_percent);
  return true;
}

void UpscalerModule::on_present(sf::PresentCtx& ctx) {
  if (!impl_->active || !sf::runtime_controls().should_run_upscaler() ||
      impl_->cfg.mode == "none" || ctx.swap_chain == nullptr) {
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
  if (back_desc.SampleDesc.Count > 1U || back_desc.Width == 0U || back_desc.Height == 0U) {
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

  if (impl_->ensure_shaders(dev) && impl_->ensure_targets(dev, back_desc)) {
    ID3D11RenderTargetView* back_rtv = create_backbuffer_rtv(dev, back, back_desc);
    if (back_rtv != nullptr) {
      const auto dims =
          fx::compute_upscale_dimensions(static_cast<uint32_t>(back_desc.Width),
                                         static_cast<uint32_t>(back_desc.Height), impl_->cfg);

      UpscalerCbData cb{};
      cb.easu =
          fx::compute_easu_constants(dims.render_width, dims.render_height, dims.display_width,
                                     dims.display_height, dims.display_width, dims.display_height);
      cb.rcas = fx::compute_rcas_constants(dims.display_width, dims.display_height,
                                           impl_->cfg.sharpness_percent);
      cb.flags = {dims.easu_active ? 1U : 0U, dims.rcas_active ? 1U : 0U, 0U, 0U};

      // Save all pipeline state we touch so game rendering state stays intact.
      std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> prev_rtvs{};
      ID3D11DepthStencilView* prev_dsv = nullptr;
      d3d_ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, prev_rtvs.data(),
                                  &prev_dsv);

      UINT prev_vp_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
      std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE>
          prev_vps{};
      d3d_ctx->RSGetViewports(&prev_vp_count, prev_vps.data());

      ID3D11RasterizerState* prev_rs = nullptr;
      d3d_ctx->RSGetState(&prev_rs);

      ID3D11VertexShader* prev_vs = nullptr;
      ID3D11PixelShader* prev_ps = nullptr;
      ID3D11GeometryShader* prev_gs = nullptr;
      ID3D11HullShader* prev_hs = nullptr;
      ID3D11DomainShader* prev_ds = nullptr;
      d3d_ctx->VSGetShader(&prev_vs, nullptr, nullptr);
      d3d_ctx->PSGetShader(&prev_ps, nullptr, nullptr);
      d3d_ctx->GSGetShader(&prev_gs, nullptr, nullptr);
      d3d_ctx->HSGetShader(&prev_hs, nullptr, nullptr);
      d3d_ctx->DSGetShader(&prev_ds, nullptr, nullptr);

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

      std::array<ID3D11ShaderResourceView*, 1> prev_srv{};
      std::array<ID3D11SamplerState*, 1> prev_samp{};
      std::array<ID3D11Buffer*, 1> prev_cb{};
      d3d_ctx->PSGetShaderResources(0U, 1U, prev_srv.data());
      d3d_ctx->PSGetSamplers(0U, 1U, prev_samp.data());
      d3d_ctx->PSGetConstantBuffers(0U, 1U, prev_cb.data());

      // Unbind swapchain RTV before copying backbuffer into src_copy.
      d3d_ctx->OMSetRenderTargets(0U, nullptr, nullptr);
      d3d_ctx->CopyResource(impl_->src_copy, back);
      d3d_ctx->UpdateSubresource(impl_->cbuffer, 0U, nullptr, &cb, 0U, 0U);

      D3D11_VIEWPORT vp{
          0.0f, 0.0f, static_cast<FLOAT>(back_desc.Width), static_cast<FLOAT>(back_desc.Height),
          0.0f, 1.0f};
      d3d_ctx->RSSetViewports(1U, &vp);
      d3d_ctx->RSSetState(impl_->cull_none);
      d3d_ctx->OMSetRenderTargets(1U, &back_rtv, nullptr);
      d3d_ctx->OMSetBlendState(nullptr, nullptr, 0xffffffffU);
      d3d_ctx->OMSetDepthStencilState(nullptr, 0U);
      d3d_ctx->IASetInputLayout(nullptr);
      d3d_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      d3d_ctx->VSSetShader(impl_->vs, nullptr, 0U);
      d3d_ctx->PSSetShader(impl_->ps, nullptr, 0U);
      d3d_ctx->GSSetShader(nullptr, nullptr, 0U);
      d3d_ctx->HSSetShader(nullptr, nullptr, 0U);
      d3d_ctx->DSSetShader(nullptr, nullptr, 0U);
      d3d_ctx->PSSetShaderResources(0U, 1U, &impl_->src_srv);
      d3d_ctx->PSSetSamplers(0U, 1U, &impl_->linear_sampler);
      d3d_ctx->PSSetConstantBuffers(0U, 1U, &impl_->cbuffer);
      d3d_ctx->Draw(3U, 0U);

      // Unbind SRV before next frame's CopyResource.
      ID3D11ShaderResourceView* null_srv = nullptr;
      d3d_ctx->PSSetShaderResources(0U, 1U, &null_srv);

      // Restore caller state.
      d3d_ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, prev_rtvs.data(),
                                  prev_dsv);
      if (prev_vp_count > 0U) {
        d3d_ctx->RSSetViewports(prev_vp_count, prev_vps.data());
      } else {
        d3d_ctx->RSSetViewports(1U, &vp);
      }
      d3d_ctx->RSSetState(prev_rs);
      d3d_ctx->VSSetShader(prev_vs, nullptr, 0U);
      d3d_ctx->PSSetShader(prev_ps, nullptr, 0U);
      d3d_ctx->GSSetShader(prev_gs, nullptr, 0U);
      d3d_ctx->HSSetShader(prev_hs, nullptr, 0U);
      d3d_ctx->DSSetShader(prev_ds, nullptr, 0U);
      d3d_ctx->IASetInputLayout(prev_il);
      d3d_ctx->IASetPrimitiveTopology(prev_topo);
      d3d_ctx->OMSetBlendState(prev_blend, prev_blend_factor.data(), prev_sample_mask);
      d3d_ctx->OMSetDepthStencilState(prev_dss, prev_stencil);
      d3d_ctx->PSSetShaderResources(0U, 1U, prev_srv.data());
      d3d_ctx->PSSetSamplers(0U, 1U, prev_samp.data());
      d3d_ctx->PSSetConstantBuffers(0U, 1U, prev_cb.data());

      for (auto*& rtv : prev_rtvs) {
        safe_release(rtv);
      }
      safe_release(prev_dsv);
      safe_release(prev_rs);
      safe_release(prev_vs);
      safe_release(prev_ps);
      safe_release(prev_gs);
      safe_release(prev_hs);
      safe_release(prev_ds);
      safe_release(prev_il);
      safe_release(prev_blend);
      safe_release(prev_dss);
      safe_release(prev_srv[0]);
      safe_release(prev_samp[0]);
      safe_release(prev_cb[0]);
      back_rtv->Release();

      ++impl_->frames_processed;
      if (dims.easu_active) {
        ++impl_->easu_passes;
      }
      if (dims.rcas_active) {
        ++impl_->rcas_passes;
      }
    }
  }

  d3d_ctx->Release();
  dev->Release();
  back->Release();
}

void UpscalerModule::on_resize() {
  if (!impl_->active) {
    return;
  }
  if (impl_->cached_width != 0U || impl_->cached_height != 0U) {
    ++impl_->resizes;
  }
  impl_->release_targets();
}

void UpscalerModule::shutdown() {
  if (!impl_->active) {
    return;
  }
  impl_->active = false;
  impl_->release_all();
  SF_LOG("UpscalerModule shutdown (frames=%llu, easu_passes=%llu, rcas_passes=%llu, resizes=%llu)",
         static_cast<unsigned long long>(impl_->frames_processed),
         static_cast<unsigned long long>(impl_->easu_passes),
         static_cast<unsigned long long>(impl_->rcas_passes),
         static_cast<unsigned long long>(impl_->resizes));
}

} // namespace sf::gfx
