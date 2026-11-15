# Copilot / AI Agent Instructions

Please follow all architectural rules, build commands, and workflow guidelines defined in [AGENTS.md](../AGENTS.md) and [docs/01-architecture-and-roadmap.md](../docs/01-architecture-and-roadmap.md).

Quick checklist before opening or modifying code:
1. Keep pure C++ modules (`src/core/{config,offline_guard,module,module_registry,sha256}`, `src/hooks/sigscan`, `src/tex/content_hash`, `src/timing/frame_stats`, `src/games/ds3/profile`) free of `<windows.h>`, `<d3d11.h>`, and `<dxgi.h>`.
2. Keep `general.offline_acknowledged = false` in config templates and preserve the `evaluate_offline_guard()` gate.
3. Run `bash scripts/ci/check-guards.sh`, `clang-format`, `host-tests`, and MinGW cross-compilation checks after edits.
4. Add or update `///` docstrings on all public APIs and add doctest unit tests under `tests/unit/` for new logic.
