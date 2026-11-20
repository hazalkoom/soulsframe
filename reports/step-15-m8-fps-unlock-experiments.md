# Step 15 — Milestone 8: FPS Unlock Experiments & Fixed-Step Accumulator

## Summary

Implemented Milestone 8 (`FpsUnlockModule` + pure `FixedStepStepper` accumulator + M8 experiment log), decoupling high-refresh rendering from DS3's 60 Hz simulation loop behind `[modules] fps_unlock = false` and `[fps_unlock]`.

## What was added

- **Configuration (`src/core/config.h`, `src/core/config.cpp`, `config/soulsframe.toml.example`):**
  - Added `[modules] fps_unlock` kill switch (default `false`).
  - Added `[fps_unlock]` table (`strategy = "fixed_step" | "camera_only" | "variable_dt"`, `target_fps` clamped `[60, 360]`, `sim_tick_hz` clamped `[30, 120]`, `max_substeps` clamped `[1, 8]`).
- **Pure C++20 Fixed-Step Accumulator & Safety Evaluator (`src/timing/fixed_step.h`, `src/timing/fixed_step.cpp`):**
  - Zero Win32/D3D11 headers; protected by `scripts/ci/check-guards.sh`.
  - `FixedStepStepper` decouples 60 Hz physics ticks from unlocked frame rates with interpolation alpha tracking and spiral-of-death clamping (`max_substeps`).
  - `evaluate_fps_unlock_safety()` enforces `dry_run = true` unless `profile.engine_hooks_allowed == true` AND both `frame_dt_ptr` and `frame_limiter` addresses are verified.
- **Runtime Module & Research Log (`src/timing/fps_unlock_module.h`, `src/timing/fps_unlock_module.cpp`, `src/core/dllmain.cpp`, `re/notes/fps_unlock_experiments.md`):**
  - Documented the 10 known DS3 >60 Hz simulation coupling hazards and evaluated Strategies A–D.
- **Tests & Wine Verification (`tests/unit/test_fixed_step.cpp`, `tests/unit/test_config.cpp`, `scripts/smoke-test.sh`):**
  - Added deterministic tests for 120 Hz -> 60 Hz tick accumulation, spiral clamping, `camera_only`, `variable_dt`, and safety verdicts.
  - Added Scenario 3e in `scripts/smoke-test.sh` verifying `FpsUnlockModule` active (`dry_run=true`) over 30 Wine frames.
