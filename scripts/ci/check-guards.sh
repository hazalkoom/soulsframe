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

# 3. Verify offline-only safety default in template config.
if ! grep -Eq '^\s*offline_acknowledged\s*=\s*false\b' config/soulsframe.toml.example; then
  echo "Safety violation: config/soulsframe.toml.example must default offline_acknowledged = false!"
  exit 1
fi
if grep -Eq '^\s*offline_acknowledged\s*=\s*true\b' config/soulsframe.toml.example; then
  echo "Safety violation: config/soulsframe.toml.example must never default offline_acknowledged = true!"
  exit 1
fi

# 4. Enforce game offsets isolation: no hardcoded x64 image base RVAs outside src/games/ds3/.
rva_leaks=$(grep -rnE '0x14[0-9a-fA-F]{7}' src/ --exclude-dir=ds3 || true)
if [ -n "$rva_leaks" ]; then
  echo "Offsets isolation violation: hardcoded DS3 VA outside src/games/ds3/:"
  echo "$rva_leaks"
  exit 1
fi

# 5. Verify pinned FetchContent tags in cmake/deps.cmake and THIRD_PARTY_LICENSES.txt coverage.
while IFS= read -r dep; do
  if ! grep -qi "$dep" THIRD_PARTY_LICENSES.txt; then
    echo "License ledger missing third-party dependency: $dep"
    exit 1
  fi
done < <(sed -nE 's/^\s*FetchContent_Declare\(([^[:space:])]+).*/\1/p' cmake/deps.cmake)

while IFS= read -r tag; do
  if [[ ! "$tag" =~ ^v[0-9] && ! "$tag" =~ ^[0-9a-fA-F]{40}$ ]]; then
    echo "Unpinned or non-version GIT_TAG in cmake/deps.cmake: $tag"
    exit 1
  fi
done < <(sed -nE 's/^\s*GIT_TAG\s+([^[:space:]]+).*/\1/p' cmake/deps.cmake)

# 6. Verify every public header in src/ has at least one /// docstring comment.
missing_docs=""
while IFS= read -r hdr; do
  if ! grep -q '///' "$hdr"; then
    missing_docs+="$hdr"$'\n'
  fi
done < <(find src -name '*.h' | sort)
if [ -n "$missing_docs" ]; then
  echo "Headers missing /// docstrings:"
  echo "$missing_docs"
  exit 1
fi

echo "All repository guards passed."
