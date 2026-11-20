# Step 13 — Milestone 6: Particle Mitigation (`ParticleModule` & `ParticlePassPolicy`)

## Summary

Implemented Milestone 6 (Particle mitigation) across pure Linux-tested DS3 heuristics and the D3D11/MinHook runtime layer, guarded behind `[modules] particles = false` and `[particles]`.

## What was added

- **Configuration (`src/core/config.h`, `src/core/config.cpp`, `config/soulsframe.toml.example`):**
  - Added `[modules] particles` kill switch (default `false`).
  - Added `[particles]` table with `mode` (`"auto" | "throttle" | "half_res"`), `scale_percent` (`[25, 100]`, default `50`), `max_instances_per_draw` (`[0, 4096]`), and `max_draws_per_frame` (`[0, 16384]`).
- **Pure DS3 heuristics & policy (`src/games/ds3/profile.h`, `src/games/ds3/render.h`, `src/games/ds3/render.cpp`):**
  - Zero Win32/D3D11 headers; protected by `scripts/ci/check-guards.sh`.
  - `is_particle_draw()` separates single-RTV blended read-only-depth particle passes from opaque, G-buffer MRT (`>= 2` RTVs), shadow (`0` RTVs), and tiny UI targets (`< 128`).
  - `compute_low_res_viewport()` scales particle viewport geometry down to `scale_percent` and tracks pixel-fill reduction (`75%` fewer pixels at `50%`).
  - `ParticlePassPolicy` enforces `max_instances_per_draw`, `max_draws_per_frame`, and low-resolution scaling across `"auto"`, `"throttle"`, and `"half_res"` modes.
- **D3D11 runtime (`src/gfx/d3d11/particle_module.h`, `src/gfx/d3d11/particle_module.cpp`, `src/gfx/d3d11/context_hook.cpp`, `src/core/dllmain.cpp`):**
  - `ParticleModule` hooks `ID3D11DeviceContext` draw slots (`12`, `13`, `20`, `21`) standalone in `win64-release` and cooperates seamlessly via `filter_d3d11_draw()` / `finish_d3d11_draw()` with `SF_PROBE` `context_hook` in `win64-probe`.
  - Temporarily scales active `D3D11_VIEWPORT` rectangles for detected particle draws and restores them immediately after the draw returns.
- **Tests & verification (`tests/unit/test_particles.cpp`, `tests/unit/test_config.cpp`, `scripts/smoke-test.sh`):**
  - Added unit coverage for particle heuristic classification, viewport downscaling math, config bounds, and per-frame budget state transitions.
  - Added Scenario 3c in `scripts/smoke-test.sh` verifying live `d3d11_testbed.exe --frames 30 --particles` mitigation under Wine.
