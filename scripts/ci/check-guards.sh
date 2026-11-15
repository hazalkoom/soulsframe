#!/usr/bin/env bash
set -euo pipefail

# 1. Ensure no forbidden game files or large binaries are tracked in git.
bad=$(git ls-files | grep -Ei '\.(exe|dll|bdt|bhd|dcx|tpf|bnd|fxr|zip)$' || true)
if [ -n "$bad" ]; then
  echo "Forbidden binary/game files tracked:"
  echo "$bad"
  exit 1
fi

big=$(git ls-files -z | xargs -0 du -k | awk '$1 > 5120 {print $2}')
if [ -n "$big" ]; then
  echo "Tracked files over 5 MB:"
  echo "$big"
  exit 1
fi

# 2. Enforce pure-C++ layering boundary: pure modules must never include <windows.h> or D3D11.
pure_files=(
  src/core/config.h
  src/core/config.cpp
  src/core/module.h
  src/core/module_registry.h
  src/core/module_registry.cpp
  src/core/offline_guard.h
  src/core/offline_guard.cpp
  src/core/sha256.h
  src/core/sha256.cpp
  src/games/ds3/profile.h
  src/games/ds3/profile.cpp
  src/hooks/sigscan.h
  src/hooks/sigscan.cpp
  src/tex/content_hash.h
  src/tex/content_hash.cpp
  src/timing/frame_stats.h
  src/timing/frame_stats.cpp
)

for f in "${pure_files[@]}"; do
  if grep -Eqi '#\s*include\s*[<"](windows|d3d11|dxgi)\.h[>"]' "$f"; then
    echo "Layering violation: pure module $f includes Win32/D3D11 headers!"
    exit 1
  fi
done

echo "All repository guards passed."
