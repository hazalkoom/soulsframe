# SoulsFrame vibe-coder cheatsheet (plain-English quick reference)

A one-page reminder of how SoulsFrame is put together, how to test it, and how to stay safe.
If something here conflicts with a deeper doc, the deeper doc wins. Written for collaborators
who are not C++/DirectX systems programmers — no engine jargon required.

## 1. What SoulsFrame is (in one paragraph)

SoulsFrame is a fake `dinput8.dll` that sits next to `DarkSoulsIII.exe`. Windows loads our DLL
instead of the real controller library, so we see frames as they are drawn and can measure timing,
skip wasted graphics work, and log what happens — while still passing controller input through to
the game normally.

It is split into two worlds so most of it can be tested on Linux without the game:

| World | What it does | Where it lives | Tested on |
|-------|--------------|----------------|-----------|
| **Pure Brain** | Reads settings, checks the offline safety lock, computes FPS/stutters, hashes files | `src/core/`, `src/timing/`, `src/probe/`, `src/hooks/sigscan.*` | Linux (fast unit tests) |
| **Windows Hands** | Talks to Windows, hooks DirectX frame drawing | `src/gfx/d3d11/` (only in the DLL build) | Wine / Windows |

## 2. Commands you will actually use

Run these from the repo root on Fedora Linux:

```bash
# Guard check (run before every commit)
bash scripts/ci/check-guards.sh

# Build + run all fast unit tests on Linux
cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests

# Coverage gate (must stay at or above the configured minimum)
SF_MIN_LINE_COVERAGE=90 bash scripts/ci/run-coverage.sh

# Cross-compile the Windows DLL (inside the build container)
podman run --rm --userns=keep-id -v "$PWD:/src:Z" -v sf-ccache:/ccache -w /src sf-build \
  bash -c "cmake --preset win64-release && cmake --build --preset win64-release"
# -> build/win64-release/dinput8.dll

# 4-scenario Wine smoke test
just smoke
```

## 3. Settings (`soulsframe.toml`) — plain English

The config file lives next to `dinput8.dll`. Every feature has its own on/off switch so one broken
feature cannot take down the game.

```toml
[general]
# YOU MUST SET THIS TO true BEFORE ANY HOOKS RUN. Defaults to false (hooks blocked).
offline_acknowledged = false
# Master kill switch for the whole mod.
mod_enabled = true
# Log verbosity: debug, info, warn, error
log_level = "info"

[present]
# "game" = leave DS3 alone; "flip" = planned presentation experiment (M5)
mode = "game"
# Frame-rate cap in FPS. 0 = disabled. Clamped to 0-360.
max_fps = 0

[modules]
# Per-feature kill switches. Flip any of these to false to disable just that feature.
frame_telemetry = true
presentation = false
probe_tracer = false

[probe]
# Only used in SF_PROBE tracer builds.
capture_frames = 120
output_file = "soulsframe_capture.jsonl"
trace_resources = true
```

Safety-critical defaults:
- `offline_acknowledged = false` means **no hooks are installed**. You must flip it to `true`
  yourself, and only when you are playing offline.
- `mod_enabled = false` turns the entire mod off without deleting anything.

## 4. Safety rules (never break these)

1. **Offline only.** Never set `offline_acknowledged = true` while connected to an online session.
   The guard also refuses hooks if it sees `--online` flags or online session markers.
2. **Never commit game files.** No `.exe`, `.dll`, `.bdt`, `.bhd`, `.dcx`, `.tpf`, `.fxr`, `.bnd`,
   `.zip`, or anything over 5 MB goes into git. Game dumps go only under `re/dumps/` (gitignored).
3. **One feature at a time.** Every feature is a `Module` with a kill switch; if it fails to start,
   the mod logs it and keeps the game running.
4. **Measure first.** No optimization lands without a before/after frame-time capture in `bench/`.

## 5. How to add or change something

Ask your AI coding agent with prompts like these:

- "Add a `[modules]` setting for feature X in `src/core/config.h`, register it as a Module, and write
  a doctest unit test for it."
- "Run `bash scripts/ci/check-guards.sh` and `cmake --preset host-tests && ... && ctest` to verify."
- "Write a concise plain-English step report under `reports/` explaining what changed."

Always run the guard script and the host tests before committing. Format C++ with `clang-format -i`.

## 6. Where to look when something breaks

| Symptom | First place to check |
|---------|----------------------|
| Mod does nothing in-game | `soulsframe.log` next to `DarkSoulsIII.exe`; look for "Present hook installed" |
| Hooks blocked / guard warning | `soulsframe.toml`: `general.offline_acknowledged` must be `true` |
| Unit test fails on Linux | `tests/unit/` — run `ctest --preset host-tests` to see which |
| JSONL capture looks wrong | `src/probe/jsonl_writer.cpp` and `tests/unit/test_jsonl_writer.cpp` |
| Frame-time math off | `src/timing/frame_stats.cpp` and `tests/unit/test_frame_stats.cpp` |
| Config rejected | `src/core/config.cpp` and `tests/unit/test_config.cpp` |

## 7. Related docs (deeper reading)

- `docs/guide-for-vibe-coders.md` — full plain-English tour (start here if you are new)
- `docs/10-deep-dive-for-learners-and-vibe-coders.md` — zero-to-systems masterclass
- `docs/01-architecture-and-roadmap.md` — milestones M0–M10 and where the project is headed
- `docs/05-testing-strategy.md` — how unit, fuzz, Wine, and perf tests fit together
- `docs/08-logging-and-telemetry.md` — JSONL capture schema and log format
- `SECURITY.md` — offline safety threat model and PE binary mitigations
- `AGENTS.md` — rules for AI coding agents working in this repo
