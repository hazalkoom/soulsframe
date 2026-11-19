# 14. Codebase trimming and efficiency (vibe-coder guide)

## 1. Release binary vs source repository

The Windows release artifacts are small compared with the complete source and developer repository:

| Item | Typical size / scale | Why |
|---|---|---|
| `dinput8.dll` release | ~250 KB | Small proxy DLL plus pure C++ modules and D3D11 hooks |
| Unit, fuzz, benchmark, and RE tools | Repository-only | Built into host executables or development tools, not linked into the game DLL |
| Source tree | Thousands of lines across runtime + tests + tools | Tests and developer tooling improve correctness and reverse engineering without inflating runtime |

A source line count is not a direct predictor of DLL size. Compilation includes only translation units and libraries reachable from the release target. Test executables, Python utilities, Ghidra scripts, benchmark harnesses, fuzz seeds, and documentation are not copied into `dinput8.dll`.

## 2. What stays out of the release DLL

The release build uses explicit CMake targets and source lists:

- `soulsframe_core` / `soulsframe_probe_core` contain runtime/research logic selected by the build.
- `soulsframe_unit_tests` contains doctest cases and never links into `dinput8.dll`.
- `soulsframe_benchmarks` contains the Release micro-benchmark harness.
- `fuzz_*` targets are compiled only for fuzzer runs.
- `tools/cpp/sftool` is a host RE utility.
- Ghidra Java scripts, Python plots, Markdown reports, and dumps remain developer artifacts.

The probe build (`SF_PROBE=ON`) is separate from the normal release build. Heavy capture/serialization code is not silently pulled into every runtime image unless the target is configured for it.

## 3. Zero-cost abstractions

SoulsFrame relies on C++20 constructs that describe intent without retaining an object at runtime:

- `std::span` describes a non-owning byte range. It replaces pointer-plus-length pairs and allows bounds checks while avoiding copies.
- `std::string_view` describes non-owning text slices, which avoids unnecessary string allocations during parsing and pattern lookup.
- `constexpr` hashing and table calculations move work to compile time where possible. Runtime hashing still uses the pinned xxHash/SHA-256 implementations only for the files that require it.
- Small enums and POD structs keep hot data compact and easy to pass by value.
- Fixed-size ring buffers and bounded vectors prevent unbounded capture growth.

These abstractions do not automatically guarantee speed. The project uses the benchmark harness to verify the result, rather than assuming a language feature is fast.

## 4. Forward declarations and include boundaries

Headers prefer forward declarations and narrow standard-library includes when a complete type is not required. This reduces work in dependent translation units while keeping public APIs visible.

The key boundaries are:

- Core, timing, hash, signature matching, and JSONL logic stay free of Win32/D3D11 types.
- D3D11 types remain in `src/gfx/d3d11/` and Win32 dump/hooks in their own runtime modules.
- Game-specific offsets/signatures stay under `src/games/ds3/` and are keyed by executable SHA-256.
- A header includes the minimum types needed by that header; implementation-only dependencies stay in `.cpp` files.

## 5. Dead-code and orphan policy

Every source file must have a linked target or a clear developer-tool purpose. When a helper is no longer called, it should be removed rather than retained “just in case.” The CI guards check layering, tracking, security defaults, and pinned dependencies, while static analysis catches unneeded paths and warning issues.

Release efficiency comes from linking a narrow target, not from deleting unit tests or developer tools. Tests and RE utilities are intentionally isolated because they improve confidence and reduce reverse-engineering risk without being shipped.

## 6. PGO prototype

`scripts/build-pgo.sh` is an optional native Linux experiment. It:

1. Builds the standard `host-bench` Release binary.
2. Rebuilds the benchmark harness with profile-generation flags.
3. Runs fixed-iteration training workloads to generate GCC `.gcda` counters or Clang profile data.
4. Rebuilds with profile-use and compares median latency and throughput with standard Release.

PGO is not enabled in normal Windows/game builds. The prototype is measured against the dedicated benchmark harness and should only be promoted when repeated runs show a stable, meaningful improvement.
