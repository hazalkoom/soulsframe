# Security Policy & Threat Model

SoulsFrame is a local, single-player rendering/diagnostic proxy (`dinput8.dll`) for Dark Souls III (x64, D3D11). Security and player-account safety are enforced as hard engineering invariants in both code and CI.

## Supported Versions

| Version / Branch | Security Updates |
|---|---|
| `main` | Actively maintained (harsh CI + CodeQL + Dependabot) |
| `develop` | Integration branch receiving all security patches first |
| Older pre-releases / snapshots | Unsupported — upgrade to latest `main` release |

## Security & Safety Guarantees

1. **Offline-Only Anti-Tamper Enforcement**
   - `config/soulsframe.toml.example` strictly defaults `general.offline_acknowledged = false` (enforced by CI Guard #3 in `scripts/ci/check-guards.sh`).
   - `sf::evaluate_offline_guard()` (`src/core/offline_guard.cpp`) blocks all mod initialization and graphics hooks unless the user explicitly sets `offline_acknowledged = true` and no online/anti-cheat markers (`-forceonline`, `force_online`) are present.
2. **PE Binary Exploit Mitigations**
   - Both `dinput8.dll` and `d3d11_testbed.exe` are linked with 64-bit ASLR and DEP (`-Wl,--dynamicbase`, `-Wl,--high-entropy-va`, `-Wl,--nxcompat`) and statically link C/C++ runtime libraries (`-static`, `-static-libgcc`, `-static-libstdc++`) to prevent DLL-sidecar confusion or missing-runtime failures.
   - CI audits `x86_64-w64-mingw32-objdump -p` output on every `win64-release` and `win64-probe` build to verify `DirectInput8Create` export integrity, `HIGH_ENTROPY_VA`, `DYNAMIC_BASE`, `NX_COMPAT`, and zero dynamic runtime DLL imports.
3. **Zero Network Telemetry**
   - SoulsFrame opens zero network sockets and transmits no data off-machine. Runtime logs (`soulsframe.log`) and captures (`*.jsonl`) stay strictly local.
4. **Supply-Chain Pinning**
   - Every C++ dependency in `cmake/deps.cmake` must use `https://` and an immutable Git tag or 40-character SHA-1 commit hash (`FetchContent_Declare`), with matching attribution in `THIRD_PARTY_LICENSES.txt`.
   - Python host-tool dependencies in `tools/py/requirements.txt` are exact-pinned (`==`) and monitored by Dependabot (`pip` ecosystem), alongside `github-actions` and `docker` (`docker/`).
5. **Automated Continuous Scanning**
   - **CodeQL (`security-extended`)**: Semantic C++20 static security analysis (`.github/workflows/codeql.yml`).
   - **Gitleaks**: Git history and tree secret scanning in both `ci.yml` and `security.yml`.
   - **Dependency Review & Supply-Chain Audit**: PR dependency vulnerability scanning and workflow least-privilege checks (`.github/workflows/security.yml`).
   - **ASan / UBSan + Harsh Static Analysis**: `host-asan`, `clang-format --Werror`, `clang-tidy --warnings-as-errors='*'`, `cppcheck --error-exitcode=1`, and `-DSF_WERROR=ON` across Linux and MinGW-w64 builds.

## Reporting a Vulnerability

Please **do not** open a public GitHub Issue for sensitive security findings (arbitrary memory corruption, secret exposure, supply-chain compromise, or proxy loading flaws).

- Preferred channel: Open a private report via GitHub Security Advisories under **Security -> Advisories -> Report a vulnerability** on `https://github.com/hazalkoom/soulsframe/security/advisories/new`.
- Include: affected commit SHA or release tag, reproduction steps, impact assessment, and any suggested fix.
- Response target: Initial acknowledgment within 72 hours and a tracked fix branch validated by the full CI and CodeQL pipeline.
