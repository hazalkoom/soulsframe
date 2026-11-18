# Step 10: Milestone 4 — RE Foundation (PE Dump, Sig DB, `sftool`, Ghidra Scripts)

**Branch:** `feat/m4-re-foundation`  
**Scope:** Builds the complete, offline-safe reverse-engineering foundation so runtime DS3 memory dumps can be captured cleanly, aligned for Ghidra, queried via `sftool`, and pinned by executable SHA-256 in `re/db/ds3_sigs.toml`.

## 1. What was added

- **Pure PE64 Parser & Ghidra Unaligned Fixup:**
  - [src/probe/pe_dump.h](../src/probe/pe_dump.h) and [src/probe/pe_dump.cpp](../src/probe/pe_dump.cpp) implement bounds-checked PE32+ header inspection (`inspect_pe64`) and section raw-to-virtual alignment (`fixup_pe_dump_for_ghidra`) with zero `<windows.h>` dependency.
  - Tested in [tests/unit/test_pe_dump.cpp](../tests/unit/test_pe_dump.cpp).
- **SHA-256 Keyed Signature DB & x64 RIP-Relative Resolver:**
  - [src/games/ds3/sig_db.h](../src/games/ds3/sig_db.h) and [src/games/ds3/sig_db.cpp](../src/games/ds3/sig_db.cpp) parse `ds3_sigs.toml`, validate byte patterns via `sf::hooks::parse_pattern()`, resolve `[rip + disp32]` globals (`resolve_signature_target`), and wire verified build lookup into [src/games/ds3/profile.cpp](../src/games/ds3/profile.cpp).
  - Unknown or unverified builds strictly leave `engine_hooks_allowed = false`.
  - Schema and initial files live in [re/db/ds3_sigs.toml](../re/db/ds3_sigs.toml) and [re/db/builds.json](../re/db/builds.json).
  - Tested in [tests/unit/test_sig_db.cpp](../tests/unit/test_sig_db.cpp).
- **Read-Only Win32 Runtime Memory Dumper (`SF_PROBE=1`):**
  - [src/probe/win_dump.h](../src/probe/win_dump.h) and [src/probe/win_dump.cpp](../src/probe/win_dump.cpp) safely walk readable committed virtual memory via `VirtualQuery` (never altering memory protection or writing game memory), align sections with `fixup_pe_dump_for_ghidra()`, and write `probe.dump_file` when `probe.dump_module = true` (off by default).
  - Added `probe.dump_module` and `probe.dump_file` to [src/core/config.h](../src/core/config.h), [src/core/config.cpp](../src/core/config.cpp), and [config/soulsframe.toml.example](../config/soulsframe.toml.example).
  - Verified end-to-end inside Wine against `d3d11_testbed.exe` in [scripts/smoke-test.sh](../scripts/smoke-test.sh) (`ProbeModule: MemDump ok`).
- **Native Host CLI (`sftool`) & Headless Ghidra Export:**
  - [tools/cpp/sftool_main.cpp](../tools/cpp/sftool_main.cpp) provides `builds hash`, `pe-info`, `pe-fixup`, `sigmaker`, and `sigtest`.
  - [re/ghidra_scripts/ExportSymbols.java](../re/ghidra_scripts/ExportSymbols.java) exports Ghidra function RVAs to JSON.

## 2. Verification

- `bash scripts/ci/check-guards.sh`: Passed.
- `host-tests` (`-DSF_WERROR=ON`): Unit tests + `sftool_help_smoke` passed (`58` test cases, `943` assertions in `soulsframe_unit_tests` at time of M4 commit, plus OpenCode's merged stress assertions).
- `win64-probe` & `win64-release` (`-DSF_WERROR=ON`): Cleanly cross-compiled.
- Wine smoke suite (`sf-wine-test`): Passed all 4 scenarios including `ProbeModule: MemDump ok` (`soulsframe_mem_dump.exe` verified non-empty).
- Exercised `sftool builds hash`, `sftool pe-info`, `sftool sigmaker`, and `sftool sigtest` directly against `build/win64-release/d3d11_testbed.exe`.
