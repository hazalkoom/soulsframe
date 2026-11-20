# Step 14 — Milestone 7: Render Scale & FSR 1 Spatial Upscaler

## Summary

Implemented Milestone 7 (`UpscalerModule` + pure FSR 1 math), enabling configurable spatial upscaling presets and RCAS sharpening behind `[modules] upscaler = false` and `[upscaler]`.

## What was added

- **Configuration (`src/core/config.h`, `src/core/config.cpp`, `config/soulsframe.toml.example`):**
  - Added `[modules] upscaler` kill switch (default `false`).
  - Added `[upscaler]` table with `mode` (`"none" | "bilinear" | "fsr1" | "rcas_only"`), `preset` (`"native" | "ultra_quality" | "quality" | "balanced" | "performance" | "custom"`), `scale_percent` (`[50, 100]`), and `sharpness_percent` (`[0, 100]`).
- **Pure FSR 1 math (`src/gfx/fx/fsr1_math.h`, `src/gfx/fx/fsr1_math.cpp`):**
  - Zero Win32/D3D11 headers; protected by `scripts/ci/check-guards.sh`.
  - Computes preset-to-scale mapping, `UpscaleDimensions`, `EasuConstants`, and `RcasConstants` cleanly on native Linux.
- **D3D11 runtime (`src/gfx/d3d11/upscaler_module.h`, `src/gfx/d3d11/upscaler_module.cpp`, `src/core/dllmain.cpp`):**
  - `UpscalerModule` compiles SM 4.0 fullscreen VS + RCAS PS via `d3dcompiler_47.dll` at runtime, manages resize-safe intermediate SRV/RTV targets on non-MSAA swap chains, restores caller D3D11 pipeline state after each pass, and logs per-frame EASU/RCAS counts on shutdown.
- **Tests & Wine verification (`tests/unit/test_fsr1_math.cpp`, `tests/unit/test_config.cpp`, `scripts/smoke-test.sh`):**
  - Added unit tests for presets, mode matrix, EASU/RCAS constant calculation, and config bounds.
  - Added Scenario 3d in `scripts/smoke-test.sh` verifying `UpscalerModule` active + 20 EASU/RCAS frames under Wine.
