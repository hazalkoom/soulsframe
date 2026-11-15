# SoulsFrame: Dark Souls III performance and frame-pacing mod

> Status: **planning, v2.** Scope is **DS3 only**. Elden Ring comes later, as a separate renderer backend (see `docs/01`).
> Dev host: Fedora Linux. Output: a Windows x64 `dinput8.dll` proxy mod, cross-compiled with MinGW-w64.

## What this is

A native C++ DLL that loads into `DarkSoulsIII.exe`, hooks the D3D11 pipeline and selected engine functions, and tries to:

1. **Measure** where frame time goes (GPU pass timing, CPU hot spots).
2. **Fix frame pacing and presentation**.
3. **Cut GPU cost** in the worst case (transparent particles / fog overdraw).
4. **Upscale** from a lower internal resolution (FSR 1 first).
5. **Unlock FPS** safely, which is the hardest and highest-risk item.

It also ships a **reverse-engineering toolkit** (`sf-probe` inside the DLL, `sftool` on the host, Ghidra in a container) so that every optimization is based on measurement instead of guesses.

## Changes from the first plan

| First plan | Now | Why |
|---|---|---|
| DS1 + DS2 + DS3 | **DS3 only**, ER later | Each game is a separate target (arch, API, offsets). DS3 is x64/D3D11 and the best documented. |
| "Inject FSR 3 frame-gen at the swap chain" | FSR 1 (EASU+RCAS) first; temporal/FG is a research spike | DS3 doesn't give you the motion vectors/depth that FSR 3 needs. See `docs/04`. |
| "Compress textures to fix fill-rate" | **Particle overdraw mitigation** | Fill-rate is about pixels shaded, not texture size. |
| vcpkg for everything | **CMake FetchContent with pinned deps** | vcpkg + Linux→MinGW cross-compile is a time sink for a solo dev. vcpkg stays optional for host tools. |
| Write C++ DCX/BND/BHD5 parsers | **C# SoulsFormats in a container for offline baking**; C++ only for what runtime needs | Less code, battle-tested parsers. |
| ModEngine2 as the base | **Own `dinput8` proxy first**; ModEngine2 optional | The proxy works trivially under Proton; ME2 on Linux is fiddly. |
| (nothing) | **Remote inspector** (web UI on your host talking to the DLL) | Plays to your backend skills, and avoids in-game overlay/input problems. |

## Doc index

| File | Read it when |
|---|---|
| [`docs/01-architecture-and-roadmap.md`](docs/01-architecture-and-roadmap.md) | You want the big picture, milestones, risks, and Elden Ring prep. |
| [`docs/02-dev-environment.md`](docs/02-dev-environment.md) | You want to **start coding today**: Fedora setup, containers, CMake, working skeleton, test loop. |
| [`docs/03-reverse-engineering-toolkit.md`](docs/03-reverse-engineering-toolkit.md) | You want to find offsets, timing sources, and renderer internals. |
| [`docs/04-optimization-playbook.md`](docs/04-optimization-playbook.md) | You want the list of optimizations, their risks, and how to measure them. |
| [`docs/05-testing-strategy.md`](docs/05-testing-strategy.md) | You want to know how we test: unit, fuzz, shader, Wine smoke, in-game, perf regression. |
| [`docs/06-ci-cd-github-actions.md`](docs/06-ci-cd-github-actions.md) | You want the GitHub Actions design: CI, nightly, release, security hygiene. |
| [`docs/07-open-source-tools.md`](docs/07-open-source-tools.md) | You want the vetted list of open-source libraries/tools, licenses, and spikes. |
| [`docs/08-logging-and-telemetry.md`](docs/08-logging-and-telemetry.md) | You want the runtime logging, JSONL capture schema, and extraction plan. |
| [`docs/09-codebase-reference.md`](docs/09-codebase-reference.md) | You want a file-by-file map of every module and symbol implemented so far. |
| [`docs/guide-for-vibe-coders.md`](docs/guide-for-vibe-coders.md) | You want a quick plain-English tour of how the mod works without deep C++/DirectX jargon. |
| [`docs/10-deep-dive-for-learners-and-vibe-coders.md`](docs/10-deep-dive-for-learners-and-vibe-coders.md) | You want a zero-to-systems masterclass explaining DLL proxying, COM vtables, MinHook, harsh CI, safe recipes, and career leverage. |
| [`SECURITY.md`](SECURITY.md) | You want our offline safety threat model, PE binary mitigations (`HIGH_ENTROPY_VA`, `DYNAMIC_BASE`, `NX_COMPAT`), supply-chain policy, and vulnerability reporting instructions. |
| [`AGENTS.md`](AGENTS.md) | You are (or are prompting) an AI coding agent working in this repository. |

## Quick start (about 30 minutes to a loaded DLL)

```bash
# 1. host tools (Fedora)
sudo dnf install -y podman just git cmake ninja-build wine
# 2. create repo and copy the files from docs/02 section 5 (skeleton)
# 3. build containers + DLL
just img-build
just configure
just build            # -> build/win64-release/dinput8.dll
# 4. deploy into DS3 (Proton) and run
just deploy           # copies DLL next to DarkSoulsIII.exe
# Steam launch options for DS3:  WINEDLLOVERRIDES="dinput8=n,b" %command%
# 5. check soulsframe.log in the game folder
```

Definition of done for first light: the log shows `Present hook installed` and an average frame time every 120 frames.

## Ground rules

- **Offline only.** Injected DLLs in online sessions risk bans. The mod shows a warning and requires an explicit config opt-in.
- **Never ship game assets.** Texture replacements are generated on the user's machine from their own game files.
- Disable other injectors (ReShade, Special K, RTSS, Steam overlay) while testing. They hook the same functions.
- Pin your game version. Record the `DarkSoulsIII.exe` SHA-256 in `re/db/` and key every offset by it.
- **Measure first.** No optimization merges without a before/after frame-time capture (`docs/04`).
