# Step 1 Report — M0 Toolchain + M1 First-Light Foundation

**Date:** 2026-11-15  
**Scope:** Milestones M0 (Toolchain, containers, CI, unit harness) and M1 (`dinput8.dll` proxy, safety guards, DXGI `Present`/`ResizeBuffers` hooks, Wine smoke verification).

## 1. What was built

1. **Build, container, and dependency infrastructure (M0):**
   - `CMakeLists.txt` + `CMakePresets.json` (`win64-release`, `win64-probe`, `host-tests`, `host-asan`).
   - `cmake/toolchains/mingw-w64-x86_64.cmake` auto-detecting Fedora and Ubuntu MinGW sysroots.
   - `cmake/deps.cmake` pinning `tomlplusplus` (`v3.4.0`), `xxHash` (`v0.8.4`), `MinHook` (`v1.3.4`), and `doctest` (`v2.5.3`) with `BUILD_SHARED_LIBS=OFF`.
   - `docker/build.Dockerfile` (`sf-build`) and `docker/wine-test.Dockerfile` (`sf-wine-test`), plus `justfile`, `.clang-format`, `.clang-tidy`, `LICENSE` (MIT), and `THIRD_PARTY_LICENSES.txt`.
2. **Pure C++ core & DS3 profile seam (Linux-native):**
   - `src/core/config.{h,cpp}`: TOML parsing with bounds clamping, unknown-key warnings, and syntax-error fallback.
   - `src/core/offline_guard.{h,cpp}`: enforces `general.offline_acknowledged = true` and rejects `--online` / active online state.
   - `src/core/module.h` & `src/core/module_registry.{h,cpp}`: isolates per-module init failures, kill switches, and unknown-exe engine-hook refusal.
   - `src/core/sha256.{h,cpp}`: zero-dependency SHA-256 implementation for executable pinning.
   - `src/timing/frame_stats.{h,cpp}`: `IClock`-injected rolling frame-time metrics (average, FPS, 1% / 0.1% lows, >2x median spikes).
   - `src/tex/content_hash.{h,cpp}`: deterministic `XXH64` byte/string hashing.
   - `src/hooks/sigscan.{h,cpp}`: IDA-style byte-pattern scanner refusing ambiguous multi-matches.
   - `src/games/ds3/profile.{h,cpp}`: build-hash lookup keeping `engine_hooks_allowed = false` on unverified builds.
3. **Windows `dinput8.dll` proxy & D3D11 hooks (M1):**
   - `src/core/proxy_dinput8.cpp`: exports `DirectInput8Create` forwarding to `%SystemRoot%\System32\dinput8.dll`.
   - `src/core/log.{h,cpp}` & `src/core/win_helpers.{h,cpp}`: thread-safe `soulsframe.log`, QPC `WinClock`, executable SHA-256 computation, and overlay conflict detection.
   - `src/core/dllmain.cpp` & `src/gfx/d3d11/swapchain_hook.{h,cpp}`: spawns background `init_thread`, evaluates offline guard, hooks `IDXGISwapChain::Present` (slot 8) and `ResizeBuffers` (slot 13) via MinHook, and logs rolling 120-frame stats.
4. **Documentation, AI context, & CI/Release automation:**
   - Added `docs/08-logging-and-telemetry.md`, `docs/09-codebase-reference.md`, `docs/guide-for-vibe-coders.md`, `AGENTS.md`, and `.github/copilot-instructions.md`.
   - Configured `.github/workflows/ci.yml` (guards, g++/clang++ host tests, ASan/UBSan, MinGW release+probe builds, DLL audit, Wine smoke test, docs lint) and `.github/workflows/release.yml` (tagged zip packaging, `SHA256SUMS.txt`, provenance attestation, GitHub Release).

## 2. Verification results observed

| Check | Outcome | Observed details |
|---|---|---|
| `bash scripts/ci/check-guards.sh` | Passed | 0 forbidden binaries/archives tracked; 0 Win32/D3D11 includes in pure modules. |
| `clang-format --dry-run --Werror` | Passed | All `src/` and `tests/` files conform cleanly. |
| `ctest --preset host-tests` | Passed | `15 / 15` doctest cases, `177 / 177` assertions passed in 0.00 s. |
| `win64-release` & `win64-probe` (`sf-build`) | Passed | Built `dinput8.dll` and `d3d11_testbed.exe` with `-Wall -Wextra -Wpedantic` and zero warnings. |
| `x86_64-w64-mingw32-objdump -p dinput8.dll` | Passed | Exports `DirectInput8Create`; imports only `d3d11.dll`, `KERNEL32.dll`, `msvcrt.dll`, `USER32.dll` (no `libstdc++`, `libgcc_s`, `libwinpthread`, or `minhook.x64.dll`). |
| `scripts/smoke-test.sh` (`sf-wine-test`) | Passed | Verified offline guard blocks without opt-in; with opt-in logged `Present hook installed`, `IDXGISwapChain::ResizeBuffers(640x480...)`, and `avg frame time 0.604 ms` / `1.025 ms` across 300 frames. |

## 3. Issues discovered & resolved during Step 1

1. **CMake 4.x compatibility:** Host runs CMake 4.3.0, which rejects older `cmake_minimum_required` bounds in third-party subprojects; fixed with `CMAKE_POLICY_VERSION_MINIMUM 3.5` in `cmake/deps.cmake`.
2. **MinHook shared library leakage:** `xxHash` subproject option evaluation caused MinHook to build `minhook.x64.dll` initially; forced `set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)` before `FetchContent_MakeAvailable`.
3. **Wine builtin `dinput8` override:** Wine loads its own builtin `dinput8.dll` unless `WINEDLLOVERRIDES="dinput8=n,b"` is exported; updated `scripts/smoke-test.sh`, `docker/wine-test.Dockerfile`, and `.github/workflows/ci.yml`.
4. **Rootless Podman `/wine` prefix ownership:** Image-built `/wine` is root-owned; `scripts/smoke-test.sh` now falls back to `/tmp/sf-wine-prefix` when `/wine` is not owned by the active UID.

## 4. Next step (M2)

- Add `SF_PROBE` draw-call tracer (`ID3D11DeviceContext` vtable hooks), caller RVA aggregation, lock-free JSONL capture ring buffer (`bench/*.jsonl`), and `tools/py/` analysis scripts.
- Verify `dinput8.dll` directly on Windows and Proton with `DarkSoulsIII.exe` (offline).
