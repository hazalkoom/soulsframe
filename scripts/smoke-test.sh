#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-/src/build/win64-release}"
cd "$BUILD_DIR"

# Prevent Wine from writing .local/share entries into the mounted source tree
# and ensure both HOME and WINEPREFIX live in directories owned by the current UID.
export HOME="/tmp/sf-home"
mkdir -p "$HOME/.local/share"

if [ -z "${WINEPREFIX:-}" ] || [ ! -d "${WINEPREFIX}" ] || [ ! -O "${WINEPREFIX}" ]; then
  export WINEPREFIX="/tmp/sf-wine-prefix"
  mkdir -p "$WINEPREFIX"
fi

# Wine ships a builtin dinput8.dll; force native override so our proxy loads.
export WINEDLLOVERRIDES="dinput8=n,b;mscoree,mshtml="

# Scenario 1: Offline guard must block hooks when offline_acknowledged is unset/false.
rm -f soulsframe.log soulsframe-resize.log soulsframe-msaa.log soulsframe.toml smoke-result.json
timeout 60 xvfb-run -a wine d3d11_testbed.exe --frames 10
grep -q "OFFLINE GUARD BLOCKED MOD INIT" soulsframe.log
if grep -q "Present hook installed" soulsframe.log; then
  echo "ERROR: Hooks were installed even though offline guard blocked init!"
  cat soulsframe.log
  exit 1
fi

# Scenario 2: Live Present + ResizeBuffers hooks and frame telemetry with offline opt-in.
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
grep -q "Swapchain hooks removed (total_frames=300)" soulsframe.log
cp soulsframe.log soulsframe-resize.log

# Scenario 3: MSAA 4x swapchain hook verification.
rm -f soulsframe.log
timeout 60 xvfb-run -a wine d3d11_testbed.exe --frames 150 --msaa 4
grep -q "Present hook installed" soulsframe.log
grep -q "avg frame time" soulsframe.log
grep -q "Swapchain hooks removed (total_frames=150)" soulsframe.log
cp soulsframe.log soulsframe-msaa.log

cat > smoke-result.json <<'EOF'
{
  "scenario_1_offline_guard_blocked": true,
  "scenario_2_resize_300_frames_ok": true,
  "scenario_3_msaa_4x_150_frames_ok": true,
  "present_hook_installed": true,
  "resize_buffers_hooked": true,
  "frame_telemetry_logged": true,
  "clean_hook_shutdown": true,
  "status": "ok"
}
EOF

rm -f soulsframe.toml
echo "smoke OK (3 scenarios)"
