# 12. AI Agent Specialist Playbooks, Quality Gates & Traps Avoided

This manual equips every AI agent (**OpenCode**, **Antigravity subagents**, and **Copilot**) with role-specific checklists, exact static-analysis rules enforced by CI, and **real C++/Windows/Wine bugs already discovered and fixed** in SoulsFrame so agents never repeat them.

---

## 1. Specialist Agent Roles & Exact Scopes

When splitting work across subagents or OpenCode sessions, assign each agent **one** explicit role:

### Role A: Pure-C++ Systems Author

- **Scope:** `src/core/{config,offline_guard,module,module_registry,sha256}`, `src/hooks/sigscan`, `src/probe/{trace_types,jsonl_writer}`, `src/tex/content_hash`, `src/timing/frame_stats`, `src/games/ds3/profile`, `config/soulsframe.toml.example`.
- **Must satisfy on every commit:**
  - Zero `<windows.h>`, `<d3d11.h>`, `<dxgi.h>`, `ID3D*`, or `HWND` references (checked by [scripts/ci/check-guards.sh](../scripts/ci/check-guards.sh)).
  - Must pass `clang-tidy --warnings-as-errors='*'` ([.clang-tidy](../.clang-tidy)) AND `cppcheck --enable=warning,style,performance,portability --error-exitcode=1`!
  - Line coverage >= 90% ([scripts/ci/run-coverage.sh](../scripts/ci/run-coverage.sh)).

### Role B: Windows & D3D11 Hook Engineer

- **Scope:** `src/core/{dllmain,log,proxy_dinput8,win_helpers}.cpp`, `src/gfx/d3d11/*`, `src/probe/probe_module.*`, `CMakeLists.txt` (Win64 targets), `tests/testbed/d3d11_testbed.cpp`.
- **Must satisfy on every commit:**
  - Probe/RE features compile **only** inside `if(SF_PROBE)` / `#ifdef SF_PROBE`, leaving `win64-release` clean.
  - Every `OMGet*`, `GetResource`, `QueryInterface`, `PSGetShader`, `VSGetShader` call releases its COM pointer before returning.
  - Must build with ZERO warnings on MinGW-w64 `-Wall -Wextra -Wpedantic -Werror` in **both** `win64-release` and `win64-probe`.
  - Both DLLs must export `DirectInput8Create`, enforce `HIGH_ENTROPY_VA`, `DYNAMIC_BASE`, `NX_COMPAT`, and have zero dynamic imports (`libstdc++`, `libgcc_s`, `libwinpthread`, `minhook`).

### Role C: Verification, Coverage & Fuzzing Specialist

- **Scope:** `tests/unit/*`, `tests/fuzz/*`, `scripts/ci/*`, `scripts/smoke-test.sh`.
- **Must satisfy on every commit:**
  - Add adversarial tests for malformed types, clamping bounds, NUL bytes, empty/null inputs, and resource leaks.
  - Exercise any new parser or serializer via a `LLVMFuzzerTestOneInput` harness and seed corpus in `tests/fuzz/`.
  - Keep headless Wine `d3d11_testbed.exe` deterministic and free of interactive dialogs/hangs.

### Role D: Telemetry & Reverse-Engineering Python Analyst

- **Scope:** `tools/py/*`, `docs/08-logging-and-telemetry.md`.
- **Must satisfy on every commit:**
  - Keep `tools/py/analyze_capture.py` stdlib-runnable with an accurate `--self-test` suite (run by `security.yml`).
  - Keep any new `pip` dependencies exact-pinned (`pkg==version`) in [tools/py/requirements.txt](../tools/py/requirements.txt).

### Role E: Docs, Markdownlint & AI Context Maintainer

- **Scope:** `README.md`, `AGENTS.md`, `.github/copilot-instructions.md`, `docs/*`, `reports/*`.
- **Must satisfy on every commit:**
  - Every fenced code block has an explicit language tag (`bash`, `cpp`, `text`, `mermaid`, etc.), blank lines surround headings/lists/fences, and no duplicate sibling headings exist ([.markdownlint-cli2.jsonc](../.markdownlint-cli2.jsonc)).
  - Every number reported in `reports/` or `docs/09-codebase-reference.md` matches the latest verified tool run (`doctest` cases/assertions, `gcovr` line coverage, CI job status).

---

## 2. CI Rules That Surprise AI Agents Most Often

Check this list **before** pushing so `static-analysis` and `fuzz-smoke` pass on the very first run:

| CI Check | Common Agent Mistake | Exact Required Fix |
|---|---|---|
| `clang-tidy` (`modernize-avoid-c-arrays`) | Writing `char buf[256];` or `uint8_t arr[16];` in pure C++ modules | Use `std::array<char, 256> buf{};` and `buf.data()`, `buf.size()` |
| `clang-tidy` (`modernize-use-designated-initializers`) | Positional aggregate initialization like `CallSiteKey{a, b, c}` | Use C++20 designated initializers: `CallSiteKey{.caller_rva = a, .ps_hash = b, ...}` |
| `clang-tidy` (`modernize-return-braced-init-list`) | `return std::string(buf.data());` inside functions returning `std::string` | Use `return {buf.data()};` |
| `clang-tidy` (`modernize-use-integer-sign-comparison`) | Casting `int64_t < static_cast<int64_t>(uint32_t)` | Include `<utility>` and use `std::cmp_less(a, b)` / `std::cmp_greater(a, b)` |
| `cppcheck` (`useStlAlgorithm`) | Writing `for (const auto& x : src) dst.push_back(f(x));` | Use `std::ranges::transform(src, std::back_inserter(dst), ...);` |
| `check-guards.sh` (Docstrings) | Adding a new `struct`, `class`, or public function in `src/**/*.h` without `///` | Always place a `///` docstring immediately above every new public symbol |
| `markdownlint-cli2` | Leaving ` ``` ` without a language or missing blank lines before tables/lists | Always tag ` ```text ` / ` ```bash ` and keep 1 blank line around blocks |

---

## 3. Real Engineering Traps Caught & Fixed in M0–M2 (Do Not Reintroduce!)

Every item below was a real failure caught by SoulsFrame's compiler, fuzzer, or Wine tests:

1. **`<windows.h>` `#define DrawState DrawStateA` Macro Collision:**
   - **What happened:** Pure C++ struct `sf::probe::DrawState` compiled cleanly on Linux, then failed under MinGW `x86_64-w64-mingw32-g++` inside `context_hook.cpp` because `<winuser.h>` defines `DrawState` as a preprocessor macro!
   - **Rule:** Never name pure structs/functions after Win32 `USER32`/`GDI32` symbols (`DrawState`, `GetObject`, `CreateEvent`, `min`, `max`). Our tracer struct is intentionally named `DrawCallState`.

2. **Windows `DllMain(DLL_PROCESS_DETACH)` + `std::thread::join()` Deadlock:**
   - **What happened:** Joining `ProbeModule`'s background JSONL `writer_thread_` unconditionally inside `shutdown()` (which runs during `FreeLibrary` / process teardown under `DLL_PROCESS_DETACH`) deadlocked under the **OS Loader Lock** because the exiting worker thread waits on the loader lock for `DLL_THREAD_DETACH` while `DllMain` holds that same lock.
   - **Rule:** Join worker threads **outside** `DllMain` (e.g., on `capture_frames` completion inside `Present()`), and during `shutdown()` under `DLL_PROCESS_DETACH`, synchronize flush completion via an atomic (`writer_done_`) + short bounded wait, then `detach()` cleanly.

3. **MinHook Multiple-Module Lifecycle Ordering:**
   - **What happened:** `swapchain_hook.cpp` calls `MH_Initialize()` first. Subsequent hook installers (`context_hook.cpp`) must handle `MH_Initialize()` returning `MH_ERROR_ALREADY_INITIALIZED`, track ownership (`g_owns_mh_init`), enable per target (`MH_EnableHook(slot.target)`), and remove only their own hook targets cleanly.
   - **Rule:** Never blindly call `MH_Uninitialize()` from a secondary module when MinHook was already initialized by `swapchain_hook`.

4. **`toml++ v3.4.0` Debug Assertion in `fuzz_config` (`[%...]`):**
   - **What happened:** `libFuzzer` + ASan/UBSan triggered an internal `TOML_ASSERT_ASSUME` inside `tomlplusplus` v3.4.0 (`parser.inl:3037`) in Debug builds when given `[%feneral]` or embedded NUL bytes in table headers before reaching its own parse-error branch.
   - **Rule:** Keep embedded-NUL rejection AND `#define TOML_ASSERT(expr) static_assert(true)` above `#include <toml++/toml.hpp>` in [src/core/config.cpp](../src/core/config.cpp).

5. **Wine `wined3d` Fixed-Function Crash on Shaderless `Draw()`:**
   - **What happened:** Calling `ID3D11DeviceContext::Draw()` in `d3d11_testbed.exe` without bound Vertex/Pixel Shaders caused Wine `wined3d`'s command stream thread to fall back to `wined3d_ffp_get_vs_settings` and dereference null (`+0x2C`).
   - **Rule:** Always bind valid VS, PS, Vertex Buffer, Input Layout, Viewport, and Primitive Topology in `tests/testbed/d3d11_testbed.cpp` before issuing draw calls.

---

## 4. "Before You Say Done" Mandatory Agent Verification Matrix

Only state test counts, line coverage, or build status that you **directly ran and observed** after your final edit:

| Layer | Command | Pass Condition |
|---|---|---|
| L0 Repo Guards | `bash scripts/ci/check-guards.sh` | `All repository guards passed.` |
| L0 Python Tooling | `python3 tools/py/analyze_capture.py --self-test` | `analyze_capture.py self-test OK` |
| L1 Unit Tests + Coverage | `SF_MIN_LINE_COVERAGE=90 bash scripts/ci/run-coverage.sh` | `100% tests passed` & line coverage >= 90% |
| L1 Format & Build (`sf-build`) | `clang-format --dry-run --Werror ...` + `win64-release` & `win64-probe` (`-DSF_WERROR=ON`) | Exit `0`, zero warnings |
| L1 PE Audit (`sf-build`) | `x86_64-w64-mingw32-objdump -p build/*/dinput8.dll` | Exports `DirectInput8Create`; shows `HIGH_ENTROPY_VA`, `DYNAMIC_BASE`, `NX_COMPAT`; zero dynamic runtime DLLs |
| L4 Wine Smoke (`sf-wine-test`) | `bash scripts/smoke-test.sh /src/build/win64-release /src/build/win64-probe` | `smoke OK (probe=true)` across all 4 scenarios |
| CI Remote Verification | `gh run watch <run-id> --exit-status` | All workflows (`ci`, `security`, `codeql`) `success` |
