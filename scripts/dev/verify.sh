#!/usr/bin/env bash
set -euo pipefail

MODE="quick"
while [ "$#" -gt 0 ]; do
  case "$1" in
    --quick)
      MODE="quick"
      shift
      ;;
    --full)
      MODE="full"
      shift
      ;;
    -h|--help)
      cat <<'EOF'
Usage: bash scripts/dev/verify.sh [--quick | --full]

  --quick  Run guards, native Linux host-tests, and all Python --self-test suites (default).
  --full   Run --quick plus sf-build clang-format + win64-release/probe builds + PE audit,
           sf-wine-test multi-scenario smoke suite, and log inspector validation.
EOF
      exit 0
      ;;
    *)
      echo "ERROR: Unknown option '$1'. Use --quick or --full." >&2
      exit 2
      ;;
  esac
done

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

echo "[1/3] Running repository and layering guards..."
bash scripts/ci/check-guards.sh

echo "[2/3] Building and running native host-tests..."
if [ ! -f build/host-tests/build.ninja ]; then
  cmake --preset host-tests -DSF_WERROR=ON
fi
cmake --build --preset host-tests
ctest --preset host-tests --output-on-failure

echo "[3/3] Running Python tool self-tests..."
python3 tools/py/analyze_capture.py --self-test
python3 tools/py/plot_gpu_passes.py --self-test
python3 tools/py/bake_textures.py --self-test
python3 tools/py/inspect_log.py --self-test

if [ "$MODE" = "full" ]; then
  echo "[4/6] Running clang-format check and win64 cross-compile inside sf-build..."
  podman run --rm --userns=keep-id -v "$PWD:/src:Z" -v sf-ccache:/ccache -w /src sf-build \
    bash -c "find src tests tools/cpp -type f \( -name '*.h' -o -name '*.cpp' \) -print0 | xargs -0 clang-format --dry-run --Werror && cmake --preset win64-release -DSF_WERROR=ON && cmake --build --preset win64-release && cmake --preset win64-probe -DSF_WERROR=ON && cmake --build --preset win64-probe && x86_64-w64-mingw32-objdump -p build/win64-release/dinput8.dll | grep -q DirectInput8Create && x86_64-w64-mingw32-objdump -p build/win64-probe/dinput8.dll | grep -q DirectInput8Create"

  echo "[5/6] Running Wine D3D11 smoke suite inside sf-wine-test..."
  podman run --rm --userns=keep-id -v "$PWD:/src:Z" -w /src sf-wine-test bash scripts/smoke-test.sh

  echo "[6/6] Inspecting Wine overlay smoke log..."
  python3 tools/py/inspect_log.py build/win64-release/soulsframe-overlay.log
fi

echo "verify.sh ($MODE): ALL CHECKS PASSED"
