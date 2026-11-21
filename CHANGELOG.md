# Changelog

All notable milestones implemented in SoulsFrame up to `v1.0.0` (pending final real-Windows DS3 hardware validation).

## [1.0.0] - 2026-11-21

### Added

- **Version `1.0.0` & Copy-Paste Release Package (`scripts/package-release.sh`, `config/README-INSTALL.txt`):** Unified `CMakeLists.txt` (`project(soulsframe VERSION 1.0.0)`), `src/core/version.h`, DLL banners, and `sftool version`; added simple step-by-step `README.txt` with hotkey explanations (`F8`–`F12`), enabled the FPS overlay by default in `soulsframe.toml.example` (while keeping `offline_acknowledged = false`), and packaged ready-to-extract release zips.
- **In-game FPS viewer & live hotkeys (`src/core/runtime_controls.{h,cpp}`, `src/gfx/fx/hud_overlay.{h,cpp}`, `src/gfx/d3d11/overlay_module.{h,cpp}`):** Built-in 8x8 bitmap FPS/avg-ms/1%-low + status badge HUD and debounced runtime toggles (`F9` HUD, `F10` master pause/resume bypass, `F11` FSR 1, `F8` particles, `F12` FPS cap) without restarting the game.

## [0.10.0] - 2026-11-21

### Added

- **Version source of truth:** Unified `CMakeLists.txt`, `src/core/version.h`, DLL banners, `sf-probe` session headers, and `sftool version`.
- **Cross-field configuration diagnostics (`src/core/config_diagnostics.{h,cpp}`):** Detects inactive `[present]` settings, throttling no-ops, upscaler preset overrides, FPS cap conflicts, probe flags on release DLLs, and risky texture paths at startup and offline via `sftool config-check <toml> [--probe] [--verified-exe]`.
- **Severity-filtered logging (`src/core/log.{h,cpp}`):** Actually enforces `general.log_level` (`debug`, `info`, `warn`, `error`) with `[DEBUG]`/`[INFO]`/`[WARN]`/`[ERROR]` tags across all modules.
- **Swap-chain resize lifecycle (`sf::Module::on_resize()`, `ModuleRegistry::on_resize()`):** Ensures modules such as `UpscalerModule` release swap-chain backbuffer RTV/texture references before `IDXGISwapChain::ResizeBuffers` executes.
- **Configuration presets (`config/presets/`):** Added `safe-frame-pacing.toml`, `low-end-gpu.toml`, and `probe-capture.toml`, all enforcing `offline_acknowledged = false` by default and verified by `scripts/ci/check-guards.sh`.
- **Release documentation & workflow hardening:** Added `docs/15-windows-validation-and-release.md`, expanded Wine smoke tests (Scenarios 3d `--resize`, 3f M9 texture swap, 3g M10 diagnostics/`log_level`), and added PE mitigation checks + preset/tool bundling in `.github/workflows/release.yml`.

### Earlier Milestones Included in `v0.10.0`

- **M0 & M1:** DirectInput8 proxy DLL, MinHook `Present`/`ResizeBuffers` telemetry, offline safety guard, reproducible Fedora MinGW & Wine Dockerfiles, strict CI guards.
- **M2 (`sf-probe` v1):** 7-slot `ID3D11DeviceContext` tracer, async JSONL writer, pass-boundary detection, Python capture analyzer, libFuzzer targets.
- **M3:** D3D11 timestamp GPU pass timing (`gpu_ms`), `GpuFrameJoiner`, Release `host-bench` micro-benchmarks.
- **M4:** PE32+ dump & Ghidra section alignment fixup (`pe_dump`, `win_dump`), SHA-256 signature DB (`sig_db`), RIP-relative resolver, native `sftool` CLI, headless Ghidra symbol exporter.
- **M5:** DXGI `FLIP_DISCARD` policy with tearing support and QPC/sleep+spin `FramePacer` (`PresentationModule`).
- **M6:** Particle overdraw mitigation via low-res viewport scaling and instanced/per-frame draw budgets (`ParticleModule`).
- **M7:** Spatial upscaling + RCAS contrast-adaptive sharpening (`UpscalerModule`, `fsr1_math`).
- **M8:** Experimental fixed-step 60 Hz simulation accumulator and dry-run safety gating (`FpsUnlockModule`, `FixedStepStepper`).
- **M9:** Content-hash `CreateTexture2D` DDS replacement hook with VRAM budget (`TextureModule`, `texture_pack`) and `tools/py/bake_textures.py`.
