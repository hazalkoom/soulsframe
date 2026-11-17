# Step 06: Performance Micro-Benchmarks Suite (`host-bench`)

**Branch:** `feat/perf-benchmarks`  
**Scope:** Native Linux Release (`-O3`) micro-benchmark harness covering all pure-C++ SoulsFrame hot paths, plus CMake preset, `justfile` shortcut, and CI summary/artifact reporting.

## 1. What was added

- **Build configuration:**
  - Added `SF_BUILD_BENCH` option to [CMakeLists.txt](../CMakeLists.txt) and the `host-bench` configure/build/test preset (`CMAKE_BUILD_TYPE=Release`, `SF_HOST_ONLY=ON`, `SF_BUILD_BENCH=ON`) to [CMakePresets.json](../CMakePresets.json).
  - Added `just bench` shortcut in [justfile](../justfile).
- **Zero-dependency micro-benchmark harness:**
  - [tests/bench/bench_harness.h](../tests/bench/bench_harness.h) and [tests/bench/main.cpp](../tests/bench/main.cpp) provide compiler optimization barriers (`do_not_optimize`, `clobber_memory`), warmup + multi-sample median/min/p95 statistics, `--quick` smoke mode for CTest (`soulsframe_benchmarks_smoke`), `--filter`, and `--json` export.
- **Hot-path benchmarks (12 cases):**
  - [tests/bench/bench_frame_and_probe.cpp](../tests/bench/bench_frame_and_probe.cpp): `FrameStats::record_frame_ms`, `FrameStats::compute_metrics`, `TraceRecorder::record_draw`, `TraceRecorder::frame_1k_draws` (realistic 1,000-draw, 8-pass, 40-site DS3 frame), `TraceRecorder::resource_hit`, `probe::json_escape`, and `probe::serialize_snapshot(40)`.
  - [tests/bench/bench_hashing_and_scan.cpp](../tests/bench/bench_hashing_and_scan.cpp): `tex::hash_bytes` (64 KB XXH64), `core::sha256_bytes` (64 KB), `hooks::sigscan` (4 MB wildcard image scan), `hooks::parse_pattern`, and `core::parse_config_toml`.
- **CI integration:**
  - Added the `benchmarks` job to [.github/workflows/ci.yml](../.github/workflows/ci.yml) to execute `soulsframe_benchmarks --json build/host-bench/bench-results.json`, publish a formatted Markdown results table to `$GITHUB_STEP_SUMMARY`, and upload `bench-results.json` as an artifact.

## 2. Measured local baseline (`host-bench` Release `-O3`)

| Benchmark | Category | Median (ns) | Throughput |
|---|---|---:|---:|
| `FrameStats::record_frame_ms` | `timing` | 4.31 | 231.86 Mops/s |
| `FrameStats::compute_metrics` | `timing` | 9944.30 | 100.6 Kops/s |
| `TraceRecorder::record_draw` | `probe` | 22.64 | 44.17 Mops/s |
| `TraceRecorder::frame_1k_draws` | `probe` | 36346.30 | 27.5 Kops/s |
| `TraceRecorder::resource_hit` | `probe` | 11.70 | 85.49 Mops/s |
| `probe::json_escape (83B)` | `jsonl` | 213.90 | 370.1 MB/s |
| `probe::serialize_snapshot(40)` | `jsonl` | 21562.50 | 46.4 Kops/s |
| `tex::hash_bytes (64KB XXH64)` | `hash` | 3959.19 | 15786.1 MB/s |
| `core::sha256_bytes (64KB)` | `hash` | 227853.24 | 274.3 MB/s |
| `hooks::sigscan (4MB pattern)` | `sigscan` | 4133265.54 | 967.8 MB/s |
| `hooks::parse_pattern` | `sigscan` | 271.45 | 3.68 Mops/s |
| `core::parse_config_toml` | `config` | 9183.59 | 108.9 Kops/s |

## 3. Verification

- Verified `bash scripts/ci/check-guards.sh` passed.
- Verified `clang-format` compliance on all files in `tests/bench/` using the `sf-build` container.
- Verified `cmake --preset host-bench -DSF_WERROR=ON`, `cmake --build --preset host-bench`, and `ctest --preset host-bench --output-on-failure` (`2/2` tests passed: `soulsframe_unit_tests` and `soulsframe_benchmarks_smoke`).
