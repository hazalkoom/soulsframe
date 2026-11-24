#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

echo "=== SoulsFrame Project Context ==="
echo "Repo Root : $REPO_ROOT"
echo "Branch    : $(git rev-parse --abbrev-ref HEAD)"
echo "HEAD      : $(git rev-parse --short HEAD) ($(git log -1 --format=%s))"

dirty="$(git status --porcelain)"
if [ -z "$dirty" ]; then
  echo "Worktree  : clean"
else
  echo "Worktree  : dirty ($(printf '%s\n' "$dirty" | wc -l) changed path(s))"
  git status -sb
fi

echo ""
echo "=== Worktrees ==="
git worktree list

echo ""
echo "=== Recent Commits ==="
git log -n 8 --oneline

echo ""
echo "=== Build & Release Artifacts ==="
for path in \
  "build/host-tests/tests/unit/soulsframe_unit_tests" \
  "build/host-tests/tools/cpp/sftool" \
  "build/win64-release/dinput8.dll" \
  "build/win64-probe/dinput8.dll" \
  "out/release/soulsframe-v1.0.0-win64.zip"; do
  if [ -e "$path" ]; then
    size="$(du -h "$path" | cut -f1)"
    echo "  [present] $path ($size)"
  else
    echo "  [missing] $path"
  fi
done

echo ""
echo "=== Architecture Map ==="
echo "  Pure C++ (Linux-testable, no <windows.h>/<d3d11.h>):"
echo "    src/core/{config,config_diagnostics,module,module_registry,offline_guard,runtime_controls,sha256,version}.*"
echo "    src/games/ds3/{profile,render,sig_db}.*  src/gfx/fx/{fsr1_math,hud_overlay}.*"
echo "    src/hooks/sigscan.*  src/probe/{gpu_join,jsonl_writer,pe_dump,trace_types}.*"
echo "    src/tex/{content_hash,texture_pack}.*  src/timing/{fixed_step,frame_pacer,frame_stats,gpu_timing,present_policy}.*"
echo "  Win32 / D3D11 Runtime:"
echo "    src/core/{dllmain,log,proxy_dinput8,win_helpers}.cpp"
echo "    src/gfx/d3d11/*  src/probe/{probe_module,win_dump}.*  src/timing/fps_unlock_module.*"
echo "  Python Tools:"
echo "    tools/py/{analyze_capture,bake_textures,inspect_log,plot_gpu_passes}.py"
echo "  CMake Presets:"
echo "    host-tests | host-asan | host-cov | host-bench | win64-release | win64-probe"

echo ""
echo "=== Local Toolchain ==="
for tool in cmake ninja podman just python3 gh clang-format clang-tidy cppcheck; do
  if command -v "$tool" >/dev/null 2>&1; then
    echo "  [ok]      $tool -> $(command -v "$tool")"
  else
    echo "  [missing] $tool"
  fi
done
