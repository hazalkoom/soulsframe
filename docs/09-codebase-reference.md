# 09. Codebase reference (Step 1: M0/M1 foundation)

This reference explains every directory, source file, and symbol currently implemented in the repository.

## 1. Execution flow at startup

```mermaid
flowchart TD
  A["DarkSoulsIII.exe / testbed loads dinput8.dll"] --> B["DllMain (DLL_PROCESS_ATTACH)"]
  B --> C["Spawn init_thread (outside loader lock)"]
  C --> D["sf::log::init() opens soulsframe.log"]
  D --> E["sf::load_config_file(soulsframe.toml)"]
  E --> F{"general.mod_enabled?"}
  F -- no --> Z["Exit init_thread cleanly"]
  F -- yes --> G["sf::evaluate_offline_guard()"]
  G -- blocked --> Z
  G -- allowed --> H["compute_host_exe_sha256() + resolve_profile()"]
  H --> I["ModuleRegistry::init_all(profile, cfg)"]
  I --> J["FrameTelemetryModule::init() -> install_swapchain_hooks()"]
  J --> K["hooked_present() ticks FrameStats & calls ModuleRegistry::on_present()"]
```

## 2. Build & tooling files

| File | Purpose |
|---|---|
| [CMakeLists.txt](../CMakeLists.txt) | Root build graph: builds pure static library `soulsframe_core`, plus either Windows `dinput8.dll` + `d3d11_testbed.exe` or native Linux `soulsframe_unit_tests`. |
| [CMakePresets.json](../CMakePresets.json) | Named configurations: `win64-release`, `win64-probe`, `host-tests`, `host-asan`. |
| [cmake/deps.cmake](../cmake/deps.cmake) | Pinned `FetchContent` dependencies (`tomlplusplus` v3.4.0, `xxHash` v0.8.4, `MinHook` v1.3.4, `doctest` v2.5.3) with `BUILD_SHARED_LIBS=OFF`. |
| [cmake/toolchains/mingw-w64-x86_64.cmake](../cmake/toolchains/mingw-w64-x86_64.cmake) | Cross-compile toolchain supporting both Fedora and Debian/Ubuntu MinGW sysroot layouts. |
| [docker/build.Dockerfile](../docker/build.Dockerfile) | Reproducible Fedora 43 MinGW-w64 + CMake + Ninja + ccache + clang-tools container (`sf-build`). |
| [docker/wine-test.Dockerfile](../docker/wine-test.Dockerfile) | Headless Fedora 43 + Wine + Xvfb + Mesa container (`sf-wine-test`). |
| [justfile](../justfile) | Task runner shortcuts (`img-build`, `build`, `probe`, `test-host`, `smoke`, `deploy`). |
| [config/soulsframe.toml.example](../config/soulsframe.toml.example) | Default documented configuration template shipped to users. |

## 3. Pure C++ modules (Linux-testable, no Win32/D3D11)

| Module | Key symbols | Responsibility |
|---|---|---|
| [src/core/config.h](../src/core/config.h) / [.cpp](../src/core/config.cpp) | `sf::Config`, `sf::parse_config_toml()`, `sf::load_config_file()` | TOML parsing with bounds clamping, warning collection, and safe fallback on syntax errors. |
| [src/core/offline_guard.h](../src/core/offline_guard.h) / [.cpp](../src/core/offline_guard.cpp) | `sf::OfflineCheckInput`, `sf::evaluate_offline_guard()` | Enforces explicit `offline_acknowledged = true` opt-in and rejects online flags/session state. |
| [src/core/module.h](../src/core/module.h) | `sf::Module`, `sf::PresentCtx` | Plugin interface for features; uses opaque `void* swap_chain` to keep core decoupled from D3D11. |
| [src/core/module_registry.h](../src/core/module_registry.h) / [.cpp](../src/core/module_registry.cpp) | `sf::ModuleRegistry`, `sf::ModuleStatus` | Initializes, updates (`on_present`), and shuts down modules independently with exception isolation, config kill switches, and `requires_engine_hooks()` checks. |
| [src/core/sha256.h](../src/core/sha256.h) / [.cpp](../src/core/sha256.cpp) | `sf::sha256_bytes()`, `sf::sha256_hex()` | Zero-dependency SHA-256 implementation for executable version pinning. |
| [src/timing/frame_stats.h](../src/timing/frame_stats.h) / [.cpp](../src/timing/frame_stats.cpp) | `sf::timing::IClock`, `sf::timing::FrameStats`, `sf::timing::FrameMetrics` | Rolling ring-buffer frame-time stats (avg, FPS, 1%/0.1% lows, >2x median spikes). |
| [src/tex/content_hash.h](../src/tex/content_hash.h) / [.cpp](../src/tex/content_hash.cpp) | `sf::tex::hash_bytes()`, `sf::tex::hash_hex()` | Fast 64-bit `XXH64` content hashing for future texture and shader registries. |
| [src/hooks/sigscan.h](../src/hooks/sigscan.h) / [.cpp](../src/hooks/sigscan.cpp) | `sf::hooks::parse_pattern()`, `sf::hooks::sigscan()` | IDA-style byte-signature parser/scanner that refuses ambiguous multiple matches. |
| [src/games/ds3/profile.h](../src/games/ds3/profile.h) / [.cpp](../src/games/ds3/profile.cpp) | `sf::GameProfile`, `sf::games::ds3::resolve_profile()` | Build-hash lookup; unknown hashes disable engine hooks while keeping telemetry active. |

## 4. Windows DLL & D3D11 hook layer

| File | Key symbols | Responsibility |
|---|---|---|
| [src/core/proxy_dinput8.cpp](../src/core/proxy_dinput8.cpp) | `DirectInput8Create` | Exports `DirectInput8Create` and forwards calls to `%SystemRoot%\System32\dinput8.dll`. |
| [src/core/log.h](../src/core/log.h) / [.cpp](../src/core/log.cpp) | `sf::log::init()`, `SF_LOG()`, `sf::log::module_dir()` | Thread-safe logger creating `soulsframe.log` alongside `dinput8.dll`. |
| [src/core/win_helpers.h](../src/core/win_helpers.h) / [.cpp](../src/core/win_helpers.cpp) | `sf::WinClock`, `compute_host_exe_sha256()`, `detect_known_overlays()` | QPC monotonic clock, executable hashing, CLI tokenization, conflicting overlay detection. |
| [src/gfx/d3d11/swapchain_hook.h](../src/gfx/d3d11/swapchain_hook.h) / [.cpp](../src/gfx/d3d11/swapchain_hook.cpp) | `install_swapchain_hooks()`, `hooked_present()`, `hooked_resize_buffers()` | Creates a dummy D3D11 device to resolve `IDXGISwapChain` vtable slots 8 & 13 and hooks them with MinHook. |
| [src/core/dllmain.cpp](../src/core/dllmain.cpp) | `DllMain`, `init_thread`, `FrameTelemetryModule` | Spawns background initialization outside loader lock and orchestrates clean shutdown. |

## 5. Tests and verification scripts

| File | Layer | Purpose |
|---|---|---|
| [tests/unit/](../tests/unit/) | L1 (Linux native) | 8 `doctest` suites (23 test cases, 605 assertions) stress-testing all pure C++ modules with `SF_WERROR=ON`. |
| [tests/testbed/d3d11_testbed.cpp](../tests/testbed/d3d11_testbed.cpp) | L4/L5 (Wine/Windows) | Minimal D3D11 window that loads `dinput8.dll`, renders frames, and optionally calls `ResizeBuffers` or `--msaa 4`. |
| [scripts/smoke-test.sh](../scripts/smoke-test.sh) | L4 | 3-scenario Wine smoke suite testing blocked offline-guard behavior, 300-frame `ResizeBuffers`, and 150-frame MSAA 4x swapchains. |
| [scripts/ci/check-guards.sh](../scripts/ci/check-guards.sh) | L0 | Rejects tracked game binaries/zips, `<windows.h>`/`<d3d11.h>` leaks in pure modules, RVA leaks outside `ds3/`, unpinned tags, missing licenses, weak offline defaults, and missing `///` header docstrings. |
| [.github/workflows/](../.github/workflows/) | CI/CD | Automated `ci.yml` (`guards`, `gitleaks`, strict `docs`, `static-analysis`, `host-tests`, `win64`, `wine-smoke`, `docker-verify`), `codeql.yml`, `auto-merge.yml`, and `release.yml`. |
