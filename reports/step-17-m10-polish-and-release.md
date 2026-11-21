# Step 17 — Milestone 10: Polish, Config UX & Release Hardening (`v0.10.0-rc`)

## Summary

Completed Milestone 10 across pure C++ diagnostics, severity-filtered logging, swap-chain resize resource cleanup, `sftool` config validation, ready-to-use presets, expanded Wine smoke verification (covering M9 `TextureModule` live DDS swap and M10 diagnostics/resize cleanup), Windows validation documentation, and release packaging hardening.

## What Was Built

1. **Unified Version Source of Truth (`0.10.0`):**
   - Set `project(soulsframe VERSION 0.10.0 LANGUAGES C CXX)` in `CMakeLists.txt`.
   - Added pure header `src/core/version.h` (`sf::kVersionString = "0.10.0"`, `sf::kProductName = "SoulsFrame"`).
   - Wired version banners into `src/core/dllmain.cpp`, `src/probe/probe_module.cpp` (`hdr.mod_version`), and `tools/cpp/sftool_main.cpp`.

2. **Pure Cross-Field Config Diagnostics (`src/core/config_diagnostics.{h,cpp}`):**
   - Added 12 static diagnostics (`SF_CFG_MOD_DISABLED`, `SF_CFG_OFFLINE_UNACK`, `SF_CFG_PRESENT_INACTIVE`, `SF_CFG_TELEMETRY_OFF_WITH_PRESENT_MODS`, `SF_CFG_PARTICLES_THROTTLE_NOOP`, `SF_CFG_UPSCALER_MODE_NONE`, `SF_CFG_UPSCALER_PRESET_OVERRIDES_SCALE`, `SF_CFG_FPS_UNLOCK_EXPERIMENTAL`, `SF_CFG_FPS_CAP_CONFLICT`, `SF_CFG_PROBE_ON_RELEASE`, `SF_CFG_TEXTURES_DUMP_KEYS`, `SF_CFG_TEXTURES_PACK_DIR_ABS`) and `sf::format_config_summary()`.

3. **Runtime Log Levels, Resize Lifecycle, and MinHook Hygiene:**
   - Implemented `general.log_level` (`debug`, `info`, `warn`, `error`) filtering and `[DEBUG]`/`[INFO]`/`[WARN]`/`[ERROR]` prefixes in `src/core/log.{h,cpp}`.
   - Added `sf::Module::on_resize()` and fault-isolated `ModuleRegistry::on_resize()` invoked before `IDXGISwapChain::ResizeBuffers` in `src/gfx/d3d11/swapchain_hook.cpp`.
   - Implemented `UpscalerModule::on_resize()` so cached swap-chain RTV/texture references are released prior to DXGI buffer reallocation (`--resize` verified in Wine).
   - Scoped `swapchain_hook.cpp` MinHook enable/disable/uninit strictly to its own targets (`g_present_target`, `g_resize_target`) and shared `g_owns_mh_init` safely with other modules.
   - Kept `FrameTelemetryModule` swap-chain hooks active silently when other `Present()`-driven modules are enabled with `modules.frame_telemetry = false`.

4. **`sftool` CLI, Safe Presets, and Release Workflow:**
   - Added `sftool version` and `sftool config-check <soulsframe.toml> [--probe] [--verified-exe]` in `tools/cpp/sftool_main.cpp` using `validate_cli_path()`.
   - Added `config/presets/{safe-frame-pacing,low-end-gpu,probe-capture}.toml` (all enforcing `offline_acknowledged = false`) and extended `scripts/ci/check-guards.sh` to guard all `config/presets/*.toml`.
   - Hardened `.github/workflows/release.yml` with `HIGH_ENTROPY_VA`/`DYNAMIC_BASE`/`NX_COMPAT` checks, `v0.10.0` default tag, and bundled presets, `CHANGELOG.md`, `WINDOWS_SETUP.md`, and `tools/py/{bake_textures,analyze_capture}.py`.

5. **Tests, Smoke Coverage, and Documentation:**
   - Added `tests/unit/test_config_diagnostics.cpp` and `on_resize()` fault-isolation tests in `tests/unit/test_module_registry.cpp`.
   - Added `--textures` mode to `tests/testbed/d3d11_testbed.cpp`, fixed `REPO_ROOT` resolution in `scripts/smoke-test.sh`, added `--resize` to Scenario 3d (`UpscalerModule`), and added Scenario 3f (M9 live DDS swap) and Scenario 3g (M10 diagnostics + `log_level = "warn"`).
   - Added `docs/15-windows-validation-and-release.md` and `CHANGELOG.md`, and updated `README.md`, `docs/09-codebase-reference.md`, and `AGENTS.md`.

## Verification Summary

- **Formatting (`clang-format` in `sf-build`):** Clean across all modified/new C++ headers and sources.
- **Repository & Layering Guards (`bash scripts/ci/check-guards.sh`):** `All repository guards passed.`
- **Host Unit Tests (`ctest --preset host-tests`):** `2/2` passed (`soulsframe_unit_tests`, `sftool_help_smoke`).
- **`sftool` Config Checks:** `sftool version` printed `SoulsFrame sftool v0.10.0`; `sftool config-check` exited `0` with `Parse: OK` and `Offline: BLOCKED` on `config/soulsframe.toml.example` and all 3 `config/presets/*.toml`.
- **Host ASan (`host-asan`):** Not linked on bare Fedora host because `/usr/lib64/libubsan.so.1.0.0` is absent locally (runs on Ubuntu in GitHub Actions `ci.yml`).
- **Windows x64 Cross-Compiles (`win64-release` & `win64-probe`, `-DSF_WERROR=ON`):** Both built with zero errors; `x86_64-w64-mingw32-objdump -p` verified `DirectInput8Create` export, `HIGH_ENTROPY_VA`, `DYNAMIC_BASE`, `NX_COMPAT`, and zero dynamic runtime imports.
- **Headless Wine Smoke Suite (`sf-wine-test`):** `smoke OK (probe=true)` — all 10 scenarios passed (`scenario_1` through `scenario_4`, including `3d` `--resize` `resizes=1`, `3f` live texture swap `replaced=1`, and `3g` `log_level=warn` + `SF_CFG_PRESENT_INACTIVE`).
- **Pending:** Native Windows 10/11 Dark Souls III hardware validation & A/B frame-time benchmarks prior to tagging `v0.10.0`.
