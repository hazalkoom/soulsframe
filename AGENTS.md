# AGENTS.md — Context for AI Coding Agents

Welcome! This file gives AI assistants the rules, architecture boundaries, commands, and current status for **SoulsFrame**, a native C++20 `dinput8.dll` proxy mod for Dark Souls III (x64, D3D11) cross-compiled from Linux with MinGW-w64.

Read [README.md](README.md), [docs/01-architecture-and-roadmap.md](docs/01-architecture-and-roadmap.md), and [docs/09-codebase-reference.md](docs/09-codebase-reference.md) first if you are starting a fresh session. If orchestrating **OpenCode + Antigravity (AGY)** together, also read [docs/11-ai-agent-orchestration-opencode-agy.md](docs/11-ai-agent-orchestration-opencode-agy.md) and [docs/12-ai-agent-playbooks-and-roles.md](docs/12-ai-agent-playbooks-and-roles.md).

## Non-negotiable project rules

1. **Offline-only safety.** Never bypass, weaken, or default-enable `general.offline_acknowledged` in user-facing config templates (`config/soulsframe.toml.example`). `evaluate_offline_guard()` must continue blocking hooks when unacknowledged or when online markers are present.
2. **Never track game files or dumps.** Do not commit `*.exe`, `*.dll`, `*.bdt`, `*.bhd`, `*.dcx`, `*.tpf`, `*.bnd`, `*.fxr`, archives (`*.zip`), or files > 5 MB. Store reverse-engineering dumps only under `re/dumps/` (gitignored).
3. **Strict layering boundaries:**
   - **Pure modules** (`src/core/{config,offline_guard,module,module_registry,sha256}`, `src/hooks/sigscan`, `src/probe/{trace_types,jsonl_writer}`, `src/tex/content_hash`, `src/timing/frame_stats`, `src/games/ds3/profile`) **must NEVER include `<windows.h>`, `<d3d11.h>`, or `<dxgi.h>`**. They must compile and pass unit tests natively on Linux. Verify with `bash scripts/ci/check-guards.sh`.
   - **D3D11 isolation:** Keep `core/`, `hooks/`, `timing/`, `tex/` free of D3D11 types (`IDXGISwapChain*` stays `void*` in `PresentCtx`) so a future Elden Ring D3D12 backend can plug in without rewriting non-graphics layers.
   - **Offsets isolation:** Game-specific RVAs, byte signatures, and struct layouts belong strictly under `src/games/ds3/` and must be keyed by host executable SHA-256 (`GameProfile::engine_hooks_allowed`).
4. **Module isolation & kill switches:** Every feature implements `sf::Module`, checks a `Config` kill switch in `is_enabled()`, and fails independently inside `ModuleRegistry::init_all()` without crashing the game.
5. **Pinned dependencies only:** Add third-party deps in `cmake/deps.cmake` pinned by immutable Git tag or commit SHA (never a branch name), force `BUILD_SHARED_LIBS OFF`, and record licenses in `THIRD_PARTY_LICENSES.txt`.
6. **Document every symbol and explain *why*.** Keep `///` docstrings on structs/classes/functions updated and write a concise milestone/step report under `reports/` after meaningful work.

## Build, format, and test commands

- **Guards & layering check:** `bash scripts/ci/check-guards.sh`
- **Host unit tests (native Linux):**

  ```bash
  cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests
  ```

- **Host coverage gate (native Linux):**

  ```bash
  SF_MIN_LINE_COVERAGE=90 bash scripts/ci/run-coverage.sh
  ```

- **Build container images (first time):** `just img-build` (or `podman build -t sf-build -f docker/build.Dockerfile .`)
- **Cross-compile Windows x64 DLL (`build/win64-release/dinput8.dll`) & probe build:**

  ```bash
  podman run --rm --userns=keep-id -v "$PWD:/src:Z" -v sf-ccache:/ccache -w /src sf-build \
    bash -c "cmake --preset win64-release && cmake --build --preset win64-release"
  ```

- **Verify static DLL imports/exports:**
  Run `x86_64-w64-mingw32-objdump -p build/win64-release/dinput8.dll`: must export `DirectInput8Create` and must not dynamically import `libstdc++`, `libgcc_s`, `libwinpthread`, or `minhook.x64.dll`.
- **Format C++ code:** Run `clang-format -i` (using `.clang-format`) before committing.
- **Wine smoke test:** `just smoke` (runs `scripts/smoke-test.sh` inside `sf-wine-test`).

## Roadmap status (where we are)

- **M0 (Toolchain + containers) & M1 (First light skeleton):** **Implemented** — pure C++ core, `dinput8.dll` proxy + `Present`/`ResizeBuffers` MinHook hooks, container Dockerfiles, D3D11 testbed, CI/CodeQL/security workflows.
- **M2 (`sf-probe` v1 draw-call tracer):** **Implemented** — pure `TraceRecorder` & JSONL serializer, `ID3D11DeviceContext` vtable hooks (7 slots, `SF_PROBE=1`), async `ProbeModule`, `[probe]` config kill switches, `gcovr` coverage gate (`host-cov`), 3 `libFuzzer` targets, 4-scenario Wine smoke suite (`--particles`), and extended `analyze_capture.py`.
- **M3 (GPU pass timing + benchmarks):** **Implemented** — pure `gpu_timing`/`GpuFrameJoiner` (Linux-tested), `GpuTimer` D3D11 timestamp queries wired into the probe (`probe.gpu_timing` kill switch, `gpu_ms` JSONL fields), Release `host-bench` micro-benchmark suite + CI job, analyzer GPU stats.
- **Next milestones:** M4 (Ghidra memory dump + signature DB), M5 (presentation/flip-model experiments), M6 (low-res particles), M7 (FSR 1), M8 (FPS unlock research).

## Commit & documentation workflow

- Commit and push frequently in small, focused steps using Conventional Commit prefixes (`feat:`, `fix:`, `test:`, `build:`, `ci:`, `docs:`, `report:`).
- Update [docs/09-codebase-reference.md](docs/09-codebase-reference.md) when adding modules or changing execution flow.

## Multi-agent orchestration (OpenCode + Antigravity / AGY)

- **Workspace separation:** Run Antigravity in the primary checkout and OpenCode in an isolated sibling worktree (`git worktree add ../soulsframe-oc -b feat/oc-work origin/main`) so builds and edits never collide.
- **Suggested split:** OpenCode owns fast Linux pure-C++ modules, `doctest` unit tests, `libFuzzer` targets, `gcovr` loops, and `tools/py/`; Antigravity owns architecture/coordination, `src/gfx/d3d11/` hooks, `sf-build` MinGW cross-compiles, `sf-wine-test` runs, CI triage, and merges.
- **Static-analysis defaults:** Always use `std::array` (never raw C arrays), designated initializers on aggregates, `{buf.data()}` braced returns for `std::string`, `std::cmp_less`/`std::cmp_greater`, and `std::ranges::transform` so `clang-tidy` and `cppcheck` stay 100% green.
- **Full playbooks & copy-paste handoffs:** See [docs/11-ai-agent-orchestration-opencode-agy.md](docs/11-ai-agent-orchestration-opencode-agy.md) and [docs/12-ai-agent-playbooks-and-roles.md](docs/12-ai-agent-playbooks-and-roles.md).
