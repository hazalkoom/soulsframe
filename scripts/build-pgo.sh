#!/usr/bin/env bash
set -euo pipefail

# Prototype GCC/Clang PGO build for the native Linux benchmark binary.
#
# Usage:
#   bash scripts/build-pgo.sh [iterations]
#
# Stages:
#   1. Configure/build host-bench with -fprofile-generate.
#   2. Run each benchmark with a fixed iteration count to emit gcda counters.
#   3. Rebuild with -fprofile-use and copy/link the PGO binary as sf-bench-pgo.
#   4. Compare median ns and throughput between standard Release and PGO.

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

ITERATIONS="${1:-1000000}"
PGO_DIR="build/host-bench-pgo"
STANDARD_JSON="build/host-bench-pgo-standard.json"
TRAINING_JSON="build/host-bench-pgo-training.json"
PGO_JSON="build/host-bench-pgo.json"

if [[ ! "${ITERATIONS}" =~ ^[1-9][0-9]*$ ]]; then
  echo "error: iterations must be a positive integer (got '${ITERATIONS}')" >&2
  exit 2
fi

CXX_COMPILER="${CXX:-g++}"
C_COMPILER="${CC:-gcc}"

echo "==> Stage 1/4: build standard Release benchmark"
cmake --preset host-bench -DCMAKE_CXX_COMPILER="${CXX_COMPILER}" -DCMAKE_C_COMPILER="${C_COMPILER}" -DSF_WERROR=ON
cmake --build --preset host-bench
STANDARD_BIN="build/host-bench/tests/bench/soulsframe_benchmarks"
"${STANDARD_BIN}" --iterations "${ITERATIONS}" --json "${STANDARD_JSON}"

if [[ "${CXX_COMPILER}" == *clang* ]]; then
  PROFILE_FLAGS="-fprofile-instr-generate=${PGO_DIR}/pgo.profraw"
  USE_FLAGS="-fprofile-instr-use=${PGO_DIR}/pgo.profdata"
else
  PROFILE_FLAGS="-fprofile-generate"
  USE_FLAGS="-fprofile-use"
fi

rm -rf "${PGO_DIR}"
mkdir -p "${PGO_DIR}"

echo "==> Stage 2/4: generate profile counters"
cmake -S . -B "${PGO_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSF_HOST_ONLY=ON \
  -DSF_BUILD_BENCH=ON \
  -DSF_WERROR=OFF \
  -DCMAKE_CXX_COMPILER="${CXX_COMPILER}" \
  -DCMAKE_C_COMPILER="${C_COMPILER}" \
  -DCMAKE_CXX_FLAGS="${PROFILE_FLAGS}" \
  -DCMAKE_C_FLAGS="${PROFILE_FLAGS}" \
  -DCMAKE_EXE_LINKER_FLAGS="${PROFILE_FLAGS}"
cmake --build "${PGO_DIR}" --target soulsframe_benchmarks
TRAIN_BIN="${PGO_DIR}/tests/bench/soulsframe_benchmarks"
"${TRAIN_BIN}" --iterations "${ITERATIONS}" --json "${TRAINING_JSON}"

# GCC creates gcda sidecars next to objects in the build tree; Clang requires
# llvm-profdata merging before -fprofile-use can consume raw profiling counters.
if [[ "${CXX_COMPILER}" == *clang* ]]; then
  command -v llvm-profdata >/dev/null 2>&1 || {
    echo "error: llvm-profdata is required for Clang PGO" >&2
    exit 1
  }
  rm -f "${PGO_DIR}/pgo.profraw" "${PGO_DIR}/pgo.profdata"
  find "${PGO_DIR}" -name '*.profraw' -print0 | xargs -0 -r llvm-profdata merge -o "${PGO_DIR}/pgo.profdata"
fi

echo "==> Stage 3/4: rebuild with profile-use"
cmake -S . -B "${PGO_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSF_HOST_ONLY=ON \
  -DSF_BUILD_BENCH=ON \
  -DSF_WERROR=OFF \
  -DCMAKE_CXX_COMPILER="${CXX_COMPILER}" \
  -DCMAKE_C_COMPILER="${C_COMPILER}" \
  -DCMAKE_CXX_FLAGS="${USE_FLAGS}" \
  -DCMAKE_C_FLAGS="${USE_FLAGS}" \
  -DCMAKE_EXE_LINKER_FLAGS="${USE_FLAGS}"
cmake --build "${PGO_DIR}" --target soulsframe_benchmarks
PGO_BIN="${PGO_DIR}/tests/bench/soulsframe_benchmarks"
cp "${PGO_BIN}" "build/sf-bench-pgo"
PGO_RESULT="build/sf-bench-pgo"
"${PGO_RESULT}" --iterations "${ITERATIONS}" --json "${PGO_JSON}"

echo "==> Stage 4/4: compare standard Release vs PGO release"
python3 - "${STANDARD_JSON}" "${PGO_JSON}" <<'PYEOF'
import json
import sys

standard_path, pgo_path = sys.argv[1], sys.argv[2]
with open(standard_path, "r", encoding="utf-8") as fh:
    standard = json.load(fh).get("benchmarks", [])
with open(pgo_path, "r", encoding="utf-8") as fh:
    pgo = json.load(fh).get("benchmarks", [])

standard_by_name = {row["name"]: row for row in standard}
pgo_by_name = {row["name"]: row for row in pgo}

print("| Benchmark | Standard median ns | PGO median ns | Delta | Throughput delta |")
print("|---|---:|---:|---:|---:|")

for name in sorted(standard_by_name):
    if name not in pgo_by_name:
        continue
    std = standard_by_name[name]
    opt = pgo_by_name[name]
    std_ns = float(std["median_ns"])
    opt_ns = float(opt["median_ns"])
    if std_ns > 0:
        delta_pct = (opt_ns - std_ns) * 100.0 / std_ns
    else:
        delta_pct = 0.0
    std_ops = float(std["ops_per_sec"])
    opt_ops = float(opt["ops_per_sec"])
    throughput_pct = ((opt_ops - std_ops) * 100.0 / std_ops) if std_ops > 0 else 0.0
    sign = "+" if delta_pct >= 0 else ""
    tp_sign = "+" if throughput_pct >= 0 else ""
    print(f"| {name} | {std_ns:.2f} | {opt_ns:.2f} | {sign}{delta_pct:.2f}% | {tp_sign}{throughput_pct:.2f}% |")
PYEOF

echo "PGO binary: ${REPO_ROOT}/${PGO_RESULT}"
