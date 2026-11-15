# Step 3 Report: Milestone 2 (`sf-probe` v1), Coverage Gate, and `libFuzzer` CI

Date: 2026-11-15
Branch: `feat/m2-probe-tracer`

## 1. What Was Built

### A. Code Coverage & Continuous Fuzzing Infrastructure

- Added `SF_COVERAGE` CMake option in [CMakeLists.txt](../CMakeLists.txt), `host-cov` presets in [CMakePresets.json](../CMakePresets.json), `test-cov` recipe in [justfile](../justfile), and [scripts/ci/run-coverage.sh](../scripts/ci/run-coverage.sh) running `gcovr` with a 90% line-coverage gate (`SF_MIN_LINE_COVERAGE=90`).
- Installed host coverage and Python analysis tools locally (`gcovr 8.8`, `pandas 3.1.1`, `matplotlib 3.11.2`).
- Added three Clang `libFuzzer` + ASan/UBSan harnesses and seed corpora under [tests/fuzz/](../tests/fuzz/): `fuzz_config`, `fuzz_sigscan`, and `fuzz_probe_jsonl`.
- Added `coverage` and `fuzz-smoke` jobs to [.github/workflows/ci.yml](../.github/workflows/ci.yml).

### B. Pure-C++ Probe Core (`soulsframe_probe_core`)

- Implemented [src/probe/trace_types.h](../src/probe/trace_types.h) and [src/probe/trace_types.cpp](../src/probe/trace_types.cpp): `DrawApi`, `PassTag`, `DrawCallState`, `CallSiteStats`, `PassSummary`, `ResourceSummary`, `FrameTraceSnapshot`, `classify_pass()`, and bounded `TraceRecorder`.
- Implemented [src/probe/jsonl_writer.h](../src/probe/jsonl_writer.h) and [src/probe/jsonl_writer.cpp](../src/probe/jsonl_writer.cpp): `SessionHeaderInfo`, `json_escape()`, `format_session_header()`, `format_frame_event()`, `format_pass_event()`, `format_draw_site_event()`, `format_resource_event()`, and `serialize_frame_snapshot()`.
- Added unit suites [tests/unit/test_probe_trace.cpp](../tests/unit/test_probe_trace.cpp) and [tests/unit/test_jsonl_writer.cpp](../tests/unit/test_jsonl_writer.cpp), and extended [src/core/config.h](../src/core/config.h) / [src/core/config.cpp](../src/core/config.cpp) / [config/soulsframe.toml.example](../config/soulsframe.toml.example) with `modules.probe_tracer = false` and `[probe]` (`capture_frames`, `output_file`, `trace_resources`), keeping `offline_acknowledged = false` untouched.
- Extended [tools/py/analyze_capture.py](../tools/py/analyze_capture.py) (`--self-test`) to aggregate total draw calls, `pass_breakdown`, `top_draw_sites`, and discovered GPU resources alongside frame timing percentiles.

### C. D3D11 Immediate Context Hooks, Async `ProbeModule`, and 4-Scenario Wine Verification

- Implemented [src/gfx/d3d11/context_hook.h](../src/gfx/d3d11/context_hook.h) and [src/gfx/d3d11/context_hook.cpp](../src/gfx/d3d11/context_hook.cpp) (compiled only under `SF_PROBE=1`): resolves `ID3D11DeviceContext` vtable slots `12` (`DrawIndexed`), `13` (`Draw`), `20` (`DrawIndexedInstanced`), `21` (`DrawInstanced`), `41` (`Dispatch`), `50` (`ClearRenderTargetView`), and `53` (`ClearDepthStencilView`), installs per-target MinHook detours cleanly even when `swapchain_hook` already initialized MinHook, and captures RTV/DSV/blend/depth/shader/RVA state with COM reference release and re-entrancy guards.
- Implemented [src/probe/probe_module.h](../src/probe/probe_module.h) and [src/probe/probe_module.cpp](../src/probe/probe_module.cpp): implements `sf::Module`, moves JSONL file flushing onto a dedicated background `writer_loop` thread, stops capture automatically at `cfg.probe.capture_frames`, joins cleanly inside `Present()` once capture finishes, and avoids Windows `DLL_PROCESS_DETACH` loader-lock deadlocks on early exit by flushing via `writer_done_` and detaching.
- Extended [tests/testbed/d3d11_testbed.cpp](../tests/testbed/d3d11_testbed.cpp) with `--particles` (compiles minimal `vs_4_0`/`ps_4_0` shaders via `d3dcompiler_47.dll`, binds vertex buffer + input layout, and renders both opaque `Draw(3, 0)` and alpha-blended no-depth-write `DrawInstanced(6, 8, 0, 0)` calls).
- Extended [scripts/smoke-test.sh](../scripts/smoke-test.sh) with Scenario 4 (`SF_PROBE` 20-frame capture of `--particles`) and updated [AGENTS.md](../AGENTS.md) and docs (`01`, `08`, `09`, `10`).

## 2. Local Verification Results

- `bash scripts/ci/check-guards.sh`: Passed (`All repository guards passed`).
- `python3 tools/py/analyze_capture.py --self-test`: Passed (`analyze_capture.py self-test OK`).
- `./build/host-cov/tests/unit/soulsframe_unit_tests`: Passed (`29/29` test cases, `673/673` assertions).
- `SF_MIN_LINE_COVERAGE=90 bash scripts/ci/run-coverage.sh`: Passed (`lines: 96.3% (632 out of 656)`, `functions: 95.3% (61 out of 64)`, `branches: 84.2% (491 out of 583)`).
- `clang-format --dry-run --Werror` across all `src/` and `tests/` files (`sf-build`): Passed.
- `win64-release` and `win64-probe` cross-compiles (`-DSF_WERROR=ON`) + `x86_64-w64-mingw32-objdump -p`: Both DLLs export `DirectInput8Create`, enforce `HIGH_ENTROPY_VA`, `DYNAMIC_BASE`, and `NX_COMPAT`, and have zero dynamic runtime (`libstdc++`, `libgcc_s`, `libwinpthread`, `minhook`) imports.
- 4-scenario Wine smoke suite (`sf-wine-test` + `scripts/smoke-test.sh`): Passed (`smoke OK (probe=true)`), producing 82-line `soulsframe_capture.jsonl` across 20 captured frames (`40` draw calls, `20` `particles_alpha` pass events, `DrawInstanced` + `Draw` call sites) verified by `analyze_capture.py`.
