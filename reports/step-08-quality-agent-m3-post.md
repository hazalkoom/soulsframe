# Step 08: Quality & Docs Agent — M3 Post-Land Completion (Assignments 1–4)

Date: 2026-11-17
Branch: `feat/oc-tasks`

## Context

Synced with `origin/main` (fast-forward) onto `feat/oc-tasks`, picking up the
Milestone 3 work: D3D11 GPU pass timing queries, `GpuFrameJoiner`, Release
benchmarks, `probe.gpu_timing` kill switch, and per-pass `gpu_ms` JSONL fields.

## Assignments completed (in order)

### 1. Python utilities — `feat(tools)`: add `tools/py/plot_gpu_passes.py`
- Reads a SoulsFrame JSONL capture, parses `gpu_pass` records.
- Aggregates total/average GPU ms per pass tag; emits Markdown/CSV tables;
  optional matplotlib bar chart via `--chart`.
- Includes `--self-test` (verified passing).

### 2. Bash automation — `feat(scripts)`: add `scripts/quick-bench.sh`
- Runs `host-bench` preset, builds benchmark harness, runs it to
  `build/host-bench/bench-results.json`, prints friendly colored summary
  (fastest/slowest with median/p95/ops-s, top-5 tables).
- End-to-end verified successfully (12 benchmarks, all passed).

### 3. Human docs — `docs`: add GPU vs CPU timing to `docs/13-vibe-coder-cheatsheet.md`
- Added "Understanding GPU vs CPU frame timing (`gpu_ms` vs `cpu_frame_ms`)"
  section explaining why a GPU pass taking 4ms differs from CPU stutter.
- Documented `[probe] gpu_timing = true` kill switch.
- Added GPU timing troubleshooting row; added `plot_gpu_passes.py` usage.

### 4. Edge-case tests — `test(timing)`: extend `tests/unit/test_gpu_timing.cpp`
- Added extreme `gpu_ticks_to_ms` tests: 10 GHz frequencies, huge tick spans
  (0→UINT64_MAX staying finite/positive), near-max spans.
- Added `GpuSlotRing` rapid stress wrap-around test (10,000 iterations with
  FIFO bookkeeping verification).

## Verification results (final state)

- `bash scripts/ci/check-guards.sh` → Passed (`All repository guards passed`).
- `cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests` → Passed.
- Full suite: `53` test cases / `110,878` assertions / `0 failed`.
- `python3 tools/py/plot_gpu_passes.py --self-test` → Passed.
- `bash scripts/quick-bench.sh` → Passed (12 benchmarks, colored summary).
- Strict typing: no `<windows.h>`/`<d3d11.h>`/`<dxgi.h>`; no raw C arrays; pure
  Linux C++20 / Python 3 stdlib-only code.

## Commits made this session (all pushed to `feat/oc-tasks`)

1. `feat(tools): add plot_gpu_passes.py for GPU pass timing summaries`
2. `feat(scripts): add quick-bench.sh for Release benchmark harness`
3. `docs: add GPU vs CPU timing section to vibe-coder cheatsheet`
4. `test(timing): add extreme GPU timing and slot ring stress tests`
