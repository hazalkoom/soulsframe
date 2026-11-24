# Step 20 — AI Development Workflow, Observability, and D3D11 Cleanup

## Summary

Added reusable context-gathering and verification scripts (`scripts/dev/context.sh`, `scripts/dev/verify.sh`), a structured `soulsframe.log` diagnostic CLI (`tools/py/inspect_log.py`), elapsed-time + thread-ID prefixes in `sf::log`, and a shared `d3d11_helpers.h` header removing duplicate RTV/COM helpers between `OverlayModule` and `UpscalerModule`.

## Key Improvements

1. **Information Gathering (`scripts/dev/context.sh`, `tools/py/inspect_log.py`):**
   - `scripts/dev/context.sh` outputs git status/worktrees, built artifacts, architecture boundaries, CMake presets, and installed local tools in one command.
   - `tools/py/inspect_log.py` parses `soulsframe.log` (both legacy and `[+elapsed][T:tid]` lines) into human-readable summaries or `--json` with actionable hints (`--self-test` wired into CI).
2. **Focused Verification (`scripts/dev/verify.sh`, `justfile`):**
   - `--quick` runs guards, native `host-tests`, and all 4 Python `--self-test` suites in seconds.
   - `--full` runs `--quick` plus `sf-build` formatting + `win64-release`/`win64-probe` builds + PE audit, `sf-wine-test` smoke suite, and `inspect_log.py`.
3. **Runtime Observability (`src/core/log.cpp`):**
   - Prefixed all `soulsframe.log` entries with `[+%07.3fs][T:%04lx]` (`GetTickCount64()` elapsed seconds and `GetCurrentThreadId()`), making startup ordering and render-thread vs UI-thread diagnostics immediate.
4. **D3D11 Code Hygiene (`src/gfx/d3d11/d3d11_helpers.h`):**
   - Extracted duplicated `safe_release`, `resolve_rtv_format`, and `create_backbuffer_rtv` helpers from `overlay_module.cpp` and `upscaler_module.cpp` into one documented header.

## Verification

- `bash scripts/dev/context.sh`: passed.
- `bash scripts/dev/verify.sh --quick`: passed (`check-guards.sh`, `2/2` `host-tests`, and all 4 Python `--self-test` suites).
- `sf-build` (`clang-format --dry-run --Werror`, `win64-release`, `win64-probe`) and `sf-wine-test` (`scripts/smoke-test.sh`): `smoke OK (probe=true)`.
- `python3 tools/py/inspect_log.py build/win64-release/soulsframe-overlay.log`: parsed `v1.0.0`, `present=True`, `wndproc=True`, `Resizes (1)`.
