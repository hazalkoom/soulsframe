# Step 5 Report: Add `tests/unit/test_security_inputs.cpp`

Date: 2026-11-17
Branch: `feat/oc-tasks`

## 1. What Was Built

Created [`tests/unit/test_security_inputs.cpp`](../tests/unit/test_security_inputs.cpp), a new doctest unit suite covering adversarial and security-critical input handling across three pure-C++ modules. Registered it in [`tests/unit/CMakeLists.txt`](../tests/unit/CMakeLists.txt).

### Coverage areas

| Area | What is tested |
|------|----------------|
| **Malformed TOML** | Unterminated strings, bad table headers (`[%...]`), truncated inline tables, dangling commas, reserved bare keys, empty/whitespace/comment-only docs, wrong types for every scalar key, duplicate keys |
| **Huge strings / large inputs** | 5,000 unknown keys, 100k-char string values, 50k-char section/key names, 1MB input to `json_escape` |
| **Path traversals** | `output_file = "../../etc/passwd"`, `..\\..\\` Windows traversal, `foo/../bar`, empty string, non-string type — all rejected via warning + safe default fallback |
| **Corrupt / adversarial JSONL** | `json_escape` with embedded NUL bytes, all 32 control characters (0x00–0x1F), extreme `UINT64_MAX`/`UINT32_MAX`/infinity/NaN values in all formatters, empty and full-capacity `FrameTraceSnapshot` serialization |
| **Offline guard spoofing** | `--Online` hidden among benign args, `--online` exact match blocked, `force_online_detected` overriding opt-in, missing opt-in, exact-match limitation documented (`--online=1` not caught), positive control (clean args allowed) |
| **Config file loading** | Missing path, empty path — both yield defaults + warning, no throw |

### Design notes

- Complements (does not duplicate) `tests/unit/test_config.cpp` and `tests/unit/test_offline_guard.cpp`, which already cover the core parse/validate/guard paths.
- `CHECK` assertions use boolean variables for compound conditions (doctest forbids `||`/`&&` inside `CHECK`).
- No raw C arrays; uses `std::string`, `std::string_view`, `std::vector`.
- No `<windows.h>`, `<d3d11.h>`, or `<dxgi.h>` — pure Linux-testable headers only.

## 2. Local Verification Results

- `bash scripts/ci/check-guards.sh`: Passed (`All repository guards passed`).
- `cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests`: Passed (`38` test cases, `790` assertions, 0 failures).
- All existing test suites continue to pass (`test_config`, `test_offline_guard`, `test_jsonl_writer`, `test_frame_stats`, etc.).

## 3. Next Priority-Queue Items (not started yet)

1. `tests/unit/test_perf_benchmarks.cpp` — frame stats ticks and JSONL serialization micro-benchmarks (< 50 ns/tick target).
2. Expand `tools/py/` telemetry utilities (plotting frametimes, CSV export, run comparison).
