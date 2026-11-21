# 05. Testing strategy

A DLL injected into a closed-source game is hard to test end to end. The strategy is to push as much logic as possible into code that can be tested without Windows or the game, and to make the remaining layers (D3D hooks, engine patches) verifiable with small, repeatable harnesses.

## 1. Test pyramid

| Layer | What it proves | Runs where | Runs in CI | Needs game? |
|---|---|---|---|---|
| **L0 Static** | No obvious bugs, style, UB patterns | Host | Every push | No |
| **L1 Unit (pure C++)** | Hashing, DDS parsing, sigscan matching, config parsing, frame-stat math, limiter math | Host native (Linux) | Every push | No |
| **L2 Fuzz** | Parsers don't crash on bad input (DDS, TOML config, signature patterns, inspector protocol) | Host native | Nightly + short run on PR | No |
| **L3 Shader tests** | HLSL (FSR, particle composite) compiles; golden-image comparison | Host + Wine | Every push (compile), nightly (images) | No |
| **L4 Wine smoke** | DLL loads, `Present` hook installs, frames flow | Wine in container | Every push | No (uses testbed) |
| **L5 Hook integration** | Hooks work against a D3D11 testbed that mimics DS3 patterns (MSAA, flip, resize, multiple contexts) | Wine + software GL, then real GPU | Smoke in CI; full run manual | No |
| **L6 In-game** | Real DS3 behaviour, visuals, performance | Proton, then **real Windows** | Manual, per milestone | **Yes** |

Rule: a bug found at L6 gets a regression test at the lowest layer that can reproduce it.

## 2. L1 unit tests

- **Framework:** [doctest](https://github.com/doctest/doctest) (single header, fast compile, MIT) or Catch2 (richer matchers). Pick doctest; fewer moving parts for cross builds.
- **Built with** the `host-tests` preset (`SF_HOST_ONLY=ON`), so no Win32 headers are included. Enforce this: pure modules must not include `<windows.h>`. A CI grep check fails if `src/core/config.*`, `src/tex/*`, `src/hooks/sigscan.*` or `src/timing/fixed_step.*` include it.
- **Design for testability:**
  - Time sources are injected (`IClock`), so limiter and pacing code can be driven with a fake clock.
  - Memory scanning takes a `std::span<const std::byte>`, so tests can feed synthetic images.
  - D3D11 access goes through thin interfaces only where logic needs to be tested (pass detection reads a `DrawState` struct, not a COM pointer).
- **Must-have suites:**

| Module | Cases |
|---|---|
| `sigscan` | Exact match, wildcards, multiple matches (reports ambiguity), match at image boundaries, empty pattern |
| `config` | Defaults, unknown keys warn, out-of-range values clamp, invalid TOML falls back to defaults, kill-switch keys |
| `content_hash` | Stable across runs, known vectors, empty input, large input |
| `dds` | Valid headers, DX10 header, truncated file, bad mip count, oversized dimensions |
| `frame_stats` | Avg, percentile lows, spike count on known sequences; NaN/zero-division safety |
| `limiter` | With fake clock: hits target period, no drift over 10k frames, handles a long stall without a catch-up burst |
| `offline_guard` | Refuses to enable when online markers are present; requires explicit opt-in |
| `pass_detector` | Given recorded `DrawState` sequences (from `bench/` captures), classifies particle passes correctly |

### M5-M10 pure-module testing

- Presentation and frame pacing: test flip upgrade eligibility, present test/tearing flags, frame-pacer target periods, and fixed-step accumulators in `tests/unit/test_present_policy.cpp`, `tests/unit/test_frame_pacer.cpp`, and `tests/unit/test_fixed_step.cpp`.
- Particles and FSR 1: test render-pass budgets/particle heuristics and EASU constants in `tests/unit/test_particles.cpp` and `tests/unit/test_fsr1_math.cpp`.
- Texture packs: test legacy DXT1/3/5, ATI1/2, BC4/BC5, DX10 BC2/3/4/5/6H/7, sRGB, odd dimensions, mip boundaries, manifest safety, key determinism, and VRAM budget overflow in `tests/unit/test_texture_pack.cpp`.
- Diagnostics: test every `SF_CFG_*` code, severity, preset mismatch, probe/release mismatch, FPS cap conflict, and texture path warning in `tests/unit/test_config_diagnostics.cpp`.
- Coverage gate: `SF_MIN_LINE_COVERAGE=90 bash scripts/ci/run-coverage.sh` is the PR gate for pure modules. The suite currently reports 95%+ line coverage.

- **Coverage:** gcov/lcov or `llvm-cov` on the host build. Target 90% on pure modules; hook glue remains measured in Wine/Windows validation.
- **Sanitizers:** a second host preset `host-asan` with `-fsanitize=address,undefined`. Run the same unit tests under it in CI.

## 3. L2 fuzzing

- [libFuzzer](https://llvm.org/docs/LibFuzzer.html) via clang on the host. Texture-pack targets are `fuzz_texture_dds` and `fuzz_texture_manifest`, alongside config/signature/JSONL fuzzers under `tests/fuzz/`.
- Commit valid and malformed seed corpora under `tests/fuzz/corpus/dds/` and `tests/fuzz/corpus/manifest/`. CI runs each fuzz target for 10 seconds; longer nightly runs remain optional. Crashes upload as artifacts.
- Later option: [OSS-Fuzz](https://github.com/google/oss-fuzz) integration if the project gets traction (free for open source).

## 4. L3 shader tests

- **Compile check:** every `.hlsl` in `shaders/` compiled with `D3DCompile` (via Wine in CI) or with DXC to SPIR-V/DXIL as a cheap syntax lint on Linux (DXC can't produce DXBC for D3D11, per ADR D4, but it catches most errors). Warnings are errors.
- **Golden images:** a testbed mode renders FSR1 (EASU + RCAS) on fixed input images with software rendering (WARP is Windows-only; under Wine use wined3d + llvmpipe). Compare to reference PNGs using a tolerance (max per-channel diff, plus [FLIP](https://github.com/NVlabs/flip) or SSIM mean). GPU/driver differences make exact match unrealistic, so use a tolerance and keep the reference set tiny.
- **Cross-check against the reference:** keep AMD's original FSR1 HLSL unmodified in `third_party/` and compare our port's output to it in the same harness.

## 5. L4/L5 testbed and Wine smoke

- `tests/testbed/d3d11_testbed.cpp` is the target for loading `dinput8.dll`. Modes, selected by CLI flag, each reproduces a DS3-like condition:

| Flag | Condition | What is asserted (via `soulsframe.log` or a JSON result file) |
|---|---|---|
| `--frames 300` | Plain windowed swap chain, `DISCARD` | Hook installed, `avg frame time` logged |
| `--msaa 4` | MSAA back buffer | Module that needs flip model disables itself instead of crashing |
| `--resize` | `ResizeBuffers` mid-run | Hook survives; RTs recreated |
| `--flip` | Game already uses flip model | No double conversion |
| `--deferred` | Uses deferred contexts | Draw tracer counts both contexts |
| `--particles` | Fake blend/depth-state pass with many draws | `pass_detector` finds it; low-res path composites without validation errors |
| `--unknown-exe` | Simulated unknown exe hash | Engine hooks refused, safe layer still active |

- The testbed enables the **D3D11 debug layer** where available (`D3D11_CREATE_DEVICE_DEBUG`) and the test fails on any `ERROR` message. On real hardware this catches state-leak bugs in our injected draws.
- Wine runs in the container from `docker/wine-test.Dockerfile`. A hang guard (`timeout 60`) wraps every run; the job fails on a timeout.
- Smoke result is a machine-readable `smoke-result.json` (not just `grep`) so CI can show a summary.

## 6. L6 in-game checklist (manual, per milestone)

Kept at `docs/checklists/`. Each release candidate must pass:

1. DLL loads, log shows module list with each module's init status.
2. Unknown-exe-hash path refuses engine hooks (rename or hex-edit a copy of the config pin).
3. Menu → load save → map transition → boss fog gate → death → reload, with no crash, 3 times.
4. Alt-tab, resolution change, fullscreen/borderless switch.
5. Each module's **kill switch** works at runtime.
6. Every feature on and off against the benchmark scenes (`docs/04` §1) with a stored capture in `bench/`.
7. Run on real Windows and on Proton. Record GPU, driver, OS in the capture metadata.
8. Run offline only.

## 7. Performance regression testing

- Benchmark captures are JSONL (`bench/<date>_<scene>_<commit>.jsonl`) with a header line (GPU, driver, resolution, settings, exe hash, commit).
- `tools/py/compare_runs.py` takes baseline and candidate sets (≥3 runs each), drops run 1, and reports mean, p1/p0.1 lows, stddev, spike count with a confidence interval (bootstrap). Verdict: improved / neutral / regressed.
- Commit a **synthetic microbenchmark** for the pure code (limiter, hash, pass detector) with [Google Benchmark](https://github.com/google/benchmark) or nanobench and run it nightly; flag >15% regressions. This is the only perf test that runs in CI, since CI machines have no relevant GPU.
- Cross-validate frame times with an external tool ([PresentMon](https://github.com/GameTechDev/PresentMon) on Windows, MangoHud on Linux) so our own log isn't the only witness.

## 8. Definition of done (testing)

A change merges when:

- L0, L1 (normal + ASan/UBSan) and L4 pass in CI.
- New logic has a unit test or a written reason why not.
- Rendering/timing changes attach a before/after capture or screenshot diff.
- A kill switch exists and is covered by a testbed or checklist item.
