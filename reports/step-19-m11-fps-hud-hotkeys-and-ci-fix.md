# Step 19 — Merge `feat/oc-tasks`, Fix CI Failures, and Add In-Game FPS Viewer & Hotkeys

## Summary

Merged OpenCode's QA/fuzzing/benchmarking branch (`feat/oc-tasks` @ `8dc0600`), fixed both failing GitHub Actions `ci.yml` jobs (`docs` markdownlint and `wine-smoke` `d3dcompiler` `FreeLibrary` use-after-unload crash), and implemented the requested in-game FPS viewer + live keyboard hotkeys (`OverlayModule`).

## Root Causes Fixed in CI

1. **`docs` job failure:** `reports/step-16-m9-texture-swap.md` lacked blank lines below headings (fixed in `8dc0600` on `feat/oc-tasks`).
2. **`wine-smoke` page-fault crash (Ubuntu Wine 9):** `UpscalerModule::Impl::ensure_shaders()` in `src/gfx/d3d11/upscaler_module.cpp` called `FreeLibrary(compiler)` **before** reading `vs_blob`/`ps_blob` buffer pointers and calling `Release()`. Because `ID3DBlob` COM vtables reside inside `d3dcompiler_47.dll`, unloading the compiler DLL early unmapped the vtable pointers and caused an access violation in Ubuntu Wine 9. Keeping `d3dcompiler` loaded until `vs_blob` and `ps_blob` are released resolves the crash cleanly.

## In-Game FPS Viewer & Live Hotkeys Added

- **Pure C++ Runtime Controls (`src/core/runtime_controls.{h,cpp}`):** Case-insensitive key parser (`F1`–`F12`, `Insert`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`, `Pause`, `ScrollLock`, `Numpad0`–`Numpad9`, `"none"`), lock-free `RuntimeControlState`, and debounced rising-edge `HotkeyTracker`.
- **Pure C++ 8x8 Bitmap HUD Rasterizer (`src/gfx/fx/hud_overlay.{h,cpp}`):** Renders a crisp 8x8 ASCII bitmap panel showing version, FPS, average frame time, 1% low, active/paused state, and hotkey toggle legend (`top_left`, `top_right`, `bottom_left`, `bottom_right`) without external font files.
- **D3D11 Overlay Module (`src/gfx/d3d11/overlay_module.{h,cpp}`):** Polls configured hotkeys via `GetAsyncKeyState()` once per `Present()` and draws the RGBA8 HUD texture with point filtering and alpha blending, releasing cached targets cleanly on `on_resize()`.
- **Live Feature Toggles:** Wired `sf::runtime_controls()` into `UpscalerModule`, `ParticleModule`, `PresentationModule` frame pacer, and `FpsUnlockModule`:
  - `F9`: Show/hide HUD
  - `F10`: Master mod pause/resume bypass (without closing the game)
  - `F11`: Toggle FSR 1 / RCAS upscaler
  - `F8`: Toggle particle overdraw mitigation
  - `F12`: Toggle FPS limiter (`present.max_fps`)
- **Config & Diagnostics:** Added `modules.overlay`, `[overlay]`, and `[hotkeys]` parsing plus `SF_CFG_HOTKEYS_DUPLICATE` and `SF_CFG_OVERLAY_HIDDEN_NO_HOTKEYS` diagnostics.

## Verification

- `clang-format --dry-run --Werror`: clean across all `.h`/`.cpp` files.
- `bash scripts/ci/check-guards.sh`: passed.
- `ctest --preset host-tests --output-on-failure`: 2/2 passed.
- `python3 tools/py/{bake_textures,analyze_capture,plot_gpu_passes}.py --self-test`: passed.
- Coverage (`gcovr` on `host-cov`): 94.9% lines (1927/2031), 97.9% functions (186/190), 82.0% branches (1667/2032) — above the 90% gate.
- Release `host-bench`: all 17 micro-benchmarks compiled with `-DSF_WERROR=ON` and ran cleanly.
- MinGW `win64-release` & `win64-probe` builds + PE audit + Wine smoke suite (`sf-wine-test` with Scenario 3h overlay + upscaler + resize): passed.
