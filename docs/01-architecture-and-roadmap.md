# 01. Architecture and roadmap (DS3 first)

## 1. Goals and non-goals

**Goals**

- A DS3 (x64, D3D11) mod that improves frame-time consistency, GPU cost in worst-case scenes, and optionally resolution/FPS.
- A reusable instrumentation and RE toolkit so the work is evidence-driven.
- A code structure where Elden Ring can be added without a rewrite of the non-graphics layers.

**Non-goals (for now)**

- DS1/DS2 support.
- Online play support.
- Competing with driver-level frame generation (AFMF) or Lossless Scaling. Frame generation is a research spike only.
- A GUI executable. Everything is DLL + config + optional remote inspector.

## 2. Feasibility by feature

| Feature | Needs game RE? | Feasibility | Notes |
|---|---|---|---|
| Present hook + frame-time telemetry | No | Easy | Milestone M1. |
| GPU per-pass timing | No (D3D11 queries) | Easy-medium | Basis for everything else. |
| Flip-model presentation / frame pacing | Little | Medium | Depends on how DS3 creates its swap chain and MSAA. Verify first. |
| Low-res particle pass | No, but needs pass detection | Medium-hard | Blend state + depth state signatures; composite correctly. |
| Particle "data diet" (fewer/smaller effects) | Offline asset edits | Medium | Zero runtime risk, but changes visuals. Needs FXR tooling. |
| Shader overrides (cheaper variants) | No | Medium | Hash-keyed replacement like 3DMigoto/Special K. |
| Texture swap / HD or optimized textures | Little | Medium | Hook `CreateTexture2D`, match by content hash. |
| Render scale + FSR 1 | Yes (resolution constants in cbuffers) | Medium-hard | Resizing render targets breaks shaders that read screen size. |
| Temporal upscaler (FSR 2/3 class) | Yes (velocity/depth) | Hard / research | Only if DS3's motion-blur velocity buffer is usable. Verify in RenderDoc. |
| Safe FPS unlock | **Heavily** | Hard, high risk | Physics/animation assume 60 Hz. See `docs/04` §FPS. |
| Frame generation | Yes | Research | Go/no-go after M8. |

## 3. Architecture

```text
          DarkSoulsIII.exe  ──loads──►  dinput8.dll (SoulsFrame)
                                          │
   ┌───────────────┬───────────────┬──────┴───────┬────────────────┐
   │ core/         │ hooks/        │ gfx/d3d11/   │ timing/        │
   │ proxy, config │ MinHook wrap, │ swapchain,   │ clock hooks,   │
   │ log, registry │ sigscan,      │ context &    │ fixed-step,    │
   │ offline guard │ patches       │ device hooks │ limiter        │
   └──────┬────────┴───────┬───────┴──────┬───────┴────────┬───────┘
          │                │              │                │
   ┌──────▼──────┐  ┌──────▼──────┐ ┌─────▼──────┐  ┌──────▼───────┐
   │ games/ds3/  │  │ tex/        │ │ gfx/fx/    │  │ probe/       │
   │ offsets,    │  │ hash, swap  │ │ fsr1,      │  │ (SF_PROBE    │
   │ sigs, struct│  │ cache       │ │ particles  │  │  builds only)│
   └─────────────┘  └─────────────┘ └────────────┘  │ tracer,      │
                                                    │ dump, server │
                                                    └──────────────┘
```

### Layering rules

1. `games/ds3/` is the **only** place with offsets, signatures, and engine struct layouts.
2. `gfx/d3d11/` knows D3D11 and nothing about DS3. DS3-specific heuristics (for example "which pass is particles") live in `games/ds3/render.cpp` and are passed in as a `RenderProfile`.
3. `probe/` is compiled only with `-DSF_PROBE=ON`. The release DLL carries none of it.
4. Any logic that can be pure (hashing, DDS parsing, signature matching, config) must be testable natively on Linux with no Win32.

### Module lifecycle

Every feature is a `Module` registered in `module_registry`:

```cpp
struct Module {
  virtual const char* name() const = 0;
  virtual bool init(const GameProfile&, const Config&) = 0;  // install hooks
  virtual void on_present(PresentCtx&) {}                    // per frame
  virtual void shutdown() = 0;                               // remove hooks
};
```

Modules fail independently: if a sigscan fails, log it, disable only that module, and keep the game running.

### GameProfile (the Elden Ring seam)

```cpp
struct GameProfile {
  std::string id;                         // "ds3"
  std::string exe_sha256;                 // pinned game build
  std::optional<uintptr_t> frame_dt_ptr;  // each capability may be absent
  std::optional<uintptr_t> frame_limiter;
  std::optional<GfxSettingsLayout> gfx_settings;
  RenderProfile render;                   // heuristics for pass detection
};
```

`games/ds3/profile.cpp` fills this in. Unknown exe hash means **don't hook engine code**, only run the safe D3D layer and telemetry.

## 4. Milestones

Effort figures assume part-time solo work. Treat them as rough.

| # | Milestone | Done when | Rough effort |
|---|---|---|---|
| **M0** | Toolchain + containers (**Done**) | `just build` produces `dinput8.dll`; Wine smoke test passes in CI | 1-2 days |
| **M1** | First light (**Done**) | DLL loads under Proton; `Present` hooked; frame-time log | 2-4 days |
| **M2** | Probe v1 (**Done**) | Draw-call tracer with call-site aggregation; pass boundary detection; resource list; JSONL export; Python analysis script | 1-2 weeks |
| **M3** | GPU pass timing + benchmark harness | Per-pass GPU ms; repeatable benchmark routes; baseline report committed | 1 week |
| **M4** | RE foundation | Clean memory dump of the unpacked exe in Ghidra; time-API tracer finds frame limiter and dt sources; signature DB with SHA-256 key | 1-3 weeks |
| **M5** | Presentation fixes | Flip-model/pacing experiment with measured result (keep or drop) | 1 week |
| **M6** | Particle mitigation | Low-res particle pass or shader/data diet with measured GPU gain in worst scenes | 2-4 weeks |
| **M7** | Render scale + FSR 1 | Internal scale 0.5-1.0 works without broken effects; RCAS sharpen; toggle at runtime | 3-5 weeks |
| **M8** | FPS unlock experiments | Experiment log of what breaks at 90/120 Hz; decision: fixed-step + interpolation, or ship without | open-ended |
| **M9** | Texture swap/bake | C# bake tool + runtime hash swap; optional HD packs | 2 weeks |
| **M10** | Polish/release | Config UX, warnings, docs, Windows-native validation | 1-2 weeks |

Order matters: M2-M4 are what turn guessing into engineering. Skip them and every later milestone costs more.

### Acceptance rules for every milestone

- Passes the CI pipeline and the testing definition of done (`docs/05` §8, `docs/06`).
- Works on **real Windows**, not only under Proton/DXVK. DXVK changes timing and some behavior.
- A **kill switch** exists (config key or hotkey) that disables the feature at runtime.
- A **before/after capture** is stored in `bench/`.

## 5. Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Anti-tamper (Arxan-style integrity checks) makes code patches crash or misbehave | Medium | High | Hook D3D/DXGI (outside game code) first; patch game code late, after the menu; hardware-breakpoint style tracing for research; verify behavior and keep patches minimal. |
| Offsets differ by game build | Low (DS3 is long stable, verify) | Medium | Key all offsets by exe SHA-256; refuse engine hooks on unknown builds. |
| Overlay conflicts (Steam, RTSS, ReShade) | High | Low-medium | Document; detect known modules in the log. |
| Proton/DXVK behaves differently from Windows | High | Medium | Treat Proton as the dev loop, Windows as the truth. Keep a Windows test box/VM or a friend with a GPU. |
| FPS unlock never becomes safe | Medium-high | Medium | It's one milestone, not the project. The rest ships without it. |
| Scope creep | High | High | Milestones above; kill-switch rule; one feature at a time. |
| Ban risk / ToS | Medium | High | Offline-only guard, loud docs, no online testing. |

## 6. Elden Ring prep (do now, build later)

Facts to plan around (verify when you start):

- ER is **D3D12**, not D3D11. `gfx/d3d11/` does not port. You'll add `gfx/d3d12/` with its own hooks (command queue/list, `Present` on the DXGI side).
- ER uses **Easy Anti-Cheat**; modding means launching without it and therefore offline only.
- Engine lineage is shared (DS3 → Sekiro → ER), so *ideas* transfer: time sources, param/settings structs, FXR particle format. *Offsets* don't.

What to do in DS3 code so ER is cheap later:

1. Keep `core/`, `hooks/`, `timing/`, `tex/`, `probe/server` free of D3D11 types.
2. Hide the renderer behind a minimal interface (`on_present`, `backbuffer info`), implemented by `gfx/d3d11/` now and `gfx/d3d12/` later.
3. Keep the RE toolkit game-agnostic: `sftool` takes `--game` and an offsets DB path.
4. Don't build the ER side yet. An unused abstraction costs more than a refactor later.

## 7. Decision log (short ADRs)

| # | Decision | Alternatives rejected |
|---|---|---|
| D1 | Proxy `dinput8.dll` as loader | ModEngine2 external DLL (optional later); launcher exe |
| D2 | MinGW-w64 toolchain, static runtime | clang-cl + xwin (MSVC ABI) unless forced |
| D3 | CMake FetchContent, pinned tags/commits | vcpkg for the DLL (cross-compile friction) |
| D4 | HLSL compiled at runtime via `D3DCompile`, cached | `fxc` at build time (Windows-only); DXC (can't emit DXBC for D3D11) |
| D5 | MinHook first; add a mid-function hook lib only when needed | SafetyHook/Detours up front |
| D6 | C++20 | C++23 (compiler/MinGW support risk) |
| D7 | Offline bake tools in C# (SoulsFormats) in a container | Rewriting BND/BHD/TPF in C++ |
| D8 | Remote inspector (HTTP/WebSocket + web UI) | In-game ImGui overlay (input capture pain; may add later) |
| D9 | Analysis scripts in Python | Custom C++ analyzers |
