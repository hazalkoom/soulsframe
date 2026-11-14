# 02. Dev environment (Fedora + containers + CMake)

Goal: from an empty folder to a cross-compiled `dinput8.dll` that loads in DS3 under Proton and logs frame times.

> Version numbers, package names, and tags below are the ones I'd expect to work. If one fails, check it first. They're the most likely thing to have drifted.

## 1. Host setup (Fedora)

```bash
# Container engine: Fedora ships podman. Docker works too; the justfile supports both.
sudo dnf install -y podman just git cmake ninja-build clang clang-tools-extra ccache \
                    wine winetricks python3-pip gdb

# Optional native tools
sudo dnf install -y mingw64-gcc-c++ mingw64-winpthreads-static   # only if you want to build without containers
pip install --user pandas matplotlib                              # trace analysis (docs/03)
```

Things to install by hand (no dnf package): **Ghidra** (zip from GitHub releases, run natively for the GUI), **RenderDoc** (`dnf` has it, or the upstream tarball), **ImHex** (Flatpak is fine), **Steam**.

**Docker instead of podman:** `export CTR=docker`. Add yourself to the `docker` group. On Fedora with SELinux, bind mounts need the `:Z` suffix (already in the justfile).

### IDE
- **CLion** is the JetBrains C++ IDE. IntelliJ IDEA has no first-class C++ support. CLion has a free non-commercial license at the time of writing (check current terms).
- Point CLion at the CMake presets and use the container toolchain (*Settings → Build → Toolchains → + Docker*), or just run `just build` in a terminal and use CLion for editing/navigation.
- Alternative: VS Code + `clangd` + `CMake Tools`. Generate `compile_commands.json` (the presets below do this).
- Either way, the real compile happens in the container. The IDE only needs headers for IntelliSense: `sudo dnf install mingw64-headers mingw64-crt` on the host.

## 2. Repository layout

```
soulsframe/
├── CMakeLists.txt
├── CMakePresets.json
├── justfile
├── .clang-format  .clang-tidy  .gitignore
├── cmake/
│   ├── toolchains/mingw-w64-x86_64.cmake
│   └── deps.cmake                  # FetchContent pins
├── docker/
│   ├── build.Dockerfile            # MinGW cross-compiler
│   ├── ghidra.Dockerfile           # headless RE
│   ├── wine-test.Dockerfile        # smoke tests under Wine + software GL
│   └── dotnet-tools.Dockerfile     # SoulsFormats bake tools
├── src/
│   ├── core/    dllmain.cpp  proxy_dinput8.cpp  log.{h,cpp}  config.*  module_registry.*  offline_guard.*
│   ├── hooks/   hook_engine.*  sigscan.*  patch.*
│   ├── gfx/
│   │   ├── d3d11/   swapchain_hook.*  context_hooks.*  device_hooks.*  pass_tracker.*  gpu_timer.*
│   │   └── fx/      fsr1.*  particles_lowres.*  post_toggle.*
│   ├── timing/  clock_hooks.*  fixed_step.*  limiter.*
│   ├── tex/     content_hash.*  swap_cache.*  dds.*
│   ├── probe/   tracer.*  time_tracer.*  dump_module.*  inspector_server.*   # SF_PROBE only
│   └── games/ds3/  profile.cpp  sigs.cpp  timing.cpp  render.cpp
├── shaders/                         # HLSL sources, embedded as strings at build
├── tools/
│   ├── sftool/                      # host-native C++ (sigmaker, offsets db, trace tools)
│   ├── py/                          # analysis scripts
│   └── bake/                        # C# SoulsFormats-based asset tools
├── tests/  unit/  testbed/          # testbed = tiny D3D11 app for Wine smoke test
├── re/     ghidra_scripts/  db/  notes/  dumps/ (gitignored)
├── bench/                           # captured frame-time reports
└── scripts/ deploy.sh  smoke-test.sh
```

## 3. Containers

### `docker/build.Dockerfile` (cross-compiler)
```dockerfile
FROM fedora:43
RUN dnf -y install cmake ninja-build ccache git python3 clang-tools-extra \
      mingw64-gcc-c++ mingw64-winpthreads-static mingw64-headers mingw64-crt \
    && dnf clean all
ENV CCACHE_DIR=/ccache
WORKDIR /src
```
`fedora:43` is a placeholder. Pin whichever Fedora release you actually run.

### `docker/ghidra.Dockerfile` (headless RE batch jobs)
```dockerfile
FROM eclipse-temurin:21-jdk
ARG GHIDRA_URL            # full URL of the release zip from GitHub, passed at build time
RUN apt-get update && apt-get install -y curl unzip python3 && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL "$GHIDRA_URL" -o /tmp/g.zip && unzip -q /tmp/g.zip -d /opt \
    && ln -s /opt/ghidra_* /opt/ghidra && rm /tmp/g.zip
ENV PATH="/opt/ghidra/support:${PATH}"
WORKDIR /work
```
Use the **GUI natively** for interactive work (GUI in containers is painful). Use the container for **repeatable batch analysis** and CI exports. Check the Ghidra release notes for the JDK version it requires.

### `docker/wine-test.Dockerfile` (smoke tests, no GPU needed)
```dockerfile
FROM fedora:43
RUN dnf -y install wine xorg-x11-server-Xvfb xorg-x11-xauth mesa-dri-drivers mesa-libGL \
    && dnf clean all
ENV WINEPREFIX=/wine WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
RUN xvfb-run -a wineboot -i || true
WORKDIR /test
```
Wine's built-in D3D11 (wined3d) on llvmpipe software GL is enough to prove "DLL loads, hook installs, frames pass through `Present`". It won't tell you anything about performance.

### `docker/dotnet-tools.Dockerfile`
```dockerfile
FROM mcr.microsoft.com/dotnet/sdk:8.0
WORKDIR /work
```
Used for the `tools/bake` project (SoulsFormats) to unpack/re-pack texture archives and later FXR files on the host's own game copy.

**Game files never go into images.** Mount your Steam folder read-only at run time.

## 4. `justfile`

```make
ctr    := env_var_or_default("CTR", "podman")
userns := if ctr == "podman" { "--userns=keep-id" } else { "--user " + `id -u` + ":" + `id -g` }
root   := justfile_directory()
run    := ctr + " run --rm " + userns + " -v " + root + ":/src:Z -v sf-ccache:/ccache -w /src"
ds3    := env_var_or_default("DS3_DIR", env_var("HOME") + "/.local/share/Steam/steamapps/common/DARK SOULS III/Game")

img-build:
    {{ctr}} build -t sf-build      -f docker/build.Dockerfile .
    {{ctr}} build -t sf-wine-test  -f docker/wine-test.Dockerfile .

configure preset="win64-release":
    {{run}} sf-build cmake --preset {{preset}}

build preset="win64-release":
    {{run}} sf-build cmake --build --preset {{preset}}

probe:                       # build with the RE/telemetry features
    {{run}} sf-build cmake --preset win64-probe
    {{run}} sf-build cmake --build --preset win64-probe

test-host:                   # pure-C++ unit tests run natively on Linux
    cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests

smoke:                       # DLL loads under Wine and hooks Present
    {{run}} sf-wine-test bash scripts/smoke-test.sh

deploy preset="win64-release":
    cp "build/{{preset}}/dinput8.dll" "{{ds3}}/dinput8.dll"
    @echo "Steam launch option: WINEDLLOVERRIDES=\"dinput8=n,b\" %command%"

log:
    tail -f "{{ds3}}/soulsframe.log"

clean:
    rm -rf build
```

## 5. Build files and a working skeleton

### `cmake/toolchains/mingw-w64-x86_64.cmake`
```cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(TC x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${TC}-gcc)
set(CMAKE_CXX_COMPILER ${TC}-g++)
set(CMAKE_RC_COMPILER  ${TC}-windres)
set(CMAKE_FIND_ROOT_PATH /usr/${TC}/sys-root/mingw)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

### `CMakePresets.json`
```json
{
  "version": 6,
  "configurePresets": [
    { "name": "base", "hidden": true, "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": { "CMAKE_EXPORT_COMPILE_COMMANDS": "ON" } },
    { "name": "win64-release", "inherits": "base",
      "toolchainFile": "${sourceDir}/cmake/toolchains/mingw-w64-x86_64.cmake",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "RelWithDebInfo", "SF_PROBE": "OFF" } },
    { "name": "win64-probe", "inherits": "win64-release",
      "cacheVariables": { "SF_PROBE": "ON" } },
    { "name": "host-tests", "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug", "SF_HOST_ONLY": "ON" } }
  ],
  "buildPresets": [
    { "name": "win64-release", "configurePreset": "win64-release" },
    { "name": "win64-probe",   "configurePreset": "win64-probe" },
    { "name": "host-tests",    "configurePreset": "host-tests" }
  ],
  "testPresets": [
    { "name": "host-tests", "configurePreset": "host-tests", "output": { "outputOnFailure": true } }
  ]
}
```

### `cmake/deps.cmake`
```cmake
include(FetchContent)
FetchContent_Declare(minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG        v1.3.4)          # verify the tag; if it has no CMakeLists.txt, pin a master commit
FetchContent_MakeAvailable(minhook)
# Add later, pinned the same way: tomlplusplus, spdlog, zydis, cpp-httplib, doctest, imgui
```
Rule: always a tag or commit hash, never a branch name. Two weeks of "it worked yesterday" is not worth it.

### `CMakeLists.txt`
```cmake
cmake_minimum_required(VERSION 3.25)
project(soulsframe LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(SF_PROBE     "Build RE/telemetry features" OFF)
option(SF_HOST_ONLY "Build only the native pure-C++ parts (tests/tools)" OFF)

if(NOT SF_HOST_ONLY)
  include(cmake/deps.cmake)
  add_library(soulsframe SHARED
    src/core/dllmain.cpp
    src/core/proxy_dinput8.cpp
    src/core/log.cpp
    src/gfx/d3d11/swapchain_hook.cpp)
  target_include_directories(soulsframe PRIVATE src)
  set_target_properties(soulsframe PROPERTIES OUTPUT_NAME "dinput8" PREFIX "")
  target_compile_options(soulsframe PRIVATE -Wall -Wextra)
  target_link_libraries(soulsframe PRIVATE minhook d3d11 dxgi
                        -static -static-libgcc -static-libstdc++)
  if(SF_PROBE)
    target_compile_definitions(soulsframe PRIVATE SF_PROBE=1)
  endif()
else()
  enable_testing()
  # add_subdirectory(tests/unit)  # pure C++ modules only
endif()
```

### `src/core/log.h` / `log.cpp`
```cpp
// log.h
#pragma once
namespace sf::log {
void init(void* module_handle);              // opens soulsframe.log next to the DLL
void write(const char* fmt, ...);
}
#define SF_LOG(...) ::sf::log::write(__VA_ARGS__)
```
```cpp
// log.cpp
#include "core/log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>
namespace sf::log {
static FILE* g_file; static std::mutex g_mu;
void init(void* mod) {
  char path[MAX_PATH]; GetModuleFileNameA(static_cast<HMODULE>(mod), path, MAX_PATH);
  char* slash = strrchr(path, '\\'); if (slash) *(slash + 1) = 0;
  strcat(path, "soulsframe.log");
  g_file = fopen(path, "w");
}
void write(const char* fmt, ...) {
  if (!g_file) return;
  std::lock_guard lk(g_mu);
  va_list ap; va_start(ap, fmt); vfprintf(g_file, fmt, ap); va_end(ap);
  fputc('\n', g_file); fflush(g_file);
}
}
```

### `src/core/proxy_dinput8.cpp` (forwards the one function the game needs)
```cpp
#include <windows.h>
namespace {
using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, const GUID&, LPVOID*, LPUNKNOWN);
Fn real_fn() {
  static Fn fn = [] {
    char dir[MAX_PATH]; GetSystemDirectoryA(dir, MAX_PATH);
    lstrcatA(dir, "\\dinput8.dll");
    HMODULE m = LoadLibraryA(dir);
    return m ? reinterpret_cast<Fn>(GetProcAddress(m, "DirectInput8Create")) : nullptr;
  }();
  return fn;
}
}
extern "C" __declspec(dllexport)
HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD ver, const GUID& riid, LPVOID* out, LPUNKNOWN outer) {
  auto fn = real_fn();
  return fn ? fn(h, ver, riid, out, outer) : E_FAIL;
}
```

### `src/core/dllmain.cpp`
```cpp
#include <windows.h>
#include "core/log.h"
#include "gfx/d3d11/swapchain_hook.h"

static HMODULE g_self;
static DWORD WINAPI init_thread(LPVOID) {
  sf::log::init(g_self);
  SF_LOG("SoulsFrame starting");
  if (!sf::gfx::install_present_hook()) SF_LOG("Present hook FAILED");
  return 0;
}
BOOL APIENTRY DllMain(HINSTANCE h, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_self = h;
    DisableThreadLibraryCalls(h);
    CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr);  // never do real work under the loader lock
  }
  return TRUE;
}
```

### `src/gfx/d3d11/swapchain_hook.h` / `.cpp`
```cpp
// swapchain_hook.h
#pragma once
namespace sf::gfx { bool install_present_hook(); }
```
```cpp
// swapchain_hook.cpp
#include "gfx/d3d11/swapchain_hook.h"
#include "core/log.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <cstdint>

namespace sf::gfx {
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
static PresentFn g_present;

static HRESULT STDMETHODCALLTYPE hooked_present(IDXGISwapChain* sc, UINT sync, UINT flags) {
  static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
  static LARGE_INTEGER last{}; static uint32_t frames = 0; static double acc_ms = 0;
  LARGE_INTEGER now; QueryPerformanceCounter(&now);
  if (last.QuadPart) { acc_ms += double(now.QuadPart - last.QuadPart) * 1000.0 / double(freq.QuadPart); ++frames; }
  last = now;
  if (frames == 120) {
    SF_LOG("avg frame time %.3f ms (%.1f fps)", acc_ms / frames, 1000.0 * frames / acc_ms);
    frames = 0; acc_ms = 0;
  }
  return g_present(sc, sync, flags);
}

bool install_present_hook() {
  // Create a throwaway device+swapchain just to read the vtable. IDXGISwapChain::Present is slot 8.
  WNDCLASSEXA wc{sizeof(wc), CS_CLASSDC, DefWindowProcA, 0, 0, GetModuleHandleA(nullptr),
                 nullptr, nullptr, nullptr, nullptr, "sf_dummy", nullptr};
  RegisterClassExA(&wc);
  HWND hwnd = CreateWindowA("sf_dummy", "", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);

  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 1; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  IDXGISwapChain* sc = nullptr; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
  D3D_FEATURE_LEVEL fl;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                             D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
  if (FAILED(hr)) { SF_LOG("dummy device failed: 0x%08lx", hr); DestroyWindow(hwnd); return false; }

  void* present = (*reinterpret_cast<void***>(sc))[8];
  sc->Release(); ctx->Release(); dev->Release(); DestroyWindow(hwnd); UnregisterClassA("sf_dummy", wc.hInstance);

  if (MH_Initialize() != MH_OK) return false;
  if (MH_CreateHook(present, reinterpret_cast<void*>(&hooked_present), reinterpret_cast<void**>(&g_present)) != MH_OK) return false;
  if (MH_EnableHook(present) != MH_OK) return false;
  SF_LOG("Present hook installed at %p", present);
  return true;
}
}
```
Later (M2+): also hook `IDXGISwapChain::ResizeBuffers` (slot 13) and `IDXGIFactory::CreateSwapChain` so you see the *game's* swap-chain parameters, and hook the `ID3D11DeviceContext` vtable (shared across immediate and deferred contexts, so one hook set covers both).

### `.gitignore` (essentials)
```
build/
re/dumps/
*.log
```

## 6. The run/test loop under Proton

```bash
just build && just deploy
# Steam → DARK SOULS III → Properties → Launch options:
#   WINEDLLOVERRIDES="dinput8=n,b" PROTON_LOG=1 %command%
just log        # tail soulsframe.log
```
- Proton log: `~/steam-374320.log` (`374320` is DS3's Steam app id).
- DXVK overlay for sanity: add `DXVK_HUD=fps,frametimes` to launch options.
- Prefix location: `~/.local/share/Steam/steamapps/compatdata/374320/pfx`.
- Flatpak Steam uses different paths. Set `DS3_DIR` accordingly.

**Remember:** Proton is the dev loop and Windows is the truth. Re-test on real Windows at every milestone.

## 7. Smoke test (Wine in a container, CI-friendly)

`tests/testbed/d3d11_testbed.cpp` (about 60 lines): creates a window, a D3D11 device and swap chain, calls `LoadLibraryA("dinput8.dll")` first, then runs 300 `Present` calls and exits. `scripts/smoke-test.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
cd /src/build/win64-release            # contains dinput8.dll and d3d11_testbed.exe
rm -f soulsframe.log
xvfb-run -a wine d3d11_testbed.exe
grep -q "Present hook installed" soulsframe.log
grep -q "avg frame time" soulsframe.log
echo "smoke OK"
```
It proves hook installation and no crash. It says nothing about performance or visuals.

## 8. Debugging recipes

| Need | How |
|---|---|
| "Did my DLL load?" | `soulsframe.log` + `PROTON_LOG=1` (look for `dinput8.dll` load lines) |
| Crash address → source line | Wine prints the faulting address. Compute the RVA (address minus module base from the log), then `x86_64-w64-mingw32-addr2line -e dinput8.dll -f -C <rva>` (build `RelWithDebInfo`) |
| Step through your own DLL | Logging first. For deeper work use `winedbg --gdb` or gdb attach to the Wine process. Symbol support for PE under host gdb is limited |
| Inspect the game's code/memory | x64dbg under Wine, or `sf-probe` tools (`docs/03`) |
| GPU frame inspection | RenderDoc on Linux captures DXVK's Vulkan output (names are lost, structure is fine). On Windows, RenderDoc captures D3D11 directly |
| Frame times | `DXVK_HUD`, MangoHud, or your own log |
| Wine/Proton hangs | `WINEDEBUG=+loaddll,+seh` for loader/exception tracing, very verbose |

## 9. CI (GitHub Actions sketch)

```yaml
name: build
on: [push, pull_request]
jobs:
  win64:
    runs-on: ubuntu-latest
    container: fedora:43
    steps:
      - uses: actions/checkout@v4
      - run: dnf -y install cmake ninja-build git mingw64-gcc-c++ mingw64-winpthreads-static mingw64-headers mingw64-crt
      - run: cmake --preset win64-release && cmake --build --preset win64-release
      - uses: actions/upload-artifact@v4
        with: { name: dinput8, path: build/win64-release/dinput8.dll }
  host-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests
```
Add the Wine smoke test as a third job once M1 is green.

## 9b. Other tools worth having

| Tool | Use |
|---|---|
| **ccache** | Already in the container; makes rebuilds near-instant |
| **clang-format / clang-tidy** | Consistent style; catches bugs early. Run tidy only on host-compilable code |
| **pre-commit** | Format check + secrets/binary guard so you never commit game files |
| **Tracy profiler** | Instrumented CPU profiling; the client lives in your DLL, the viewer runs natively on Fedora and connects over TCP (works through Proton) |
| **RenderDoc** | GPU frame captures |
| **ImHex** | Hex editor with pattern language, great for TPF/DCX/FXR structures |
| **MangoHud / gamescope** | Frame-time overlays and controlled test conditions on Linux |
