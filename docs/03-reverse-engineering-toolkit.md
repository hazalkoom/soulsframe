# 03. Reverse-engineering toolkit and workflow (DS3)

Yes, you can reverse-engineer the game to optimize beyond what pure D3D hooking allows. The point of the tooling is to make RE **systematic and repeatable**, so findings survive and are verifiable.

> Legal and practical note: do RE on your own legitimate copy, for interoperability/personal use. Don't commit or distribute game binaries, memory dumps, or extracted assets. `re/dumps/` is gitignored for that reason. Publish only your own code, signatures/offsets, and notes.

## 1. What you can learn, in order of payoff

| # | Target | Why it matters |
|---|---|---|
| 1 | **Frame limiter and delta-time sources** | Required for any FPS unlock; also reveals the main loop |
| 2 | **Main loop / the function that calls `Present`** | Anchor for everything else (render thread, frame boundaries) |
| 3 | **Graphics settings struct and its apply functions** | Expose hidden/higher settings; know when resolution changes |
| 4 | **Resolution-dependent render targets and constant buffers** | Needed for render-scale + FSR without broken effects |
| 5 | **Particle/FX system** (spawn, update, per-effect caps, draw submission) | The biggest known GPU sink; can cull or throttle at the source |
| 6 | **Texture load path** (TPF data → D3D resource) | Clean place to swap textures and control streaming |
| 7 | Camera/FOV | Nice to have |

## 2. Toolbox

| Tool | Where it runs | Purpose |
|---|---|---|
| **Ghidra** (GUI native, headless in container) | Fedora | Static analysis, decompiler, scripting |
| **x64dbg** | Wine | Dynamic analysis, hardware breakpoints, memory view |
| **Cheat Engine** or **PINCE** | Wine / native Linux | Value scanning, "what writes/reads this address" (CE under Wine can be finicky; try both) |
| **ReClass.NET** | Wine (needs .NET) | Structure recovery |
| **RenderDoc** | Fedora/Windows | Frame structure, draw calls, shader constants |
| **ImHex** | Fedora | File formats (TPF/DCX/FXR) |
| **Tracy** | Fedora viewer + DLL client | CPU timeline across your own hooks |
| **`sf-probe`** (you build it) | Inside the DLL | D3D11 tracer, call-site mapping, dumps, inspector server |
| **`sftool`** (you build it) | Fedora, native | Signature maker/tester, offsets DB, trace analysis |
| Python + pandas | Fedora | Analysis of trace exports |

Existing community work to study (don't reinvent): SoulsFormats (C# parsers), Yabber/UXM-style unpackers, DS3 Cheat Engine tables from the modding community, open-source FPS unlockers for FromSoft games, ModEngine2 source, Special K, 3DMigoto/ReShade (shader hunting patterns).

## 3. Getting a Ghidra-friendly binary

The on-disk `DarkSoulsIII.exe` is protected by anti-tamper/obfuscation (Arxan-style), so static analysis of the file on disk is mostly noise. The usable approach is a **runtime memory image**:

1. Launch the game, reach the main menu, and **don't touch online features**.
2. Dump the module image from memory. Option A: `sf-probe dump` (command via the inspector server). Option B: x64dbg + Scylla-style plugin, or `pe-sieve` under Wine.
3. Fix up the dump as a PE for Ghidra: set each section's raw offset/size equal to its virtual address/size (an "unaligned dump"), and keep the real `ImageBase`. Rebuild import names by scanning loaded modules' exports. Imports are optional for Ghidra but make the decompiler output far more readable.
4. Import into Ghidra with the PE loader. Store the file at `re/dumps/ds3_<version>_<sha256-prefix>.exe` (gitignored).
5. Record the on-disk exe's SHA-256 in `re/db/builds.json`. All offsets are keyed to it.

Expect some code regions to still be encrypted/unpacked lazily, so dump again later (after loading into a map) and diff.

### `sf-probe dump` design
- Enumerate the module's sections via `VirtualQuery` across `[base, base+SizeOfImage)`.
- Copy committed pages; zero-fill reserved/guard pages.
- Write headers + sections with the fix-ups above.
- Never write to or modify the game's memory. Read-only.

## 4. Docker pipeline for headless Ghidra

```bash
# build once (see docs/02 for the Dockerfile)
podman build -t sf-ghidra -f docker/ghidra.Dockerfile \
   --build-arg GHIDRA_URL=<release zip URL> .

# analyze a dump and export a function/symbol DB as JSON
podman run --rm --userns=keep-id -v $PWD:/work:Z sf-ghidra \
  analyzeHeadless /work/re/project SoulsDS3 \
    -import /work/re/dumps/ds3_dump.exe -overwrite \
    -scriptPath /work/re/ghidra_scripts \
    -postScript ExportSymbols.java /work/re/db/ds3_functions.json
```
- Raise the JVM heap in `support/launch.properties` (`MAXMEM`) for a big binary.
- `re/ghidra_scripts/` holds scripts: `ExportSymbols` (named functions → JSON), `ApplyOffsetsDb` (push your known names/offsets into a fresh analysis), `FindXrefsTo` (batch xref queries).
- Commit **scripts and exported JSON/notes**, not the Ghidra project or binaries.

## 5. Techniques

### 5.1 D3D11 as a Rosetta stone (API-anchored RE)
You can't read the engine yet, but you can see exactly when it calls D3D11. So:
1. Hook the `ID3D11DeviceContext` methods (see §6).
2. On each draw/state call, capture the **return address** and a short stack (`RtlCaptureStackBackTrace`).
3. Map those addresses into RVAs of the main module (`addr - module_base`).
4. Aggregate by call site per frame: "RVA 0x1A2B3C issues 340 draws using additive blending."
5. Open that RVA in Ghidra. The function name practically writes itself: `DrawParticleBatch`, `RenderShadowCascade`, etc.

This is the fastest way to bridge "I saw something in RenderDoc" to "this is the function."

### 5.2 Time-API tracing (find the limiter and dt)
Hook, from the probe, the functions a frame loop uses to measure and wait, and log callers:
`QueryPerformanceCounter`, `GetTickCount64`, `timeGetTime`, `Sleep`, `SleepEx`, `WaitForSingleObject(Ex)`, `WaitForMultipleObjects`, `NtDelayExecution`, plus `IDXGISwapChain::Present`'s sync interval.
Aggregate by caller RVA and call rate. The caller that fires once per frame and feeds a subtraction is your dt source; the one that blocks for the remainder of 16.67 ms is the limiter.

### 5.3 Value tracing (Cheat Engine / PINCE / x64dbg)
- Scan for a float near `0.016667` (or `1/60`) that changes each frame → candidate dt.
- "Find what accesses/writes" → which instructions touch it → Ghidra.
- Use **hardware breakpoints** where possible so you don't modify code bytes, which matters with integrity checks.
- Log every consumer of dt (animation, physics, durability, status effects, camera). That list is the real FPS-unlock work plan.

### 5.4 Signature scanning and the offsets DB
- **Never hardcode raw addresses.** Use signature patterns + RVA fallback keyed by exe hash.
- `sftool sigmaker --image dump.exe --rva 0x1A2B3C`:
  1. Decode instructions with Zydis from the RVA.
  2. Wildcard RIP-relative displacements, call/jmp targets, and absolute immediates.
  3. Extend byte-by-byte until the pattern is **unique** in the image.
  4. Output `48 8B ?? ?? 89 5C 24 ??`-style patterns.
- `sftool sigtest --image <dump> --db re/db/ds3_sigs.toml` verifies every pattern is still unique.
- DB entry format:

```toml
[build."<exe sha256>"]
version = "1.15.x"      # fill with what you actually observe

[[build."<exe sha256>".sig]]
name        = "frame_dt_read"
pattern     = "F3 0F 10 ?? ?? ?? ?? ?? F3 0F 59 ??"
offset      = 0          # bytes from match to the interesting instruction
kind        = "code"     # code | data | rip_rel_data
confidence  = "verified" # guessed | tested | verified
notes       = "reads float dt from global; 1 write site in main loop"
```

### 5.5 Findings records (so RE is reproducible)
Every RE claim gets a note in `re/notes/<topic>.md`:

```md
## <name>
- Claim:
- How found (tool + steps):
- RVA / signature:
- How verified (test, breakpoint, behavior change):
- Confidence: guessed | tested | verified
- Date / game build:
- Open questions:
```

## 6. `sf-probe`: the in-DLL tooling (build with `-DSF_PROBE=ON`)

### 6.1 D3D11 tracer
Hook (vtable patch or MinHook on the shared vtable entries; one set covers immediate + deferred contexts):

- **Context:** `Draw*`, `Dispatch*`, `OMSetRenderTargets*`, `OMSetBlendState`, `OMSetDepthStencilState`, `RSSetViewports`, `RSSetState`, `PSSetShader`, `VSSetShader`, `PSSetShaderResources`, `PSSetConstantBuffers`, `Map/Unmap`, `UpdateSubresource`, `ClearRenderTargetView`, `ClearDepthStencilView`, `CopyResource`, `CopySubresourceRegion`, `ResolveSubresource`, `GenerateMips`.
- **Device:** `CreateTexture2D`, `CreateBuffer`, `CreateRenderTargetView`, `CreateShaderResourceView`, `Create{Pixel,Vertex,Compute}Shader`, `CreateBlendState`, `CreateDepthStencilState`.
- **DXGI:** `Present`, `ResizeBuffers`, `IDXGIFactory::CreateSwapChain*`.

Rules: per-thread ring buffers (draws can come from several threads), merge at `Present`, never block the render thread on file I/O (flush on a worker).

**Per-frame record (JSON lines):**
```json
{"frame": 1042, "passes": [
  {"id": 7, "rt": ["R11G11B10_FLOAT 1920x1080"], "depth": "D32 ro", "draws": 312,
   "blend": "additive", "callsites": [{"rva": "0x1A2B3C", "draws": 300}], "gpu_ms": 2.41}
]}
```
**Pass boundary** = change of render-target set (or depth target) between draws. Good enough to start; refine with explicit clears.

### 6.2 GPU timing
`ID3D11Query` `TIMESTAMP_DISJOINT` per frame + `TIMESTAMP` at pass boundaries. Read results 2-3 frames later to avoid stalling. Timing resolution and reliability vary across Wine/DXVK vs. native drivers, so trust Windows numbers.

### 6.3 Resource & shader registries
- Track every texture: size, format, mips, usage, bind flags, content hash of mip 0 (xxh3), creation callsite RVA, bind counts per frame.
- Track every shader by DXBC hash. Dump `.dxbc` and a text disassembly (`D3DDisassemble` from `d3dcompiler_47.dll`, in-process, so no external tools) into `dump/shaders/<hash>.{dxbc,txt}`.
- **Override directory** `override/shaders/<hash>.{dxbc,hlsl}`: loaded at `CreatePixelShader` time. This is the same idea as 3DMigoto/Special K, and it's how you test "cheaper variant of the fog shader" in minutes.

### 6.4 Remote inspector (leans on your backend skills)
- Embed a small HTTP/WebSocket server in the DLL (header-only lib such as `cpp-httplib`; localhost only, off by default, random token in the log).
- JSON endpoints: `/frame/latest`, `/passes`, `/resources`, `/shaders`, `/hooks`, `/config` (GET/PUT), `/dump/module`, `/capture?frames=60`, `/toggle/<module>`.
- Build the UI in whatever you like (Node + a tiny React/htmx page, or a Python Streamlit app): flame-style pass timeline, resource browser, live parameter sliders.
- Works through Proton (localhost networking is shared).
- Why: in-game overlays need input capture, which is a rabbit hole (DS3 reads input via its own paths). An external UI avoids that.

### 6.5 Hot reload
Watch `override/` and `soulsframe.toml` (`ReadDirectoryChangesW`) and apply on change. Shader iteration should take seconds, not rebuilds.

## 7. `sftool` (host-native C++ + Python)

| Command | Function |
|---|---|
| `sftool sigmaker` / `sigtest` | §5.4 |
| `sftool builds add <exe>` | Hash exe, register in `re/db/builds.json` |
| `sftool trace summarize <jsonl>` | Per-pass and per-callsite stats |
| `sftool trace diff a.jsonl b.jsonl` | Before/after comparison for optimization PRs |
| `tools/py/plot_frametimes.py` | Frame-time histogram, 1%/0.1% lows, spikes |
| `tools/py/pass_waterfall.py` | Per-pass GPU timeline |

Pure logic (`sigscan`, hashing, DDS parsing, trace parsing) lives in code shared with the DLL and is unit-tested natively on Fedora with `ctest`.

## 8. A concrete first RE week (after M2)

| Day | Task | Output |
|---|---|---|
| 1 | RenderDoc capture of a heavy scene (fog/particles) | Pass list; hypothesis about the most expensive passes |
| 2 | Run tracer; aggregate by call site | Table: callsite RVA → pass → draws |
| 3 | Dump module at main menu; import to Ghidra (native GUI) | Analyzable image |
| 4 | Time-API trace | Candidate limiter and dt callers |
| 5 | Name functions in Ghidra, generate signatures, write first `ds3_sigs.toml` | First entries in offsets DB, with notes |

## 9. Safety rules while reverse engineering

- Offline, always. No network features during any RE session.
- Prefer **observation** (hooks on D3D/OS APIs, hardware breakpoints) over **modification** (patching game code) until a finding is understood.
- Patch the smallest thing possible, and make every patch revertible (store original bytes; restore on `shutdown()`).
- Unknown build hash → no engine hooks.
