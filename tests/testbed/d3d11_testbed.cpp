#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
  int frames = 300;
  bool do_resize = false;
  UINT msaa = 1;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      frames = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--resize") == 0) {
      do_resize = true;
    } else if (std::strcmp(argv[i], "--msaa") == 0 && i + 1 < argc) {
      msaa = static_cast<UINT>(std::atoi(argv[++i]));
    }
  }

  HMODULE mod = LoadLibraryA("dinput8.dll");
  if (!mod) {
    std::fprintf(stderr, "Failed to load dinput8.dll (%lu)\n", GetLastError());
    return 2;
  }
  // Give init_thread a brief moment to install the Present hook before frame 0.
  Sleep(250);

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
    }
    sc->Present(0, 0);
  }

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
