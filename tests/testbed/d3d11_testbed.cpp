#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
  int frames = 300;
  bool do_resize = false;
  bool do_particles = false;
  bool do_textures = false;
  UINT msaa = 1;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      frames = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--resize") == 0) {
      do_resize = true;
    } else if (std::strcmp(argv[i], "--particles") == 0) {
      do_particles = true;
    } else if (std::strcmp(argv[i], "--textures") == 0) {
      do_textures = true;
    } else if (std::strcmp(argv[i], "--msaa") == 0 && i + 1 < argc) {
      msaa = static_cast<UINT>(std::atoi(argv[++i]));
    }
  }

  HMODULE mod = LoadLibraryA("dinput8.dll");
  if (!mod) {
    std::fprintf(stderr, "Failed to load dinput8.dll (%lu)\n", GetLastError());
    return 2;
  }
  // Give init_thread enough time to finish SwapChain + Context hook setup before frame 0.
  Sleep(500);

  WNDCLASSEXA wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_CLASSDC;
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = "sf_testbed_wnd";
  RegisterClassExA(&wc);

  HWND hwnd = CreateWindowA(wc.lpszClassName, "SoulsFrame Testbed", WS_OVERLAPPEDWINDOW, 0, 0, 320,
                            240, nullptr, nullptr, wc.hInstance, nullptr);
  if (!hwnd) {
    std::fprintf(stderr, "CreateWindowA failed (%lu)\n", GetLastError());
    FreeLibrary(mod);
    return 3;
  }

  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 1;
  sd.BufferDesc.Width = 320;
  sd.BufferDesc.Height = 240;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = msaa;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  IDXGISwapChain* sc = nullptr;
  ID3D11Device* dev = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  D3D_FEATURE_LEVEL fl{};

  D3D_DRIVER_TYPE drivers[] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};
  HRESULT hr = E_FAIL;
  for (auto drv : drivers) {
    hr = D3D11CreateDeviceAndSwapChain(nullptr, drv, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd,
                                       &sc, &dev, &fl, &ctx);
    if (SUCCEEDED(hr) && sc) {
      break;
    }
  }

  if (FAILED(hr) || !sc) {
    std::fprintf(stderr, "D3D11CreateDeviceAndSwapChain failed: 0x%08lx\n",
                 static_cast<unsigned long>(hr));
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    FreeLibrary(mod);
    return 4;
  }

  ID3D11RenderTargetView* rtv = nullptr;
  auto rebuild_rtv = [&]() {
    if (rtv) {
      rtv->Release();
      rtv = nullptr;
    }
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) &&
        back) {
      dev->CreateRenderTargetView(back, nullptr, &rtv);
      back->Release();
    }
  };
  rebuild_rtv();

  ID3D11Texture2D* test_tex = nullptr;
  if (do_textures) {
    UINT pixels[64]{};
    for (UINT i = 0; i < 64U; ++i) {
      pixels[i] = 0xFF000000U | (i * 4U) | ((255U - i * 3U) << 8U);
    }
    D3D11_TEXTURE2D_DESC tdesc{};
    tdesc.Width = 8;
    tdesc.Height = 8;
    tdesc.MipLevels = 1;
    tdesc.ArraySize = 1;
    tdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tdesc.SampleDesc.Count = 1;
    tdesc.Usage = D3D11_USAGE_IMMUTABLE;
    tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init_tex{pixels, 8U * 4U, 0};
    dev->CreateTexture2D(&tdesc, &init_tex, &test_tex);
  }

  ID3D11BlendState* alpha_blend = nullptr;
  ID3D11DepthStencilState* no_depth_write = nullptr;
  ID3D11VertexShader* vs = nullptr;
  ID3D11PixelShader* ps = nullptr;
  ID3D11InputLayout* input_layout = nullptr;
  ID3D11Buffer* vb = nullptr;
  if (do_particles) {
    using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR,
                                          LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!compiler) {
      compiler = LoadLibraryA("d3dcompiler_43.dll");
    }
    D3DCompileFn compile_fn = nullptr;
    if (compiler) {
      FARPROC raw = GetProcAddress(compiler, "D3DCompile");
      std::memcpy(&compile_fn, &raw, sizeof(compile_fn));
    }
    if (compile_fn) {
      static const char kVsSrc[] =
          "struct VOut { float4 p : SV_POSITION; };\n"
          "VOut main(float3 pos : POSITION) { VOut o; o.p = float4(pos, 1.0f); return o; }\n";
      static const char kPsSrc[] =
          "float4 main() : SV_TARGET { return float4(1.0f, 0.5f, 0.2f, 0.5f); }\n";
      ID3DBlob* vs_blob = nullptr;
      ID3DBlob* ps_blob = nullptr;
      if (SUCCEEDED(compile_fn(kVsSrc, std::strlen(kVsSrc), nullptr, nullptr, nullptr, "main",
                               "vs_4_0", 0, 0, &vs_blob, nullptr)) &&
          vs_blob) {
        dev->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr,
                                &vs);
        const D3D11_INPUT_ELEMENT_DESC ied = {
            "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
        dev->CreateInputLayout(&ied, 1, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
                               &input_layout);
        vs_blob->Release();
      }
      if (SUCCEEDED(compile_fn(kPsSrc, std::strlen(kPsSrc), nullptr, nullptr, nullptr, "main",
                               "ps_4_0", 0, 0, &ps_blob, nullptr)) &&
          ps_blob) {
        dev->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &ps);
        ps_blob->Release();
      }
    }

    static const float kVerts[18] = {
        0.0f,  0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f,
        -0.5f, 0.5f, 0.5f, 0.5f, 0.5f,  0.5f, 0.0f,  -0.5f, 0.5f,
    };
    D3D11_BUFFER_DESC vbd{};
    vbd.ByteWidth = sizeof(kVerts);
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init_data{kVerts, 0, 0};
    dev->CreateBuffer(&vbd, &init_data, &vb);

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
    ddesc.DepthEnable = TRUE;
    ddesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ddesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dev->CreateDepthStencilState(&ddesc, &no_depth_write);
    if (!vs || !ps) {
      std::fprintf(stderr, "Warning: D3DCompile failed for --particles shaders\n");
    }
    if (compiler) {
      FreeLibrary(compiler);
    }
  }

  for (int f = 0; f < frames; ++f) {
    if (do_resize && f == frames / 2) {
      if (rtv) {
        rtv->Release();
        rtv = nullptr;
      }
      ctx->OMSetRenderTargets(0, nullptr, nullptr);
      sc->ResizeBuffers(0, 640, 480, DXGI_FORMAT_UNKNOWN, 0);
      rebuild_rtv();
    }

    if (rtv) {
      const float clear_col[4] = {0.1f, 0.2f, 0.3f, 1.0f};
      ctx->OMSetRenderTargets(1, &rtv, nullptr);
      ctx->ClearRenderTargetView(rtv, clear_col);
      if (do_particles && vs && ps) {
        D3D11_VIEWPORT vp{0.0f, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f};
        ctx->RSSetViewports(1, &vp);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if (input_layout && vb) {
          UINT stride = 3 * sizeof(float);
          UINT offset = 0;
          ctx->IASetInputLayout(input_layout);
          ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        }
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);

        ctx->OMSetBlendState(nullptr, nullptr, 0xffffffffU);
        ctx->OMSetDepthStencilState(nullptr, 0);
        ctx->Draw(3, 0);

        ctx->OMSetBlendState(alpha_blend, nullptr, 0xffffffffU);
        ctx->OMSetDepthStencilState(no_depth_write, 0);
        ctx->DrawInstanced(6, 8, 0, 0);
      }
    }
    sc->Present(0, 0);
  }

  if (vb)
    vb->Release();
  if (input_layout)
    input_layout->Release();
  if (ps)
    ps->Release();
  if (vs)
    vs->Release();
  if (no_depth_write)
    no_depth_write->Release();
  if (alpha_blend)
    alpha_blend->Release();
  if (test_tex)
    test_tex->Release();
  if (rtv)
    rtv->Release();
  ctx->Release();
  dev->Release();
  sc->Release();
  DestroyWindow(hwnd);
  UnregisterClassA(wc.lpszClassName, wc.hInstance);
  FreeLibrary(mod);
  return 0;
}
