## Summary

<!-- Describe what this change does and why. -->

## Type of Change

- [ ] Feature (`feat:`)
- [ ] Bug fix (`fix:`)
- [ ] Tests (`test:`)
- [ ] CI / Build (`ci:` / `build:`)
- [ ] Documentation / Report (`docs:` / `report:`)

## Non-Negotiable Safety & Architecture Checklist

- [ ] `config/soulsframe.toml.example` still defaults `general.offline_acknowledged = false`
- [ ] Pure modules (`src/core/{config,offline_guard,module,module_registry,sha256}`, `src/hooks/sigscan`, `src/tex/content_hash`, `src/timing/frame_stats`, `src/games/ds3/profile`) do NOT include `<windows.h>`, `<d3d11.h>`, or `<dxgi.h>`
- [ ] Every new/updated public symbol has a `///` docstring explaining *what* and *why*
- [ ] Passes `bash scripts/ci/check-guards.sh`
- [ ] Passes `cmake --preset host-tests -DSF_WERROR=ON && cmake --build --preset host-tests && ctest --preset host-tests`
- [ ] Formatted with `clang-format`
