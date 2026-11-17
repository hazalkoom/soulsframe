# Step 4 Report: Add `docs/13-vibe-coder-cheatsheet.md`

Date: 2026-11-16
Branch: `feat/oc-tasks`

## 1. What Was Built

Created [`docs/13-vibe-coder-cheatsheet.md`](../docs/13-vibe-coder-cheatsheet.md), a one-page plain-English quick reference for collaborators who are not C++/DirectX systems programmers. It covers:

- What SoulsFrame is (fake `dinput8.dll` between the game and Windows/controller input)
- The two-world split ("Pure Brain" pure-C++ core vs. "Windows Hands" D3D11 hooks) and where each lives
- The day-to-day commands (guard check, host tests, coverage gate, Windows cross-compile, Wine smoke test)
- A plain-English `soulsframe.toml` settings reference with safety-critical defaults (`offline_acknowledged = false`, per-module kill switches)
- Safety rules (offline-only, no game files in git, one feature at a time, measure first)
- How to prompt AI agents and where to look when something breaks
- A pointer table to the deeper docs

Also updated [`README.md`](../README.md) doc index to link the new cheatsheet next to the existing vibe-coder guide.

## 2. Local Verification Results

- `bash scripts/ci/check-guards.sh`: Passed (`All repository guards passed`).
- `cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests`: Passed (`1/1` tests, `soulsframe_unit_tests OK`, 0.08 s).
- No C++ source or headers changed, so coverage, fuzz targets, and format checks are unaffected.

## 3. Next Priority-Queue Items (not started yet)

1. `tests/unit/test_security_inputs.cpp` — malformed TOML, huge strings, path traversals, corrupt JSONL, offline guard spoofing.
2. `tests/unit/test_perf_benchmarks.cpp` — frame stats ticks and JSONL serialization micro-benchmarks (< 50 ns/tick target).
3. Expand `tools/py/` telemetry utilities (plotting frametimes, CSV export, run comparison).
