# Step 11: M4 dead-code/include audit, PGO prototype, and edge tests

Date: 2026-11-19
Branch: `feat/oc-tasks`

## M4 sync

Synced the `feat/oc-tasks` worktree with `origin/main` FF-only. The merge landed at
`cdbf4d1`, including `pe_dump`, `sig_db`, `win_dump`, `sftool`, Ghidra scripts, and M4 tests.

## Include/dependency audit

Audited headers and implementation units under:

- `src/core/`
- `src/hooks/`
- `src/probe/`
- `src/tex/`
- `src/timing/`
- `src/games/ds3/`

Findings:

- No confirmed orphaned runtime source file was found. Each source file belongs to a CMake
  runtime, host probe core, benchmark, or host tool target.
- Header includes are generally narrow and match their types; implementation-only standard
  headers remain in `.cpp` files where applicable.
- `sig_db.h` uses complete types only where public structs own strings/vectors/spans. No
  additional forward-declaration change is justified without changing public interfaces.
- M4 Win32 PE work remains isolated under `src/probe/` and no PE/signature implementation
  leaks Win32/D3D11 types into pure Linux headers.

## PGO prototype

Added `scripts/build-pgo.sh` with four stages:

1. Standard `host-bench` Release build.
2. Profile-generation build and fixed-iteration benchmark run.
3. Profile-use rebuild as `build/sf-bench-pgo`.
4. JSON comparison of median latency and throughput.

Extended `soulsframe_benchmarks` with `--iterations` to make training and PGO runs reproducible.
Added `tests/unit/test_pgo_script.cpp` to validate the script contract and invalid iteration count.

## Edge-case tests

Extended `test_sig_db.cpp` with missing fields, invalid hex pairs, and out-of-range/negative
numeric signature values. Added resolved-target safety checks for extreme offsets.

Extended `test_pe_dump.cpp` with short DOS buffers, truncated NT headers, corrupt `e_lfanew`
values, and truncated section tables.

## Efficiency documentation

Added `docs/14-codebase-trimming-and-efficiency.md`, covering release DLL footprint, isolated
test/tool targets, zero-cost C++20 abstractions, and PGO scope.

## Verification

- `bash scripts/ci/check-guards.sh`: pass.
- `cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests`: pass.
- PGO prototype: all four stages complete; a 1000-iteration run produced standard-vs-PGO JSON and
  a Markdown-ready comparison.
- `clang-format`/`markdownlint-cli2` availability on the native host was not present; CI/container
  equivalents should remain authoritative until binaries are installed or run through the build container.
