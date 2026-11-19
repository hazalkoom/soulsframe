# Step 12 — Milestone 5: Presentation Flip-Model & Hybrid Frame Pacer

## Summary

Implemented Milestone 5 (Presentation fixes) with strict layering between pure Linux-tested presentation logic and the Win32/DXGI hook implementation, wired into `dinput8.dll` behind the `[modules] presentation` and `[present]` kill switches.

## What was added

- **Pure C++20 modules (`soulsframe_core`, zero Win32/D3D11 headers):**
  - `src/timing/frame_pacer.h` / `src/timing/frame_pacer.cpp`: `sf::timing::FramePacer` hybrid sleep+spin frame-rate limiter (`[present] max_fps`, clamped to `[0, 360]`) with cadence-preserving deadline stepping and hitch recovery (no catch-up bursts after slow frames).
  - `src/timing/present_policy.h` / `src/timing/present_policy.cpp`: `sf::timing::evaluate_flip_upgrade()` and `sf::timing::resolve_present_call()` enforcing DXGI flip-model compatibility rules (windowed only, `sample_count == 1`, flip-compatible back-buffer formats, `BufferCount >= 2`, and safe `DXGI_PRESENT_ALLOW_TEARING` gating).
- **D3D11/DXGI hook module (`src/gfx/d3d11/`):**
  - `src/gfx/d3d11/presentation_module.h` / `src/gfx/d3d11/presentation_module.cpp`: `sf::gfx::PresentationModule` implementing `sf::Module`.
    - Hooks `IDXGIFactory::CreateSwapChain` (vtable slot 10) via MinHook when `[present] mode = "flip"` to upgrade compatible windowed swap chains to `DXGI_SWAP_EFFECT_FLIP_DISCARD`, with automatic fallback to the game's original descriptor if DXGI rejects the upgrade.
    - Runs `FramePacer` with a `QueryPerformanceCounter` + `timeBeginPeriod(1)` / `Sleep` / `SwitchToThread` clock in `on_present()` when `max_fps > 0`.
  - Extended `sf::PresentCtx` (`src/core/module.h`) with pure `sync_interval` and `present_flags` fields so `swapchain_hook.cpp` can forward adjusted `Present()` flags without leaking DXGI types into `src/core/`.
- **Unit tests & Wine smoke verification:**
  - `tests/unit/test_frame_pacer.cpp`: deterministic virtual-clock tests covering disabled/clamped FPS, 60 FPS hybrid sleep+spin accuracy, hitch recovery, OS sleep overshoot, and stalled sleep clocks.
  - `tests/unit/test_present_policy.cpp`: kill-switch, flip upgrade, MSAA/fullscreen/format rejection, and `ALLOW_TEARING` safety tests.
  - `scripts/smoke-test.sh`: added Scenario 3b exercising `modules.presentation = true`, `mode = "flip"`, and `max_fps = 60` live under Wine.

## Verification

- `bash scripts/ci/check-guards.sh`: passed (including new pure-file guards on `frame_pacer` and `present_policy`).
- `ctest --preset host-tests`: 74/74 doctest cases + `sftool_help_smoke` passed.
- `win64-release` and `win64-probe` cross-compiles in `sf-build`: passed with zero warnings.
- Wine smoke suite (`sf-wine-test`): verified Scenarios 1, 2, 3, 3b (M5 presentation), and 4 (`SF_PROBE`).
