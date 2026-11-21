# Step 18: M9-M10 QA, Fuzzers, CI, Coverage, Benchmarks, and Docs

Date: 2026-11-21
Branch: `feat/oc-tasks`

## Milestone sync

Synced the sibling worktree from `origin/main` FF-only to `5ea4e99`, the v0.10.0-rc M9/M10 state.

## Changes by task

### M9 texture-pack fuzzers

Added `tests/fuzz/fuzz_texture_dds.cpp` and `tests/fuzz/fuzz_texture_manifest.cpp` with seed corpora for valid/malformed DDS and manifest inputs. The `fuzz-smoke` CI job builds and runs both targets for 10 seconds each.

### Bake-tool self-tests

Added stdlib `unittest` coverage to `tools/py/bake_textures.py` behind `--self-test`. CI runs this suite from `python-tools` alongside the existing Python telemetry self-tests.

### Static-analysis catch-up

Added M5-M10 pure modules to both clang-tidy and cppcheck lists in `ci.yml`:

- `src/core/config_diagnostics.cpp`
- `src/games/ds3/render.cpp`
- `src/gfx/fx/fsr1_math.cpp`
- `src/tex/texture_pack.cpp`
- `src/timing/fixed_step.cpp`
- `src/timing/frame_pacer.cpp`
- `src/timing/present_policy.cpp`

### Unit edge cases and coverage

Expanded texture-pack and config-diagnostics tests. The local coverage run passed the 90% gate at:

- **Lines:** 95.0% (1676/1765)
- **Functions:** 97.6% (161/165)
- **Branches:** 82.9% (1444/1742)

### M5-M9 benchmarks

Added `bench_m5_m9_pure.cpp` benchmarks for:

- `tex::compute_texture_key`
- `tex::parse_dds`
- `timing::evaluate_flip_upgrade`
- `gfx::compute_easu_constants`
- `FixedStepStepper::advance`

## Verification commands

- `bash scripts/ci/check-guards.sh`: passed.
- `cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests --output-on-failure`: passed.
- `SF_MIN_LINE_COVERAGE=90 bash scripts/ci/run-coverage.sh`: passed at 95.0% lines.
- `python3 tools/py/bake_textures.py --self-test`: 5 unittest methods passed.
- `python3 tools/py/analyze_capture.py --self-test`: passed.
- `python3 tools/py/plot_gpu_passes.py --self-test`: passed.
- `cmake --preset host-bench -DSF_WERROR=ON && cmake --build --preset host-bench`: passed.
- Markdownlint: 26 files checked, 0 issues.

## Safety

No changes were made to `src/gfx/d3d11/`, `src/core/dllmain.cpp`, or `offline_guard.cpp`. Offline acknowledgement remains false in all templates and presets.
