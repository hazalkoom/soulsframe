#!/usr/bin/env bash
#
# quick-bench.sh - Run the Release benchmark harness and print a friendly
# colored summary of fastest/slowest operations.
#
# Usage:
#   ./scripts/quick-bench.sh [--json PATH] [--preset host-bench]
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

JSON_PATH="${REPO_ROOT}/build/host-bench/bench-results.json"
PRESET="host-bench"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --json)
      JSON_PATH="$2"
      shift 2
      ;;
    --preset)
      PRESET="$2"
      shift 2
      ;;
    --help|-h)
      echo "Usage: $0 [--json PATH] [--preset NAME]"
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

# ANSI colors (no external deps).
C_RESET='\033[0m'
C_BOLD='\033[1m'
C_GREEN='\033[32m'
C_YELLOW='\033[33m'
C_RED='\033[31m'
C_CYAN='\033[36m'
C_MAGENTA='\033[35m'

cd "${REPO_ROOT}"

echo "${C_BOLD}${C_CYAN}==> SoulsFrame quick benchmark${C_RESET}"
echo "${C_BOLD}Preset:${C_RESET} ${PRESET}"
echo "${C_BOLD}JSON output:${C_RESET} ${JSON_PATH}"
echo

# 1. Configure.
echo "${C_BOLD}${C_CYAN}==> Configuring CMake (${PRESET})${C_RESET}"
cmake --preset "${PRESET}"

# 2. Build.
echo "${C_BOLD}${C_CYAN}==> Building benchmarks${C_RESET}"
cmake --build --preset "${PRESET}"

BIN="${REPO_ROOT}/build/${PRESET}/tests/bench/soulsframe_benchmarks"
if [[ ! -x "${BIN}" ]]; then
  echo "${C_RED}error: benchmark binary not found at ${BIN}${C_RESET}" >&2
  exit 1
fi

# 3. Run benchmarks and emit JSON.
echo "${C_BOLD}${C_CYAN}==> Running benchmark harness${C_RESET}"
"${BIN}" --json "${JSON_PATH}"

if [[ ! -f "${JSON_PATH}" ]]; then
  echo "${C_RED}error: benchmark JSON not written to ${JSON_PATH}${C_RESET}" >&2
  exit 1
fi

# 4. Parse JSON and print a friendly colored summary.
echo
echo "${C_BOLD}${C_CYAN}==> Benchmark summary${C_RESET}"
echo

# Single python3 heredoc that reads JSON_PATH from the environment variable
QUICK_BENCH_JSON="${JSON_PATH}" python3 - <<'PYEOF'
import json
import os
import sys

path = os.environ["QUICK_BENCH_JSON"]
with open(path, "r", encoding="utf-8") as fh:
    data = json.load(fh)
benchmarks = data.get("benchmarks", [])
if not benchmarks:
    print("\n\033[31mNo benchmark results found.\033[0m")
    sys.exit(1)

# Sort by median_ns ascending (fastest first).
benchmarks.sort(key=lambda b: b.get("median_ns", float("inf")))

C_RESET = "\033[0m"
C_BOLD = "\033[1m"
C_GREEN = "\033[32m"
C_YELLOW = "\033[33m"
C_RED = "\033[31m"
C_CYAN = "\033[36m"
C_MAGENTA = "\033[35m"

fastest = benchmarks[0]
slowest = benchmarks[-1]

print(f"{C_BOLD}{C_CYAN}Total benchmarks run: {len(benchmarks)}{C_RESET}")
print()

# Fastest.
print(f"{C_GREEN}{C_BOLD}Fastest:{C_RESET}")
print(
    f"  {C_MAGENTA}{fastest['name']}{C_RESET} "
    f"({fastest.get('category', '?')})"
)
print(f"    median: {C_GREEN}{fastest['median_ns']:.2f} ns{C_RESET}  "
      f"min: {fastest['min_ns']:.2f} ns  "
      f"p95: {fastest['p95_ns']:.2f} ns  "
      f"ops/s: {fastest['ops_per_sec']:.2f}")

print()

# Slowest.
print(f"{C_RED}{C_BOLD}Slowest:{C_RESET}")
print(
    f"  {C_MAGENTA}{slowest['name']}{C_RESET} "
    f"({slowest.get('category', '?')})"
)
print(f"    median: {C_RED}{slowest['median_ns']:.2f} ns{C_RESET}  "
      f"min: {slowest['min_ns']:.2f} ns  "
      f"p95: {slowest['p95_ns']:.2f} ns  "
      f"ops/s: {slowest['ops_per_sec']:.2f}")

print()

# Top 5 fastest table.
print(f"{C_BOLD}{C_CYAN}Top 5 fastest:{C_RESET}")
print(f"  {'Name':<36} {'Category':<14} {'Median (ns)':>12} {'P95 (ns)':>12}")
print(f"  {'-'*36} {'-'*14} {'-'*12} {'-'*12}")
for b in benchmarks[:5]:
    print(
        f"  {C_MAGENTA}{b['name']:<36}{C_RESET} "
        f"{b.get('category', '?'):<14} "
        f"{C_GREEN}{b['median_ns']:>11.2f} ns{C_RESET} "
        f"{b['p95_ns']:>11.2f} ns"
    )

print()

# Top 5 slowest table.
print(f"{C_BOLD}{C_CYAN}Top 5 slowest:{C_RESET}")
print(f"  {'Name':<36} {'Category':<14} {'Median (ns)':>12} {'P95 (ns)':>12}")
print(f"  {'-'*36} {'-'*14} {'-'*12} {'-'*12}")
for b in benchmarks[-5:]:
    print(
        f"  {C_MAGENTA}{b['name']:<36}{C_RESET} "
        f"{b.get('category', '?'):<14} "
        f"{C_RED}{b['median_ns']:>11.2f} ns{C_RESET} "
        f"{b['p95_ns']:>11.2f} ns"
    )

print()
print(f"{C_BOLD}Full JSON: {path}{C_RESET}")
PYEOF