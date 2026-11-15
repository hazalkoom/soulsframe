# Copilot & AI Agent Instructions

Please follow all architectural rules, build commands, and workflow guidelines defined in [AGENTS.md](../AGENTS.md) and [docs/01-architecture-and-roadmap.md](../docs/01-architecture-and-roadmap.md).

## Mandatory Code & Review Checklist

1. **Pure-C++ Layering:** Keep `src/core/{config,offline_guard,module,module_registry,sha256}`, `src/hooks/sigscan`, `src/tex/content_hash`, `src/timing/frame_stats`, and `src/games/ds3/profile` completely free of `<windows.h>`, `<d3d11.h>`, and `<dxgi.h>`.
2. **Offline-Only Safety Gate:** Keep `general.offline_acknowledged = false` in config templates and preserve all checks inside `evaluate_offline_guard()`. Reject any change that bypasses or weakens offline checks.
3. **Offsets & D3D11 Isolation:** Never hardcode game RVAs outside `src/games/ds3/` or leak `ID3D11*` / `IDXGISwapChain*` types outside `src/gfx/d3d11/`.
4. **Module Fault Isolation:** Every feature must implement `sf::Module`, honor a kill switch in `is_enabled()`, and handle errors safely so `ModuleRegistry` keeps the game stable.
5. **Documentation & Reports:** Add `///` docstrings on all public structs, classes, functions, and test helpers, keep [docs/09-codebase-reference.md](../docs/09-codebase-reference.md) updated, and write milestone reports under `reports/`.
6. **Harsh Verification Before Merge:** Ensure `bash scripts/ci/check-guards.sh`, `clang-format --dry-run --Werror`, `cppcheck`, `clang-tidy`, `-DSF_WERROR=ON` unit tests (`tests/unit/`), MinGW static DLL checks, and multi-scenario Wine smoke tests all pass.
