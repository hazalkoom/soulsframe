# Copilot & AI Agent Instructions

Please follow all architectural rules, build commands, and multi-agent handoff protocols defined in [AGENTS.md](../AGENTS.md), [docs/01-architecture-and-roadmap.md](../docs/01-architecture-and-roadmap.md), [docs/11-ai-agent-orchestration-opencode-agy.md](../docs/11-ai-agent-orchestration-opencode-agy.md), and [docs/12-ai-agent-playbooks-and-roles.md](../docs/12-ai-agent-playbooks-and-roles.md).

## Mandatory Code & Review Checklist

1. **Pure-C++ Layering:** Keep `src/core/{config,offline_guard,module,module_registry,sha256}`, `src/hooks/sigscan`, `src/probe/{trace_types,jsonl_writer}`, `src/tex/content_hash`, `src/timing/frame_stats`, and `src/games/ds3/profile` completely free of `<windows.h>`, `<d3d11.h>`, and `<dxgi.h>`.
2. **Offline-Only Safety Gate:** Keep `general.offline_acknowledged = false` in config templates and preserve all checks inside `evaluate_offline_guard()`. Reject any change that bypasses or weakens offline checks.
3. **Offsets & D3D11 Isolation:** Never hardcode game RVAs outside `src/games/ds3/` or leak `ID3D11*` / `IDXGISwapChain*` types outside `src/gfx/d3d11/`.
4. **Module Fault Isolation & Loader-Lock Safety:** Every feature must implement `sf::Module`, honor a kill switch in `is_enabled()`, handle errors safely, and never call `std::thread::join()` inside `DllMain(DLL_PROCESS_DETACH)`.
5. **Documentation & Reports:** Add `///` docstrings on all public structs, classes, functions, and test helpers, keep [docs/09-codebase-reference.md](../docs/09-codebase-reference.md) updated, and write milestone reports under `reports/`.
6. **Harsh Verification Before Merge:** Ensure `bash scripts/ci/check-guards.sh`, `clang-format --dry-run --Werror`, `cppcheck`, `clang-tidy`, `-DSF_WERROR=ON` unit tests (`tests/unit/`), `gcovr` coverage (`>= 90%`), 3 `libFuzzer` targets (`tests/fuzz/`), MinGW static DLL checks (`release` + `probe`), and 4-scenario Wine smoke tests all pass.
