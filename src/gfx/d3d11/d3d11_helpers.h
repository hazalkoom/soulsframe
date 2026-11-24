#pragma once

#include <d3d11.h>
#include <dxgi.h>

namespace sf::gfx {

/// Releases a COM pointer safely and resets it to `nullptr`.
template <typename T> inline void safe_release(T*& ptr) noexcept {
  if (ptr != nullptr) {
    ptr->Release();
    ptr = nullptr;
  }
}

/// Maps typeless swap-chain backbuffer formats to typed RTV-compatible `DXGI_FORMAT` values.
inline DXGI_FORMAT resolve_rtv_format(DXGI_FORMAT fmt) noexcept {
  switch (fmt) {
  case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    return DXGI_FORMAT_R10G10B10A2_UNORM;
  case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    return DXGI_FORMAT_R16G16B16A16_FLOAT;
  default:
    return DXGI_FORMAT_UNKNOWN;
  }
}

/// Creates a transient `ID3D11RenderTargetView` for a swap-chain backbuffer texture,
/// resolving typeless formats and MSAA dimensions automatically.
inline ID3D11RenderTargetView* create_backbuffer_rtv(ID3D11Device* dev, ID3D11Texture2D* back,
                                                     const D3D11_TEXTURE2D_DESC& desc) noexcept {
  ID3D11RenderTargetView* rtv = nullptr;
  const DXGI_FORMAT resolved = resolve_rtv_format(desc.Format);
  if (resolved != DXGI_FORMAT_UNKNOWN) {
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};
    rtv_desc.Format = resolved;
    rtv_desc.ViewDimension =
        (desc.SampleDesc.Count > 1U) ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
    if (SUCCEEDED(dev->CreateRenderTargetView(back, &rtv_desc, &rtv))) {
      return rtv;
    }
    safe_release(rtv);
  }
  if (SUCCEEDED(dev->CreateRenderTargetView(back, nullptr, &rtv))) {
    return rtv;
  }
  safe_release(rtv);
  return nullptr;
}

} // namespace sf::gfx
