# 07. Open-source tools and libraries we can use

A shortlist, grouped by purpose, with the decision for this project. "Use" means planned. "Evaluate" means try in a spike before committing. "Reference" means read and learn from, don't depend on. Licenses and versions drift: **verify each license and pin each version before adding a dependency**, and record it in `THIRD_PARTY_LICENSES.txt`.

## 1. Runtime libraries (linked into the DLL)

| Tool | Purpose | License (verify) | Decision |
|---|---|---|---|
| [MinHook](https://github.com/TsudaKageyu/minhook) | x86/x64 inline hooking, minimal | BSD-2 | **Use** (ADR D5). Fine for D3D/DXGI vtable hooks |
| [SafetyHook](https://github.com/cursey/safetyhook) | Modern hooking on top of Zydis; relocates RIP-relative code, handles mid-function hooks, suspends threads | BSD-2 | **Evaluate** for engine code patches (M4+); needs C++23, check MinGW support |
| [Zydis](https://github.com/zyantific/zydis) | x86-64 disassembler | MIT | **Use** in `sftool` sigmaker and possibly the DLL (via SafetyHook) |
| [toml++](https://github.com/marzer/tomlplusplus) | Config parsing | MIT | **Use** |
| [spdlog](https://github.com/gabime/spdlog) or the tiny custom logger | Logging | MIT | Start with custom logger (docs/02); switch only if needed |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | HTTP server for the remote inspector (D8) | MIT | **Use** in probe builds only |
| [nlohmann/json](https://github.com/nlohmann/json) or [simdjson](https://github.com/simdjson/simdjson) | JSON for probe export/inspector | MIT / Apache-2 | nlohmann for simplicity (probe only) |
| [Dear ImGui](https://github.com/ocornut/imgui) | Optional in-game overlay (D8 says later) | MIT | **Later**; has D3D11 backend |
| [FidelityFX-FSR (FSR 1)](https://github.com/GPUOpen-Effects/FidelityFX-FSR) | EASU + RCAS upscale/sharpen shaders (HLSL headers) | MIT | **Use** for M7. Keep upstream files untouched in `third_party/` |
| [FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) | Newer FSR versions (temporal) | MIT (verify per component) | **Research** only for M8+ spike; D3D11 support is limited |
| [xxHash](https://github.com/Cyan4973/xxHash) | Fast content hashing for texture/shader keys | BSD-2 | **Use** (`tex/content_hash`) |
| [DirectXTex](https://github.com/microsoft/DirectXTex) | DDS load/save, format conversion | MIT | **Evaluate** for `tex/dds` instead of a hand-written parser; check MinGW build |
| [Tracy](https://github.com/wolfpld/tracy) | CPU/GPU profiler; client in DLL, viewer on host | BSD-3 | **Use** in probe builds. Has a D3D11 GPU zone API |
| [Microsoft Detours](https://github.com/microsoft/Detours) | Hooking alternative | MIT | Reference only (MSVC-oriented) |

## 2. Test, quality and CI tooling

| Tool | Purpose | Decision |
|---|---|---|
| [doctest](https://github.com/doctest/doctest) | Unit tests | **Use** (`docs/05`) |
| [Google Benchmark](https://github.com/google/benchmark) / [nanobench](https://github.com/martinus/nanobench) | Microbenchmarks of pure code | **Use** one (nanobench is single header) |
| [libFuzzer](https://llvm.org/docs/LibFuzzer.html) / [OSS-Fuzz](https://github.com/google/oss-fuzz) | Fuzzing | **Use** libFuzzer in CI; OSS-Fuzz later |
| Sanitizers (ASan/UBSan) | Memory/UB bugs in host tests | **Use** |
| [clang-format](https://clang.llvm.org/docs/ClangFormat.html), [clang-tidy](https://clang.llvm.org/extra/clang-tidy/) | Style and static checks | **Use** |
| [cppcheck](https://cppcheck.sourceforge.io/) | Extra static analysis | **Use** |
| [CodeQL](https://codeql.github.com/) | Security analysis (free for public repos) | **Use** |
| [lcov](https://github.com/linux-test-project/lcov) / `llvm-cov` + [Codecov](https://about.codecov.io/) | Coverage | **Use** nightly |
| [ccache](https://ccache.dev/) | Faster rebuilds | **Use** |
| [just](https://github.com/casey/just) | Task runner (already chosen) | **Use** |
| [pre-commit](https://pre-commit.com/) | Local hooks: format, large-file and game-asset guard, secret scan | **Use** |
| [gitleaks](https://github.com/gitleaks/gitleaks) | Secret scanning in pre-commit/CI | **Use** |
| [markdownlint-cli2](https://github.com/DavidAnson/markdownlint-cli2), [lychee](https://github.com/lycheeverse/lychee) | Docs lint and link check | **Use** |
| [FLIP](https://github.com/NVlabs/flip) | Perceptual image diff for golden-image tests | **Evaluate** |
| [act](https://github.com/nektos/act) | Run Actions locally | Optional |
| [Dependabot](https://docs.github.com/en/code-security/dependabot) | Action version updates | **Use** |

## 3. Reverse engineering and debugging

| Tool | Purpose | Decision |
|---|---|---|
| [Ghidra](https://github.com/NationalSecurityAgency/ghidra) | Static analysis, decompiler, headless batch mode | **Use** (docs/03) |
| [x64dbg](https://github.com/x64dbg/x64dbg) | Dynamic debugging (under Wine) | **Use** |
| [Cutter / Rizin](https://github.com/rizinorg/cutter) | Alternative RE UI | Optional |
| [ImHex](https://github.com/WerWolv/ImHex) | Hex editor with pattern language for TPF/DCX/FXR | **Use** |
| [ReClass.NET](https://github.com/ReClassNET/ReClass.NET) | Struct recovery | **Use** |
| [PINCE](https://github.com/korcankaraokcu/PINCE) / Cheat Engine | Value scanning, "what writes this" | **Use** |
| [pe-sieve](https://github.com/hasherezade/pe-sieve) / [Scylla](https://github.com/NtQuery/Scylla) | Dump and fix PE images from memory | **Evaluate** for the unpacked-exe dump |
| [RenderDoc](https://github.com/baldurk/renderdoc) | GPU frame capture/analysis | **Use** |
| [apitrace](https://github.com/apitrace/apitrace) | Alternative D3D11 call tracing/replay | **Evaluate** (good for diffing runs) |
| [Special K](https://github.com/SpecialKO/SpecialK) | Large open-source D3D hooking/frame-pacing codebase | **Reference** for flip-model, limiter and swap-chain hooks |
| [ReShade](https://github.com/crosire/reshade) | Open-source D3D11 injection framework (BSD-3) | **Reference** for generic D3D11 hook patterns and depth-buffer heuristics; **disable it while testing** |
| [3DMigoto](https://github.com/bo3b/3Dmigoto) | Shader hashing/override model | **Reference** for T5 |
| [Wine / Proton / DXVK](https://github.com/doitsujin/dxvk) | Linux dev loop (`DXVK_HUD`, `DXVK_LOG_LEVEL`, state cache) | **Use**; remember Windows is the truth |

## 4. FromSoftware-specific ecosystem

| Tool | Purpose | Decision |
|---|---|---|
| [SoulsFormats](https://github.com/JKAnderson/SoulsFormats) and maintained forks (e.g. via [DSMapStudio](https://github.com/soulsmods/DSMapStudio), [Smithbox](https://github.com/vawser/Smithbox)) | C# parsers for DCX/BND/BHD/TPF/FXR/param | **Use** in `tools/bake` (D7). Check which fork supports DS3 FXR best and pin it |
| [Yabber](https://github.com/JKAnderson/Yabber) / [WitchyBND](https://github.com/ividyon/WitchyBND) | Unpack/repack archives | **Use** for manual inspection |
| [UXM Selective Unpack](https://github.com/Nordgaren/UXM-Selective-Unpack) | Unpack game archives to loose files | **Evaluate** (needed for loose-file overrides) |
| [ModEngine2](https://github.com/soulsmods/ModEngine2) | Asset override loader, DLL loader | **Optional route** (ADR D1); own proxy first |
| [DSMapStudio / Smithbox](https://github.com/vawser/Smithbox) | Param/map/text editor | **Use** for exploring params (DS3 supported) |
| [Souls Modding wiki](https://www.soulsmodding.com/) | Community docs for formats and tools | **Read first** |
| [Dark Souls 3 Cheat Engine tables / community RE notes](https://github.com/The-Grand-Archives) | Known structures/offsets | **Reference** (Grand Archives has CE tables and research for FromSoft games; verify build) |
| Existing FPS unlockers/fixes for DS3 and other FromSoft games (search GitHub: `DS3 fps unlock`, `Elden Ring FPS unlock`, `SoulsFPS`) | How others found the limiter and handled dt consumers | **Reference**; read the code and issues for what breaks (physics, rolling, durability, ladders/animations) |
| [DS3 `dinput8` / `dxgi` style proxy mods](https://github.com/topics/dark-souls-3) | Loader and conflict patterns | **Reference** |

> Keep a `docs/references.md` log of each repository studied: URL, commit read, what was learned, license (to avoid copying code with incompatible terms). Learning from open-source code is fine; copying requires license compatibility and attribution.

## 5. Measurement tools

| Tool | Purpose | Decision |
|---|---|---|
| [PresentMon](https://github.com/GameTechDev/PresentMon) | Independent frame-time and presentation-mode capture on Windows (MIT) | **Use** to validate our own telemetry |
| [CapFrameX](https://github.com/CXWorld/CapFrameX) | Analysis UI over PresentMon data | **Use** for human review |
| [MangoHud](https://github.com/flightlessmango/MangoHud) | Linux frame-time overlay and logging | **Use** in Proton loop |
| [gamescope](https://github.com/ValveSoftware/gamescope) | Controlled resolution/refresh for repeatable Linux tests | **Use** |
| [Tracy](https://github.com/wolfpld/tracy) | CPU timeline | **Use** (see §1) |
| pandas / matplotlib / [Polars](https://github.com/pola-rs/polars) | Analysis of JSONL captures | **Use** pandas; Polars if captures get big |
| Vendor tools (RGP, NVIDIA Nsight Graphics, Intel GPA) | GPU pass analysis on real hardware | **Use** per your GPU; not open source but free |

## 6. Docs and project infrastructure

| Tool | Purpose |
|---|---|
| [mdBook](https://github.com/rust-lang/mdBook) or [MkDocs Material](https://github.com/squidfunk/mkdocs-material) | Publish docs to GitHub Pages once the docs outgrow the repo view |
| [Mermaid](https://github.com/mermaid-js/mermaid) | Diagrams inside Markdown (GitHub renders them natively) |
| [Conventional Commits](https://www.conventionalcommits.org/) + [git-cliff](https://github.com/orhun/git-cliff) | Structured commits and changelog generation |
| [reuse](https://github.com/fsfe/reuse-tool) | License compliance linting (SPDX headers) |

## 7. Licensing rules for this repo

1. Our code: pick a permissive license (MIT or Apache-2.0) before the first external contribution. Add `LICENSE`.
2. Every dependency is added with its license recorded; no GPL/AGPL code linked into the DLL without a deliberate decision.
3. Don't vendor game files, dumps, or extracted assets (ever).
4. Pin dependencies by tag or commit hash (`cmake/deps.cmake` rule); record the hash in `THIRD_PARTY_LICENSES.txt`.
5. Reading code from GPL projects for understanding is fine; don't copy it.

## 8. Spike backlog from this list

| Spike | Question to answer | Milestone |
|---|---|---|
| S1 | Does SafetyHook build and work under MinGW-w64 GCC 14+? Is MinHook enough for everything through M6? | M0/M4 |
| S2 | Does DirectXTex cross-compile cleanly, or do we keep a tiny DDS reader? | M9 |
| S3 | Which SoulsFormats fork reads DS3 FXR and TPF correctly? | M9 |
| S4 | Can pe-sieve under Wine produce a Ghidra-ready dump of the unpacked DS3 exe? | M4 |
| S5 | Does Tracy's D3D11 GPU zone support give per-pass GPU time cheaply enough, versus our own timestamp queries? | M3 |
| S6 | What do Special K's flip-model conversion tricks require when the game uses MSAA? | M5 |
| S7 | How did existing FromSoft FPS unlockers deal with physics/animation dt consumers, and what regressions were reported? | M8 |
