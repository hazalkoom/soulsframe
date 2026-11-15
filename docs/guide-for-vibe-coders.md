# SoulsFrame guide for vibe coders (plain-English tour)

Don't worry if you aren't a C++ or DirectX systems programmer. This guide gives you a fast orientation to what **SoulsFrame** is doing under the hood, why it is layered safely, and how to collaborate with AI coding agents on it.

> Want to go deeper? Read [docs/10-deep-dive-for-learners-and-vibe-coders.md](10-deep-dive-for-learners-and-vibe-coders.md) for visual step-by-step explanations of DLL proxying, COM vtables, function hooking, harsh CI gates, contribution recipes, and career skill leverage.

## 1. What are we actually building?

Normally, when **Dark Souls III** starts on Windows (or Proton on Linux), it asks Windows for `dinput8.dll` (the controller/keyboard input library) and `d3d11.dll` (the graphics library that draws frames to your screen).

SoulsFrame creates its own fake `dinput8.dll` that sits right next to `DarkSoulsIII.exe`:

```text
  DarkSoulsIII.exe
        │
        ├──► asks for DirectInput ──► Our dinput8.dll ──► forwards to real Windows dinput8.dll
        │                                   │
        └──► draws every frame ─────► (intercepted!) ──► Measures timing & optimizes GPU work
```

Think of it like installing a smart power meter and surge protector between a wall outlet and your PC. The PC still gets normal power (controller input still works), while our box watches what flows through, cleans up stutters, and skips wasted graphics work.

## 2. Why is it built in layers?

Because testing inside a real 3D game every time you change 5 lines of code is painfully slow, we split the code into two worlds:

1. **The "Pure Brain" (runs on Linux natively, tests in 0.1 seconds):**
   - Reading your `soulsframe.toml` settings file
   - Checking the **Offline Safety Guard** (making sure you don't accidentally go online and get banned)
   - Calculating FPS, averages, and frame stutter spikes
   - Matching byte patterns and hashing textures/executables
2. **The "Windows Hands" (cross-compiled into `dinput8.dll`):**
   - Talking to Windows (`DllMain`, `soulsframe.log`)
   - Attaching hooks onto DirectX (`IDXGISwapChain::Present` and `ResizeBuffers`)

Because the "Pure Brain" never touches Windows code, we can test all its math and safety rules cleanly right on Linux!

## 3. Folder map (where things live)

| Folder | Plain-English meaning | Safe to ask AI to edit? |
|---|---|---|
| `config/` | Example `soulsframe.toml` settings file for players | Yes — great place to tweak defaults |
| `docs/` | Roadmaps, plans, test strategies, and this guide | Yes |
| `reports/` | Summaries of what was built and verified at each step | AI updates after each milestone |
| `src/core/` | Startup glue, config reader, logger, safety guard, module loader | Careful — always run tests after |
| `src/gfx/d3d11/` | DirectX 11 hooks that see frames being drawn | Advanced — test under Wine/Windows |
| `src/timing/` | Frame-time math (FPS, 1% lows, stutters) | Yes — well covered by unit tests |
| `src/games/ds3/` | Dark Souls III-specific memory signatures (none active yet in Step 1) | Careful — game-version sensitive |
| `tests/unit/` | Fast automated self-checks for the "Pure Brain" | Yes — add tests with every feature |
| `tests/testbed/` | A tiny fake D3D11 mini-app used to test `dinput8.dll` without owning DS3 | Careful |
| `docker/` & `.github/` | Containers and automatic GitHub checks | Rarely needs editing |

## 4. Built-in safety guardrails

- **Offline-only lock:** By default (`offline_acknowledged = false`), the mod **refuses to hook anything** and writes a reminder in `soulsframe.log`. Players must explicitly flip `offline_acknowledged = true` in `soulsframe.toml` so nobody runs hooks online by mistake.
- **Kill switches:** Every feature can be turned on or off individually under `[modules]` in `soulsframe.toml`.
- **Unknown-build shield:** If Dark Souls III has an unfamiliar `.exe` hash, SoulsFrame automatically turns off engine memory patches while keeping safe D3D11 frame-time logging alive.
- **Failure isolation:** If one feature fails to start, the mod logs the failure and keeps the game running instead of crashing.

## 5. How to prompt AI agents effectively on this repo

Every AI agent automatically reads `AGENTS.md`, which teaches it our safety rules. When asking an AI to add or change something, use prompts like:

- *"Add a `[modules]` setting for X in `config.h` and write a doctest unit test for it in `tests/unit/test_config.cpp`."*
- *"Run `host-tests` and `scripts/ci/check-guards.sh` to verify nothing broke."*
- *"Update `reports/` explaining what you changed in plain English."*

Every pull request and push is protected by strict automated checks (`scripts/ci/check-guards.sh`, `clang-format`, `clang-tidy --warnings-as-errors`, `cppcheck`, CodeQL, `gitleaks`, `-Werror` unit tests with 605 assertions, and a 3-scenario Wine DirectX 11 test). Even if an experiment breaks something, CI catches it immediately before anything merges into `main`.

## 6. Things you should NOT ask an AI to put in Git

- Never commit files copied from your Dark Souls III game folder (`.exe`, `.bdt`, `.bhd`, `.dcx`, `.tpf`, `.fxr`).
- Never disable the offline guard or `scripts/ci/check-guards.sh` just to make a check pass.
