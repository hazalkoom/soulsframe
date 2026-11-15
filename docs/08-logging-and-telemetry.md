# 08. Logging and telemetry extraction plan

Performance work without trustworthy numbers is guesswork. This document defines how SoulsFrame writes human-readable diagnostics, exports machine-readable performance telemetry, and extracts engine insights without harming frame times or leaking user data.

## 1. Two separate output streams

| Stream | File | Audience | Compiled in | Overhead budget |
|---|---|---|---|---|
| **Runtime log** | `soulsframe.log` (next to `dinput8.dll`) | Users, bug reports, CI smoke tests | Release + Probe | Minimal (rare lifecycle events + 1 summary line every 120 frames) |
| **Telemetry stream** | `bench/<scene>_<ts>.jsonl` (or remote inspector stream) | `tools/py/`, `sftool`, CapFrameX-style graphs | Probe + opt-in Release capture | Buffered ring + background writer thread so `Present()` never blocks on disk I/O |

Never mix raw per-frame telemetry dumps into `soulsframe.log`. Keep `soulsframe.log` concise enough to paste into a GitHub issue.

## 2. Runtime log design (`soulsframe.log`)

### 2.1 Format
Planned evolution from the Step 1 prototype (`[ms_since_start] [level] [module] message`):
```text
[   12.410] [info ] [core     ] SoulsFrame v0.1.0 starting (release build)
[   12.890] [info ] [guard    ] Offline opt-in verified
[   13.104] [warn ] [core     ] Detected overlay module: GameOverlayRenderer64.dll
[   14.220] [info ] [ds3      ] exe sha256=3a7f... engine_hooks_allowed=false
[   15.018] [info ] [d3d11    ] Present hook installed at 0x00007FF...
[ 2015.442] [info ] [telemetry] avg=16.652 ms (60.1 fps) | p1_low=17.410 ms | spikes=0
```

### 2.2 Filtering, rotation, and flushing
- **Levels:** `debug`, `info`, `warn`, `error` configured via `general.log_level` in `soulsframe.toml`.
- **Size cap / rotation:** truncate or rotate (`soulsframe.prev.log`) on startup if the previous log exceeds 5 MB so long play sessions cannot fill a disk.
- **Flush policy:** flush immediately on startup, config warnings, hook errors, and shutdown; avoid flushing every line if `debug` level is enabled inside draw-call hooks.
- **Privacy rule:** never write Windows usernames, local Steam account IDs, or full `C:\Users\<name>\...` paths into logs; replace user profile prefixes with `<HOME>`.

## 3. Machine-readable telemetry (`JSONL`)

JSON Lines (`*.jsonl`, one self-contained JSON object per line) lets Python (`pandas`), `jq`, and our host analysis scripts stream captures of any size without loading everything into memory.

### 3.1 Event types

1. **`session_header`** (line 1 of every capture):
```json
{
  "type": "session_header",
  "schema": 1,
  "mod_version": "0.1.0",
  "git_commit": "bdcbb88",
  "game": "ds3",
  "exe_sha256": "...",
  "os": "Windows 11 / Proton 9.0",
  "gpu": "AMD Radeon RX ...",
  "driver": "...",
  "resolution": [1920, 1080],
  "present_mode": "game",
  "active_modules": ["frame_telemetry"]
}
```

2. **`frame`** (emitted per presented frame when capture is active):
```json
{
  "type": "frame",
  "idx": 1200,
  "cpu_frame_ms": 16.64,
  "present_wait_ms": 1.12,
  "gpu_total_ms": 11.40,
  "draw_calls": 1842,
  "triangles": 2410900
}
```

3. **`gpu_pass`** (M3+, grouped by render-target set / D3D11 pass heuristics):
```json
{
  "type": "gpu_pass",
  "frame_idx": 1200,
  "pass_id": 4,
  "tag": "particles_alpha",
  "draws": 312,
  "gpu_ms": 4.85,
  "rt_width": 1920,
  "rt_height": 1080
}
```

4. **`draw_site`** (M2+ `SF_PROBE` builds only, for API-anchored RE):
```json
{
  "type": "draw_site",
  "rva": "0x1A2B3C",
  "api": "DrawIndexed",
  "count": 340,
  "blend_enabled": true,
  "depth_write": false,
  "ps_hash": "8f31c09e41a2b001"
}
```

## 4. Extracting info cleanly (host + probe pipeline)

```
  In-game DLL (SF_PROBE)                 Host tools
  ──────────────────────                 ──────────
  Present / DeviceContext hooks          tools/py/analyze_capture.py
        │                                  ├─ averages, 1% / 0.1% lows
        ▼                                  ├─ per-pass GPU breakdown
  Lock-free SPSC ring buffer               └─ A/B bootstrap comparison
        │
        ├──► Async writer thread ──► bench/*.jsonl
        └──► HTTP / WebSocket    ──► Remote Web Inspector (browser on Linux/Windows)
```

### Rules for low-overhead extraction
1. **Never allocate or do disk I/O inside D3D11 hooks.** Push compact fixed-size records into a single-producer/single-consumer ring buffer and let a background thread format JSONL on the side.
2. **Aggregate in-memory for `draw_site` records.** Grouping draw calls per caller RVA per 120-frame window cuts output volume by >100x while preserving the exact hotspots needed in Ghidra.
3. **Separate Release vs Probe.** Heavy introspection (call-stack capture via `RtlCaptureStackBackTrace`, HTTP inspector server, PE memory dumping) compiles only under `SF_PROBE=ON` (`docs/01` §3).
4. **Cross-check external tools.** Verify our `cpu_frame_ms` numbers against `PresentMon` (Windows) and `MangoHud` (Linux) whenever changing presentation or limiter behavior (`docs/05` §7).

## 5. Planned implementation order

1. **Step 1 (done):** Synchronous thread-safe `soulsframe.log` + rolling 120-frame summary (`avg_ms`, `avg_fps`, `p1_low_ms`, `spike_count`).
2. **M2:** Async log/JSONL ring buffer + `ID3D11DeviceContext` draw-call counter + caller-RVA aggregator (`SF_PROBE`) + `tools/py/plot_frametimes.py`.
3. **M3:** D3D11 timestamp queries per render pass (`gpu_timer`) + `session_header` metadata capture + A/B comparison script (`tools/py/compare_runs.py`).
4. **M4+:** Remote inspector HTTP endpoint (`cpp-httplib`) serving live JSON snapshots and accepting runtime kill-switch toggles.
