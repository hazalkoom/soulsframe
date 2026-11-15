# Step 2 Report: Harsh CI Gates, Fault Isolation, Dependabot Resolution, and Learning Docs

Date: 2026-11-15
Branch progression: `ci/strict-linters-and-sonar` -> `test/harsh-stress-and-fuzz` -> `ci/harsh-pipeline-and-automerge` -> `docs/vibe-coder-masterclass`

## 1. What Was Built and Hardened

### A. Module Fault Isolation & Pure-C++ Static Analysis Cleanups

- Hardened [src/core/module_registry.cpp](../src/core/module_registry.cpp) and [src/core/module_registry.h](../src/core/module_registry.h) so `on_present()` and `shutdown_all()` catch unexpected exceptions per module, auto-disable only the throwing module, record the status failure, and let healthy modules and game rendering continue unaffected.
- Added CMake `SF_WERROR` option in [CMakeLists.txt](../CMakeLists.txt) and [tests/unit/CMakeLists.txt](../tests/unit/CMakeLists.txt) to treat warnings as errors in strict builds.
- Resolved 136 `clang-tidy` diagnostics (including `bugprone-implicit-widening-of-multiplication-result` in [src/core/sha256.cpp](../src/core/sha256.cpp) and C++20 `std::ranges` / `std::array` / designated-initializer cleanups across all 8 pure modules).
- Added shutdown hook telemetry (`Swapchain hooks removed (total_frames=...)`) in [src/gfx/d3d11/swapchain_hook.cpp](../src/gfx/d3d11/swapchain_hook.cpp).

### B. Expanded Unit & Stress Test Suite (23 Cases, 605 Assertions)

Expanded all 8 suites under [tests/unit/](../tests/unit/):

- [test_config.cpp](../tests/unit/test_config.cpp): wrong types for every field, negative/overflow clamping, embedded NUL-byte payload handling, 64 KB whitespace stress, missing file handling.
- [test_offline_guard.cpp](../tests/unit/test_offline_guard.cpp): mixed-case online CLI flags, precedence checks (`force_online_detected` vs opt-in), empty/benign CLI combinations.
- [test_sigscan.cpp](../tests/unit/test_sigscan.cpp): single/double wildcard equivalence, irregular whitespace, exact boundary matches at index `0` and end of a 64 KB buffer, oversized patterns, invalid hex tokens.
- [test_frame_stats.cpp](../tests/unit/test_frame_stats.cpp): 10,000-frame ring-buffer wrap-around stress test verifying old spike eviction and accurate `p1_low_ms`/`p0_1_low_ms`, clock backward jump protection, `NaN` handling, `reset()`.
- [test_sha256.cpp](../tests/unit/test_sha256.cpp): SHA-256 block padding boundaries (`55`, `56`, `64`, `65`, `128`), 1,000,000-byte NIST vector, 1-bit flip avalanche check.
- [test_content_hash.cpp](../tests/unit/test_content_hash.cpp): 1 MB payload hashing, seed sensitivity, 1-bit flip avalanche verification.
- [test_module_registry.cpp](../tests/unit/test_module_registry.cpp): null registration safety plus injected exceptions across `init()`, `on_present()`, and `shutdown()`.
- [test_profile.cpp](../tests/unit/test_profile.cpp): uppercase normalization and empty-hash safe fallback.

### C. Harsh CI Pipeline, Security Scanning, Multi-Scenario Wine Smoke & Auto-Merge

- Added [.markdownlint-cli2.jsonc](../.markdownlint-cli2.jsonc) and [.sonarcloud.properties](../.sonarcloud.properties), and removed `continue-on-error: true` from `docs` CI job.
- Expanded [scripts/ci/check-guards.sh](../scripts/ci/check-guards.sh) to enforce offline-default safety, zero DS3 RVA leakage outside `src/games/ds3/`, pinned `FetchContent` tags, `THIRD_PARTY_LICENSES.txt` completeness, and `///` header docstring coverage.
- Expanded [scripts/smoke-test.sh](../scripts/smoke-test.sh) into a 3-scenario headless Wine + Xvfb verification suite:
  1. Unacknowledged config blocks initialization (`OFFLINE GUARD BLOCKED MOD INIT`).
  2. Opt-in 300-frame run with `--resize` verifies `Present`, `ResizeBuffers`, frame telemetry, and clean shutdown (`total_frames=300`).
  3. Opt-in 150-frame run with `--msaa 4` verifies MSAA 4x swapchain hook stability (`total_frames=150`).
- Upgraded [.github/workflows/ci.yml](../.github/workflows/ci.yml) and [.github/workflows/release.yml](../.github/workflows/release.yml) with `actions/checkout@v7`, `actions/cache@v6`, `actions/upload-artifact@v7`, `DavidAnson/markdownlint-cli2-action@v25`, `gitleaks` secret scanning, `static-analysis` (`clang-format`, `clang-tidy`, `cppcheck`), `-DSF_WERROR=ON` across all builds, and `docker-verify`.
- Created [.github/workflows/codeql.yml](../.github/workflows/codeql.yml), [.github/workflows/auto-merge.yml](../.github/workflows/auto-merge.yml), and [.github/PULL_REQUEST_TEMPLATE.md](../.github/PULL_REQUEST_TEMPLATE.md), and enhanced [.github/copilot-instructions.md](../.github/copilot-instructions.md).
- Enabled GitHub repository `allow_auto_merge=true` and `delete_branch_on_merge=true`.

### D. Educational Documentation for Learners & Vibe Coders

- Created [docs/10-deep-dive-for-learners-and-vibe-coders.md](../docs/10-deep-dive-for-learners-and-vibe-coders.md) covering the game frame lifecycle, DLL proxying, COM vtables, MinHook trampolines, byte signature scanning, ring-buffer frame pacing, harsh CI architecture, contribution recipes, and career/skill leverage.
- Updated [README.md](../README.md), [docs/guide-for-vibe-coders.md](../docs/guide-for-vibe-coders.md), [docs/06-ci-cd-github-actions.md](../docs/06-ci-cd-github-actions.md), and [docs/09-codebase-reference.md](../docs/09-codebase-reference.md).

## 2. Local Verification Results

- `bash scripts/ci/check-guards.sh`: Passed (all 6 guard categories green).
- `cmake --preset host-tests -DSF_WERROR=ON && cmake --build --preset host-tests && ctest --preset host-tests --output-on-failure`: Passed (`23/23` test cases, `605/605` assertions, zero warnings under `-Werror`).
- `clang-format --dry-run --Werror` across all `src/` and `tests/` headers/sources (inside `sf-build`): Passed cleanly.
- `clang-tidy --warnings-as-errors='*'` across all 8 pure C++ implementation files (inside `sf-build`): Passed with exit code `0`.
- `win64-release` MinGW-w64 build (`-DSF_WERROR=ON`) + 3-scenario Wine + Xvfb smoke test inside `sf-wine-test`: Passed (`smoke-result.json` status `"ok"`).
