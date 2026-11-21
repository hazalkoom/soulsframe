# 15. Real Windows Validation & Release Checklist (`v0.10.0`)

Proton/Wine + DXVK validates DXGI hooks, D3D11 pass detection, and shader compilation logic, but Proton replaces Microsoft's DXGI swap-chain scheduler and driver query behavior. Always run final validation on **real Windows 10/11** with **Dark Souls III** strictly **offline** before cutting an end-user release.

---

## 1. Windows installation & offline safety verification

1. Download or build `soulsframe-v0.10.0-win64.zip` (or use `win64-probe` when capturing frame data).
2. Extract `dinput8.dll` and `soulsframe.toml` (or one of the files from `presets/`) into the directory containing `DarkSoulsIII.exe` (for example `...\SteamLibrary\steamapps\common\DARK SOULS III\Game`).
3. Verify the safety guard **before** acknowledging offline mode:
   - Leave `offline_acknowledged = false` in `soulsframe.toml`, launch DS3 offline, and exit at the title screen.
   - Open `soulsframe.log` and confirm `[WARN] OFFLINE GUARD BLOCKED MOD INIT` appears and no swap-chain hooks were installed.
4. Set Steam to **Offline Mode** (or disable network access / set the game's Start Offline option), then edit `soulsframe.toml`:

   ```toml
   [general]
   offline_acknowledged = true
   mod_enabled = true
   log_level = "info"
   ```

5. Optionally validate your config on Linux or MSYS2 beforehand using `sftool`:

   ```bash
   ./build/host-tests/tools/cpp/sftool config-check soulsframe.toml
   ```

---

## 2. Config presets guide

Copy any file from `config/presets/` (or `presets/` in the release archive) over `soulsframe.toml` and set `offline_acknowledged = true`:

| Preset | Purpose | Key behavior |
|---|---|---|
| `presets/safe-frame-pacing.toml` | Lowest-risk frame-time stabilization (M5) | Forces windowed `FLIP_DISCARD` swap chain and paces to 60 FPS with `timeBeginPeriod(1)` + spin tail. Zero visual changes. |
| `presets/low-end-gpu.toml` | Weak/integrated GPU frame-rate boost (M5 + M6 + M7) | Combines flip-model pacing, half-res instanced particle mitigation (`auto`, 50%, max 128 instances), and FSR 1 (`quality`, 67%, sharpness 80%). |
| `presets/probe-capture.toml` | Profiling capture for `win64-probe` builds (M2 + M3) | Records 120 frames of draw-site, pass-tag, texture/RT resource, and D3D11 timestamp (`gpu_ms`) JSONL telemetry. |

---

## 3. Overlay conflict checklist

Before measuring performance on Windows, disable third-party injectors that hook `Present` or `CreateSwapChain`:

- Steam In-Game Overlay (Right-click DS3 in Steam -> Properties -> General -> uncheck Enable Steam Overlay)
- RivaTuner Statistics Server (RTSS) / MSI Afterburner OSD (for A/B measurements, prefer external ETW tools like PresentMon or CapFrameX)
- ReShade / Special K / NVIDIA App / AMD Adrenalin overlays

When `soulsframe.log` prints `[WARN] warning: detected overlay/injector module loaded: ...`, temporarily disable that injector if stuttering or hook collisions occur.

---

## 4. Windows A/B validation matrix

Capture 60-second runs in the same camera angle (e.g., Cemetery of Ash bonfire looking toward Iudex Gundyr arena, or Firelink Shrine) using PresentMon / CapFrameX (`avg FPS`, `1% low`, `frame time stddev`) plus `sf-probe` captures:

| Run | Config | What to verify |
|---|---|---|
| **Baseline** | No `dinput8.dll` (renamed away) | Stock 60 FPS / frame-time variance baseline |
| **M5 Flip + Pacer** | `presets/safe-frame-pacing.toml` (`offline_acknowledged = true`) | Log shows `CreateSwapChain ...` flip upgrade or fallback; lower 1% low spikes |
| **M6 Particles** | `modules.particles = true` | Worst-case fog/fire scenes show lower GPU frame time in `analyze_capture.py` |
| **M7 FSR 1 + Resize** | `modules.upscaler = true` (`fsr1`, `quality`) | Visual crispness check + `Alt+Enter` / resolution change logs `resizes=...` cleanly |
| **M9 Texture Swap** | `modules.textures = true`, baked pack in `soulsframe_textures/` | Log shows `replaced > 0`, `invalid = 0`, zero hitching or visual corruption |

Store verified logs and PresentMon summaries under `bench/` (remember never to commit `*.exe`, `*.dll`, or game files).

---

## 5. Cutting a GitHub Release (`v0.10.0`)

1. Confirm all CI checks pass on `main` (`guards`, `docs`, `static-analysis`, `host-tests`, `benchmarks`, `coverage`, `fuzz-smoke`, `win64`, `wine-smoke`, `docker-verify`, `security`, `CodeQL`).
2. Confirm clean git status and run local final checks (`check-guards.sh`, `host-tests`, `win64-release`, `win64-probe`, Wine `smoke-test.sh`).
3. Create and push an annotated SemVer tag matching `CMakeLists.txt` (`project(soulsframe VERSION 0.10.0 ...)`):

   ```bash
   git tag -a v0.10.0 -m "SoulsFrame v0.10.0"
   git push origin v0.10.0
   ```

4. Monitor [.github/workflows/release.yml](../.github/workflows/release.yml) until `build-and-package`, `smoke-verify`, and `publish-release` complete with signed build provenance attestations (`soulsframe-v0.10.0-win64.zip` and `soulsframe-v0.10.0-win64-probe.zip`).
