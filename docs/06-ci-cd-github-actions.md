# 06. CI/CD with GitHub Actions

Goal: every push proves the DLL still cross-compiles, pure code still passes tests, the DLL still loads under Wine, and tagged releases publish a verified artifact. CI machines have no DS3 and no useful GPU, so CI covers layers L0 to L4 of `docs/05`. Nothing in CI ever touches game files.

> Workflow files live in `.github/workflows/`. A working first version (`ci.yml`) is already committed. Pin third-party actions to a major version at minimum; pin to a commit SHA once the project has releases (supply-chain hygiene).

## 1. Workflow map

| Workflow | Trigger | Jobs | Time budget |
|---|---|---|---|
| `ci.yml` | push (`main`, `develop`, `feat/**`, `fix/**`, `test/**`, `docs/**`, `ci/**`), PR | `guards` (+`gitleaks`) → `static-analysis` (`clang-format`, `clang-tidy`, `cppcheck`), `host-tests` (`g++`/`clang++`/`ASan` with `SF_WERROR`), `win64` (`release`+`probe` + PE hardening check), `docker-verify` → `wine-smoke` (3 scenarios) | < 12 min |
| `security.yml` | push (`main`, `develop`, feature branches), PR, weekly cron | `dependency-review` (PR moderate+ CVE/license block), `supply-chain-audit` (guards, HTTPS pins, workflow `permissions:`, `analyze_capture.py --self-test`, `gitleaks`) | < 5 min |
| `codeql.yml` | push (`main`, `develop`), PR, weekly cron | CodeQL C/C++ (`security-extended`) | < 20 min |
| `auto-merge.yml` | `pull_request_target` | Approves Dependabot & repo branch PRs and enables `--auto --squash` once all required checks succeed | < 1 min |
| `release.yml` | tag `v*.*.*` | rebuild from tag (`SF_WERROR=ON`), smoke test, `SHA256SUMS`, provenance attest, GitHub Release | < 20 min |
| `nightly.yml` | cron (03:00 UTC), manual | long fuzz, coverage, microbenchmarks, shader golden images, dependency drift check (planned) | < 60 min |

## 2. `ci.yml` job design

### lint

- `clang-format --dry-run --Werror` on changed files.
- `cppcheck` with `--enable=warning,performance,portability --error-exitcode=1`.
- `clang-tidy` only on host-compilable code (via `host-tests` compile database), not on Win32 glue.
- Guard script `scripts/ci/check-no-game-files.sh`: fails on `*.exe`, `*.dll`, `*.bdt`, `*.bhd`, `*.dcx`, `*.tpf`, `*.bnd`, `*.fxr` or files larger than 5 MB being added (except allow-listed test fixtures). Backs up the pre-commit hook.
- Guard script: fails if pure modules include `<windows.h>`.

### host-tests

- Matrix: `{gcc, clang} × {Debug, ASan+UBSan}` on `ubuntu-latest`.
- `cmake --preset host-tests && ctest --preset host-tests`.
- Upload `Testing/` logs on failure.

### win64

- Runs in the same container image as local dev (`docker/build.Dockerfile`), so CI and laptop match. Build the image with `docker/build-push-action` and cache layers in the GitHub cache (`cache-from: type=gha`).
- ccache state in `actions/cache` keyed by compiler version + hash of `CMakeLists.txt` and `cmake/deps.cmake`.
- Builds both `win64-release` and `win64-probe`.
- Verifies the output: `x86_64-w64-mingw32-objdump -p dinput8.dll` must export `DirectInput8Create`, enforce PE mitigations (`HIGH_ENTROPY_VA`, `DYNAMIC_BASE`, `NX_COMPAT`), and must not import unexpected DLLs (allow-list: `KERNEL32`, `USER32`, `D3D11`, `DXGI`, `D3DCOMPILER_47`, `ntdll`, `msvcrt`). A static-runtime build should have no `libstdc++-6.dll` or `libgcc_s_seh-1.dll` import.
- Uploads `dinput8.dll` (release and probe) plus `.pdb`-equivalent debug info (`.debug` split via `objcopy --only-keep-debug`) as artifacts.

### wine-smoke

- Needs `win64`. Runs `docker/wine-test.Dockerfile` with `scripts/smoke-test.sh`.
- Runs all testbed modes from `docs/05` §5, asserts on `smoke-result.json`.
- Uses `xvfb-run` and a hard timeout. Failure uploads `soulsframe.log` and the Wine log (`WINEDEBUG=+loaddll,err`).
- Marked `continue-on-error: true` for the first two weeks while its flakiness is measured, then made required.

## 3. Starter `ci.yml`

This is the file committed at `.github/workflows/ci.yml`. It is intentionally conservative: it works before the source tree exists by skipping jobs whose inputs are missing, so the repo stays green during planning.

```yaml
name: ci
on:
  push: { branches: [main] }
  pull_request:
concurrency:
  group: ci-${{ github.ref }}
  cancel-in-progress: true
permissions:
  contents: read

jobs:
  guards:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: No game files or huge blobs
        run: |
          bad=$(git ls-files | grep -Ei '\.(exe|dll|bdt|bhd|dcx|tpf|bnd|fxr|zip)$' || true)
          [ -z "$bad" ] || { echo "Forbidden files tracked:"; echo "$bad"; exit 1; }
          big=$(git ls-files -z | xargs -0 du -k | awk '$1 > 5120 {print $2}')
          [ -z "$big" ] || { echo "Files over 5 MB:"; echo "$big"; exit 1; }

  host-tests:
    needs: guards
    if: hashFiles('CMakePresets.json') != ''
    runs-on: ubuntu-latest
    strategy:
      matrix:
        cxx: [g++, clang++]
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y ninja-build cmake clang
      - run: cmake --preset host-tests -DCMAKE_CXX_COMPILER=${{ matrix.cxx }}
      - run: cmake --build --preset host-tests
      - run: ctest --preset host-tests

  win64:
    needs: guards
    if: hashFiles('CMakePresets.json') != ''
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y ninja-build cmake g++-mingw-w64-x86-64 ccache
      - uses: actions/cache@v4
        with:
          path: ~/.cache/ccache
          key: ccache-win64-${{ hashFiles('CMakeLists.txt', 'cmake/**') }}
          restore-keys: ccache-win64-
      - run: cmake --preset win64-release -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
      - run: cmake --build --preset win64-release
      - uses: actions/upload-artifact@v4
        with:
          name: dinput8-win64
          path: build/win64-release/dinput8.dll

  docs:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: DavidAnson/markdownlint-cli2-action@v16
        continue-on-error: true
        with:
          globs: "**/*.md"
```

Note: CI uses Ubuntu's `g++-mingw-w64-x86-64` while local dev uses Fedora's MinGW. Version differences are a known risk; the container-based job (above) removes it. Switch to the container job once `docker/build.Dockerfile` exists.

## 4. `nightly.yml`

| Job | Does | Fails when |
|---|---|---|
| fuzz | 10 min per libFuzzer target, corpus cached | Any crash/timeout/OOM; artifact holds the reproducer |
| coverage | `llvm-cov` on host tests, upload to Codecov | Never (informational) |
| bench | Google Benchmark on pure modules, compare against last `main` result stored via `github-action-benchmark` | > 15% regression |
| shader-golden | Wine testbed renders FSR1 on fixture images, diff vs references | Diff above tolerance |
| deps | Script checks each pinned dependency in `cmake/deps.cmake` for new tags and opens an issue | Never |

## 5. `release.yml`

1. Trigger on tag `vX.Y.Z`. Verify the tag is on `main`.
2. Rebuild `win64-release` from scratch (no ccache), run wine-smoke.
3. Package `soulsframe-vX.Y.Z-win64.zip`:
   - `dinput8.dll`, `soulsframe.toml` (default config, offline-only opt-in off), `README.txt`, `LICENSE`, `THIRD_PARTY_LICENSES.txt`.
   - Never the probe build in the default zip; probe ships as a separate `-probe` asset.
4. Generate `SHA256SUMS` and a build provenance attestation with `actions/attest-build-provenance`. Users can then verify the DLL was built by this repo's workflow, which matters a lot for a DLL people drop into a game folder.
5. Create the GitHub Release with notes generated from commits (`gh release create --generate-notes`), marked pre-release until M10.
6. Release notes template includes the offline-only warning and the supported `DarkSoulsIII.exe` SHA-256 list.

## 6. Repository hygiene and security

- **Branch protection & Auto-Merge on `main`:** require PR, require `ci` checks, linear history. While solo, `auto-merge.yml` approves and enables `--auto --squash` for Dependabot and repo PRs once all harsh checks pass.
- **Dependabot** (`.github/dependabot.yml`): covers 3 ecosystems weekly — `github-actions` (`/` with grouped minor/patch bumps), `docker` (`/docker` base images), and `pip` (`/tools/py` Python host telemetry requirements) — plus automated Dependabot security updates enabled in repository settings. CMake `FetchContent` pins stay audited by HTTPS-only URL and immutable tag checks in `check-guards.sh`.
- **CodeQL** for C/C++ (`.github/workflows/codeql.yml`, `security-extended` queries, free for public repos).
- **Supply-Chain & Dependency Review** (`.github/workflows/security.yml`): runs `actions/dependency-review-action@v4` on PRs (blocking moderate+ CVEs) and validates `SECURITY.md`, workflow `permissions:`, and secret scanning (`gitleaks`).
- **Secret scanning and push protection:** enabled in repo settings (free on public repos).
- **Least privilege:** workflows default to `contents: read`; only `release.yml` and `auto-merge.yml` request targeted write scopes.
- **Security Policy** ([`SECURITY.md`](../SECURITY.md)): documents the offline-only threat model, PE exploit mitigations (`HIGH_ENTROPY_VA`, `DYNAMIC_BASE`, `NX_COMPAT`), zero-network telemetry guarantee, and GitHub Security Advisories reporting.
- **Issue and PR templates** require: game build hash, OS/Proton version, GPU/driver, `soulsframe.log` attached, "offline only" checkbox.
- **No self-hosted runners** on a public repo (untrusted PR code).
- **Licensing:** add `LICENSE` before accepting contributions. Suggestion: MIT or Apache-2.0 for our code. Keep third-party licenses in `THIRD_PARTY_LICENSES.txt` (see `docs/07`).

## 7. What CI cannot cover (be honest about it)

- Real DS3 behaviour, anti-tamper interaction, visuals, and real GPU performance. These stay manual (`docs/05` §6).
- Real Windows D3D11 driver behaviour. Mitigation: a manual Windows test pass (or a friend with a GPU) per milestone, and an optional self-hosted Windows runner kept **private** and used only for pushes to `main`, never PRs from forks.

## 8. Rollout order

1. Commit `ci.yml` with `guards` + `docs` (now).
2. Add `host-tests` as soon as the first pure module exists (M0).
3. Add `win64` when the skeleton builds (M0).
4. Add `wine-smoke` (M1), required after two weeks of stability.
5. Add CodeQL, Dependabot, nightly (M2).
6. Add `release.yml` before the first public build (M10, or earlier for a pre-release).
