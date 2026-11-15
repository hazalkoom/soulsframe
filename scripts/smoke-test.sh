#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-/src/build/win64-release}"
cd "$BUILD_DIR"

# Prevent Wine from writing .local/share entries into the mounted source tree.
export HOME="/tmp"
mkdir -p /tmp/.local/share

# If container /wine is owned by root under --userns=keep-id, use a per-UID prefix.
if [ -d "${WINEPREFIX:-/wine}" ] && [ ! -O "${WINEPREFIX:-/wine}" ]; then
  export WINEPREFIX="/tmp/sf-wine-prefix"
  mkdir -p "$WINEPREFIX"
fi

# Wine ships a builtin dinput8.dll; force native override so our proxy loads.
export WINEDLLOVERRIDES="dinput8=n,b;mscoree,mshtml="

# 1. Verify offline guard blocks initialization when offline_acknowledged is not set.
rm -f soulsframe.log soulsframe.toml smoke-result.json
timeout 60 xvfb-run -a wine d3d11_testbed.exe --frames 10
grep -q "OFFLINE GUARD BLOCKED MOD INIT" soulsframe.log

# 2. Verify live Present + ResizeBuffers hooks and frame telemetry with offline opt-in.
cat > soulsframe.toml <<'EOF'
[general]
offline_acknowledged = true
mod_enabled = true
log_level = "info"

[present]
mode = "game"
max_fps = 0

[modules]
frame_telemetry = true
presentation = false
EOF

rm -f soulsframe.log
timeout 60 xvfb-run -a wine d3d11_testbed.exe --frames 300 --resize

grep -q "Present hook installed" soulsframe.log
grep -q "IDXGISwapChain::ResizeBuffers" soulsframe.log
grep -q "avg frame time" soulsframe.log

cat > smoke-result.json <<'EOF'
{
  "offline_guard_blocked_without_opt_in": true,
  "present_hook_installed": true,
  "resize_buffers_hooked": true,
  "frame_telemetry_logged": true,
  "status": "ok"
}
EOF

rm -f soulsframe.toml
echo "smoke OK"
