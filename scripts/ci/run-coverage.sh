#!/usr/bin/env bash
set -euo pipefail

MIN_LINE_COVERAGE="${SF_MIN_LINE_COVERAGE:-85}"
CXX_COMPILER="${CMAKE_CXX_COMPILER:-g++}"

rm -rf build/host-cov
cmake --preset host-cov -DCMAKE_CXX_COMPILER="$CXX_COMPILER" -DSF_WERROR=ON
cmake --build --preset host-cov
ctest --preset host-cov --output-on-failure

mkdir -p build/host-cov/report

# Measure coverage strictly on project source files under src/ (exclude third-party deps and test files).
gcovr \
  --root . \
  --filter 'src/' \
  --exclude 'tests/' \
  --exclude 'build/.*/_deps/' \
  --exclude-throw-branches \
  --print-summary \
  --txt \
  --html-details build/host-cov/report/index.html \
  --json-summary build/host-cov/report/summary.json \
  --cobertura build/host-cov/report/cobertura.xml \
  --fail-under-line "$MIN_LINE_COVERAGE" \
  build/host-cov
