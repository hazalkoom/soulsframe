# Step 07: Milestone 3 - GPU pass timing (D3D11 timestamp queries)

**Branch:** `feat/m3-gpu-timing`
**Goal:** Know how many *GPU milliseconds* each render pass costs (shadows, particles, post-process...), not just CPU frame time, so later milestones (M6 low-res particles, M7 FSR) can be proven with real before/after numbers.

## 1. What a vibe coder needs to know

The GPU runs behind the CPU. To time a pass we drop a "stopwatch tick" into the GPU's command stream at the start of every pass and one at end of frame. A frame's ticks are only readable 1-3 frames later, so we **never wait** for them: we keep each frame's capture in a small waiting room, attach `gpu_ms` when the answer arrives, then write the line. If a driver can't do this, the mod simply omits `gpu_ms` (no crash, no stutter).

## 2. Design (pure vs Windows split kept intact)

| Layer | File | Role | Tested on |
|---|---|---|---|
| Pure | [src/timing/gpu_timing.h](../src/timing/gpu_timing.h) | ticks -> ms, N+1 stamps -> N pass segments, query slot ring | Linux unit tests |
| Pure | [src/probe/gpu_join.h](../src/probe/gpu_join.h) | waiting room: ordering, 8-frame timeout, pass-count mismatch safety | Linux unit tests |
| Pure | [src/probe/jsonl_writer.cpp](../src/probe/jsonl_writer.cpp) | optional trailing `"gpu_ms"` (old captures stay byte-identical) | Linux unit tests |
| Pure | [src/core/config.h](../src/core/config.h) | `probe.gpu_timing` kill switch (default on) | Linux unit tests |
| D3D11 | [src/gfx/d3d11/gpu_timer.cpp](../src/gfx/d3d11/gpu_timer.cpp) | real `TIMESTAMP` / `TIMESTAMP_DISJOINT` queries, non-blocking polling | Wine smoke |
| D3D11 | [src/gfx/d3d11/context_hook.cpp](../src/gfx/d3d11/context_hook.cpp) | stamps *before* the first call of each new pass | Wine smoke |

## 3. Commits

1. `feat(timing)`: pure GPU timestamp math + slot ring (+5 test cases)
2. `feat(probe)`: optional `gpu_ms` JSONL fields (+1)
3. `feat(config)`: `probe.gpu_timing` kill switch (+1)
4. `feat(probe)`: `GpuFrameJoiner` (+5)
5. `feat(gfx)`: `GpuTimer` + probe wiring + `TraceRecorder::pass_count()` (+1)
6. `feat(tools)`: analyzer reports GPU frame stats and per-pass GPU totals
7. `docs`: this report, codebase reference, roadmap

## 4. Verification

- Host: `51` test cases / `870` assertions pass (`SF_WERROR=ON`); guards green.
- `win64-probe` and `win64-release` cross-compile with `-Werror`.
- Wine smoke (`smoke OK (probe=true)`): log shows `GpuTimer: timestamp queries ready`, and real captures contain `gpu_ms` on frames and `gpu_pass` lines (software renderer, ~10 of 20 frames resolved).
- `python3 tools/py/analyze_capture.py <capture>` now prints `gpu_frames` and per-pass `gpu_ms_total`.

## 5. Known limits (honest list)

- The last <=8 frames of a capture are written without `gpu_ms` (their results had not arrived when capture stopped). Irrelevant for 120+ frame captures.
- Software renderers (Wine/llvmpipe) resolve fewer frames than real GPUs; real-hardware numbers need a DS3 session.
- Per-pass segments are only attached when GPU stamps and recorder passes match 1:1; otherwise only the frame total is kept (never mislabeled).
- Stamping runs on the immediate context from the render thread only; deferred contexts are not timed.
- Not yet validated on real DS3 / real GPU hardware.
