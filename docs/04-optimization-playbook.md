# 04. Optimization playbook (DS3)

How to find out what's actually slow, which fixes are worth the risk, and how to prove each one worked.

**Principle:** the list below is a set of *hypotheses*. Some will turn out to be irrelevant for DS3. The M2/M3 tooling decides the order. Don't pick a favorite technique before measuring.

## 1. Measurement methodology

### 1.1 Benchmark scenes
Pick 5-8 repeatable spots and save a character at each (offline). Include a mix of:
- GPU-heavy: fog/volumetric areas, heavy particle effects, large open vistas, boss arenas with lots of FX. Candidates reported by players as heavy include Irithyll of the Boreal Valley, Smouldering Lake, and Profaned Capital. **Verify yourself**, since reports vary by hardware.
- CPU-heavy: dense enemy areas, large interiors.
- Streaming-heavy: fast traversal / area transitions.
- A cheap baseline: an empty room (floor on overhead).

For each, run a fixed 60-second scripted path (or stand still plus fixed camera angle for the first version).

### 1.2 Metrics
| Metric | Why |
|---|---|
| Avg frame time and FPS | Headline |
| **1% and 0.1% lows** | What players feel |
| Frame-time stddev / spike count (> 2× median) | Pacing |
| GPU ms per pass (from `sf-probe`) | Where the cost is |
| CPU frame ms (Tracy zones around hooks, or the game's loop once found) | CPU vs GPU bound |
| VRAM usage | Texture changes |
| Input-to-photon latency (optional; needs hardware) | Pacing/flip-model changes |

Capture with the probe's JSONL + `tools/py/plot_frametimes.py`. Store captures in `bench/<date>_<scene>_<commit>.jsonl`.

### 1.3 Rules
- Same hardware, driver, resolution, settings, and power profile for A/B.
- ≥ 3 runs per config; discard run 1 (shader cache / streaming warmup).
- Record GPU/CPU clocks if you can; thermal throttling lies.
- Test on **Windows**. Proton numbers are for development only.
- Every PR touching rendering/timing includes a `sftool trace diff` result.

### 1.4 CPU-bound or GPU-bound?
If GPU pass total ≈ frame time → GPU-bound: particles/post/resolution changes help.
If GPU idles between frames and one CPU thread is pegged → CPU-bound: render-scale/FSR won't help; look at CPU hot spots (Tracy) and the game's frame loop.

## 2. Optimization catalog

Legend: **RE** = needs game reverse engineering. **Risk** = chance of visual/gameplay bugs or crashes.

| # | Technique | RE? | Risk | Prereq | Notes |
|---|---|---|---|---|---|
| T1 | Frame-time telemetry + GPU pass timing | No | None | M1-M3 | Foundation |
| T2 | Frame pacing / presentation fixes | Little | Low-Med | T1 | §3 |
| T3 | Low-res particle pass | No* | Med | T1, pass detection | §4. *Needs RenderDoc/tracer to identify the pass |
| T4 | Particle data diet (offline FXR edits) | Format RE | Low | FXR tooling | §5 |
| T5 | Shader overrides (cheaper variants of expensive PS) | No | Med | Shader registry | §6 |
| T6 | Texture work: VRAM, streaming, HD packs | Little | Low | Resource registry | §7 |
| T7 | Render scale + FSR 1 | **Yes** | Med-High | RE target #4 | §8 |
| T8 | Temporal upscale (FSR 2-class) | **Yes** | High | Usable depth + velocity | §9 |
| T9 | Safe FPS unlock | **Yes** | High | Time-source RE | §10 |
| T10 | CPU hot-spot fixes | **Yes** | Med | Tracy + RE | §11 |
| T11 | Frame generation | **Yes** | Very high | T9, T8 | §12 |

## 3. T2: Frame pacing and presentation

Hypotheses to test (each could be a no-op):
- **Flip-model swap chain** (`FLIP_DISCARD`, `ALLOW_TEARING` for VRR) can lower latency and avoid compositor copies. Caveats: flip model doesn't allow MSAA back buffers, so check how DS3 renders and resolves; fullscreen-exclusive vs borderless matters; Windows "fullscreen optimizations" may already do this for you.
- **Frame-latency waitable object** (`DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`, `SetMaximumFrameLatency(1)`) reduces queued frames.
- **Own frame limiter** (hybrid sleep + spin on QPC) in place of the game's if the game's limiter produces uneven pacing. Needs the limiter RE target, or cap at `Present` instead.
- Sync interval / VRR behavior at 60 Hz caps.

How: intercept `IDXGIFactory::CreateSwapChain*` and modify the `DXGI_SWAP_CHAIN_DESC` before creation. Measure frame-time variance and latency.
Kill switch: config `present.mode = "game" | "flip"`.

## 4. T3: Low-res particle pass

**Problem:** large overlapping transparent quads (fog, smoke, spell FX) are limited by pixels shaded (overdraw), and cost scales with resolution × layers.

**Approach (well-known "off-screen / low-resolution particles"):**
1. Detect the particle pass: render target set unchanged, **blend enabled** (alpha/additive), **depth test on, depth write off**, draw count high. Verify in RenderDoc, then encode the heuristic in `games/ds3/render.cpp`.
2. At pass start: create a half- (or quarter-) res RT (RGBA16F) + a **downsampled depth** (min or max of 2×2) to keep occlusion correct.
3. Redirect the pass's draws to the low-res RT (adjust viewport/scissor; clear to transparent black).
4. Composite back with an **upsample + blend** that matches the original blend modes. Additive and premultiplied alpha compose cleanly; non-premultiplied alpha is harder. You may need two accumulation targets (additive vs alpha).
5. Optional: bilateral/depth-aware upsample to avoid halos at depth edges.

**Pitfalls:** soft-particle shaders that sample the depth buffer (they need the *downsampled* depth bound at the same slot); particles that read the scene color (refraction/heat haze) can't be redirected; MSAA depth; HDR format mismatches; visible blockiness on sharp FX (use per-effect exceptions).
**Success metric:** GPU ms for the particle passes in the worst scenes, with side-by-side image comparison at fixed camera.

## 5. T4: Particle "data diet"

Particle behavior is data-driven (DS3 uses FXR effect files, which are in the game archives). Editing them offline (reduce spawn counts, lifetimes, billboard sizes on the worst offenders) removes work at the source with no runtime hooks.

- Tooling: SoulsFormats (C#) has support for the container formats; verify FXR support for DS3 specifically. Otherwise use ImHex + RE to map the format.
- Delivery: bake on the user's machine from their own files, then load via an asset-override path (our own file hook, or ModEngine2 as an optional route).
- Rank offenders using the tracer: which effects (by draw call site/resource) cost the most GPU ms.
- Trade-off: it changes the look. Offer presets (`faithful` / `balanced` / `performance`).

## 6. T5: Shader overrides

1. Registry gives you DXBC hash → GPU cost (attribute pass time to shaders by sampling timestamps per draw group).
2. Take the top cost shaders, disassemble (`D3DDisassemble`), and inspect: loop counts, texture fetch counts, expensive math (pow/exp/sqrt chains, large blur kernels).
3. Write a cheaper variant (fewer taps, lower-precision math, skipped optional layers). Place in `override/shaders/<hash>.hlsl`, compile at runtime with `D3DCompile`, cache the DXBC.
4. Compare image difference (RenderDoc screenshot diff) and GPU ms.

Candidates to examine: volumetric fog, SSAO, depth of field, motion blur, large-kernel bloom. Each is a hypothesis, so measure first.

## 7. T6: Textures

- **VRAM:** list uncompressed textures (R8G8B8A8 / large sizes) in the resource registry. Compress offline (BC1/BC3/BC7 via DirectXTex) and swap by content hash. This helps VRAM and bandwidth, **not** fill-rate.
- **Streaming hitches:** see whether texture creation happens on the render thread mid-frame (callsite + time in tracer).
- **HD packs:** the same swap mechanism; the pack builder runs on the user's machine.
- Hash: xxh3 of mip-0 initial data; stored in `cache/swap.bin` (mmap'd).
- Open question to verify with the tracer: does DS3 create textures with initial data (`CreateTexture2D` with `pInitialData`) or upload via `UpdateSubresource`/staging? The swap hook point depends on it.

## 8. T7: Render scale + FSR 1

Two parts, and the second is the hard one.

**(a) Upscale pass (easy):** in the `Present` hook, take the scaled color buffer, run **EASU** (upscale) + **RCAS** (sharpen) as compute passes into the real back buffer. FSR 1 shader code is MIT-licensed HLSL (`ffx_a.h`, `ffx_fsr1.h` in AMD's FidelityFX repos). Compile at runtime with `D3DCompile`.

**(b) Reduce internal resolution (hard):**
1. Intercept `CreateTexture2D` for textures whose dimensions equal the screen size and scale them (render targets, depth, G-buffer-like targets). Keep UI targets at native size. Identify UI by when it draws (last passes) via the tracer.
2. Scale viewports/scissors in `RSSetViewports/RSSetScissorRects`.
3. **Fix shader constants** that hold screen size / inverse size / pixel offsets (cbuffers). This requires RE: find them via the tracer's cbuffer dump and diffing between resolutions.
4. HUD must render at native resolution after the upscale (or be composited post-upscale).
5. Post effects (bloom, SSAO) sample at assumed resolutions. Expect artifacts you need to chase.

Prior art: DSfix did this for DS1 (D3D9); study its approach.
Where FSR 1 loses: it's spatial-only, so image quality drops below ~0.67× scale. Offer scale presets (1.0/0.77/0.67/0.5).

## 9. T8: Temporal upscaling (research)

FSR 2/3-class upscalers need per-pixel **motion vectors**, **depth**, and jitter. Does DS3 have a velocity buffer (for its motion blur)? Check in RenderDoc: look for an RG16F/RG8 target written by moving geometry and read by the motion-blur pass. If yes, you might use it. If no, you'd have to generate vectors yourself (very expensive). Decide with data. Jitter injection requires modifying the projection matrix in constant buffers (RE).
AMD's FSR 2 had a D3D11 backend in some releases, and later FidelityFX SDK versions focus on D3D12/Vulkan. Verify the current SDK before committing.

## 10. T9: Safe FPS unlock

**Why it's hard:** DS3 ties simulation (physics, animation events, damage, durability, status timers) to a 60 Hz step. Making the clock tick faster without changing how subsystems integrate time causes known glitches (weapon durability, fall damage, animation-driven effects, jumps, ladders, etc.).

**Strategies**, from least to most invasive:

| Strategy | Idea | Pros | Cons |
|---|---|---|---|
| A. Fixed 60 Hz sim + **render interpolation** | Run the simulation at 60, present extra frames interpolated | Gameplay identical | Need previous/next transforms of rendered objects or camera-only reprojection; large RE effort |
| B. Camera-only high-rate | Update camera/input at high rate, characters at 60 | Smooth camera feel (largest perceived gain) | Needs camera + input RE; visible judder on characters |
| C. Variable dt, **per-subsystem patching** | Let time scale and patch each integrator | "True" unlock | Fragile; per-system bugs; Arxan exposure |
| D. Global dt override (naive) | Change the one delta | Quick experiment | Known to break stuff: use only as an **experiment** |

**Plan:**
1. M4 gives the dt sources and limiter.
2. **Experiment E1** (naive dt override at 90/120) with a checklist: weapon durability loss per swing, fall damage thresholds, jump distance, roll i-frames, ladder speed, animation-triggered effects, spell cast timings, regen rates, bonfire/menus. Record results in `re/notes/fps_unlock_experiments.md`.
3. Based on results pick A or B (or ship without T9).

Kill switch required; default off.

## 11. T10: CPU hot spots

Only if M3 says CPU-bound.
- Tracy zones around the hooks you own; sampling profile via Tracy or `perf` (under Proton, symbolication of game code is the hard part, so use your Ghidra names).
- Candidates (hypotheses): per-frame allocations, shader/resource creation on the render thread, thread contention around the render submission, driver-side overhead from many small draws/state changes (consider redundant state-change filtering in context hooks).
- Redundant-state filtering (skip `PSSetShaderResources`/`OMSetBlendState` calls that don't change anything) is low-risk and safe to try early.

## 12. T11: Frame generation (research)

Needs depth, motion vectors, and a clean HUD-less color buffer, none of which DS3 hands you. Driver-level and screen-capture generators already exist and work without engine knowledge. Go/no-go after M8: only continue if T8 found usable motion vectors and you have a specific reason to beat existing tools.

## 13. Test matrix for every change

| Axis | Values |
|---|---|
| OS | Windows (truth), Proton (dev) |
| GPU vendor | At least two if you can borrow (AMD/NVIDIA) |
| Display mode | Borderless, fullscreen, windowed |
| Resolution | 1080p, 1440p, 4K |
| Settings | Low, max |
| Other injectors | None (clean), Steam overlay on |
| Scenes | All benchmark scenes + a 30-minute soak |

## 14. Release checklist

- [ ] Offline-only warning shown; config opt-in required.
- [ ] Unknown exe hash → engine hooks disabled, D3D-only features still safe.
- [ ] Every feature has a runtime kill switch and a config default of **off** if risky.
- [ ] Bench reports for each feature included in the repo.
- [ ] No game assets or dumps in the repo or release.
- [ ] Tested on real Windows, clean and with the Steam overlay.
- [ ] Crash handler writes `soulsframe_crash.txt` (module RVA of faulting address, last 100 log lines).
