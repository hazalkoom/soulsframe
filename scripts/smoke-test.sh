#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-/src/build/win64-release}"
PROBE_DIR="${2:-}"
if [ -z "$PROBE_DIR" ]; then
  CANDIDATE_PROBE="$(dirname "$BUILD_DIR")/win64-probe"
  if [ -f "$CANDIDATE_PROBE/dinput8.dll" ]; then
    PROBE_DIR="$CANDIDATE_PROBE"
  fi
fi
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
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

# Scenario 3b (M5): Presentation flip-model hook + 60 FPS hybrid frame pacer.
cat > soulsframe.toml <<'EOF'
[general]
offline_acknowledged = true
mod_enabled = true
log_level = "info"

[present]
mode = "flip"
max_fps = 60

[modules]
frame_telemetry = true
presentation = true
EOF

rm -f soulsframe.log
timeout 60 xvfb-run -a wine d3d11_testbed.exe --frames 30
grep -q "Present hook installed" soulsframe.log
grep -q "PresentationModule active (mode=flip, max_fps=60, flip_hook=on)" soulsframe.log
grep -q "PresentationModule shutdown" soulsframe.log
grep -q "Swapchain hooks removed (total_frames=30)" soulsframe.log
cp soulsframe.log soulsframe-present.log

# Scenario 3c (M6): Particle pass mitigation (low-res viewport scaling + instance cap).
cat > soulsframe.toml <<'EOF'
[general]
offline_acknowledged = true
mod_enabled = true
log_level = "info"

[modules]
frame_telemetry = true
presentation = false
particles = true

[particles]
mode = "auto"
scale_percent = 50
max_instances_per_draw = 4
max_draws_per_frame = 0
EOF

rm -f soulsframe.log
timeout 60 xvfb-run -a wine d3d11_testbed.exe --frames 30 --particles
grep -q "ParticleModule active (mode=auto, scale_percent=50, max_instances=4" soulsframe.log
grep -q "ParticleModule shutdown (frames=30, particle_draws=30, low_res=30, throttled=30, skipped=0, instances_culled=120" soulsframe.log
cp soulsframe.log soulsframe-particles.log

# Scenario 3d (M7): Spatial upscaler + RCAS pass on non-MSAA backbuffer.
cat > soulsframe.toml <<'EOF'
[general]
offline_acknowledged = true
mod_enabled = true
log_level = "info"

[modules]
frame_telemetry = true
presentation = false
particles = false
upscaler = true

[upscaler]
mode = "fsr1"
preset = "ultra_quality"
sharpness_percent = 80
EOF

rm -f soulsframe.log
timeout 60 xvfb-run -a wine d3d11_testbed.exe --frames 20
grep -q "UpscalerModule active (mode=fsr1, preset=ultra_quality, scale_percent=77, sharpness=80)" soulsframe.log
grep -q "UpscalerModule shutdown (frames=20, easu_passes=20, rcas_passes=20" soulsframe.log
cp soulsframe.log soulsframe-upscaler.log

# Scenario 3e (M8): Experimental fixed-step FPS unlock accumulator in safe dry-run mode.
cat > soulsframe.toml <<'EOF'
[general]
offline_acknowledged = true
mod_enabled = true
log_level = "info"

[modules]
frame_telemetry = true
presentation = false
particles = false
upscaler = false
fps_unlock = true

[fps_unlock]
strategy = "fixed_step"
target_fps = 120
sim_tick_hz = 60
max_substeps = 4
EOF

rm -f soulsframe.log
timeout 60 xvfb-run -a wine d3d11_testbed.exe --frames 30
grep -q "FpsUnlockModule active (strategy=fixed_step, target_fps=120, sim_tick_hz=60, max_substeps=4, dry_run=true)" soulsframe.log
grep -q "FpsUnlockModule shutdown (frames=30" soulsframe.log
cp soulsframe.log soulsframe-fps-unlock.log

SCENARIO_4_OK=false
if [ -n "$PROBE_DIR" ] && [ -f "$PROBE_DIR/dinput8.dll" ]; then
  # Scenario 4: SF_PROBE draw-call tracer JSONL capture with alpha-blended particles.
  if [ ! -f "$PROBE_DIR/d3d11_testbed.exe" ] && [ -f "$BUILD_DIR/d3d11_testbed.exe" ]; then
    cp "$BUILD_DIR/d3d11_testbed.exe" "$PROBE_DIR/d3d11_testbed.exe"
  fi
  cd "$PROBE_DIR"
  rm -f soulsframe.log soulsframe.toml soulsframe_capture.jsonl soulsframe_mem_dump.exe
  cat > soulsframe.toml <<'EOF'
[general]
offline_acknowledged = true
mod_enabled = true
log_level = "info"

[modules]
frame_telemetry = true
probe_tracer = true
presentation = false

[probe]
capture_frames = 20
output_file = "soulsframe_capture.jsonl"
trace_resources = true
dump_module = true
dump_file = "soulsframe_mem_dump.exe"
EOF
  timeout 60 xvfb-run -a wine d3d11_testbed.exe --frames 30 --particles
  grep -q "Present hook installed" soulsframe.log
  grep -q "ID3D11DeviceContext hooks installed" soulsframe.log
  grep -q "ProbeModule: MemDump ok" soulsframe.log
  test -s soulsframe_mem_dump.exe
  grep -q "ProbeModule reached capture_frames limit (20)" soulsframe.log
  grep -q "ID3D11DeviceContext hooks removed" soulsframe.log
  grep -q '"type":"session_header"' soulsframe_capture.jsonl
  grep -q '"type":"frame"' soulsframe_capture.jsonl
  grep -q '"tag":"particles_alpha"' soulsframe_capture.jsonl
  grep -q '"api":"DrawInstanced"' soulsframe_capture.jsonl
  if command -v python3 >/dev/null 2>&1 && [ -f "$REPO_ROOT/tools/py/analyze_capture.py" ]; then
    python3 "$REPO_ROOT/tools/py/analyze_capture.py" soulsframe_capture.jsonl > /dev/null
  fi
  rm -f soulsframe.toml soulsframe_mem_dump.exe
  SCENARIO_4_OK=true
  cd "$BUILD_DIR"
fi

cat > smoke-result.json <<EOF
{
  "scenario_1_offline_guard_blocked": true,
  "scenario_2_resize_300_frames_ok": true,
  "scenario_3_msaa_4x_150_frames_ok": true,
  "scenario_3b_presentation_flip_pacer_ok": true,
  "scenario_3c_particles_mitigation_ok": true,
  "scenario_3d_upscaler_fsr1_ok": true,
  "scenario_3e_fps_unlock_dry_run_ok": true,
  "scenario_4_probe_jsonl_capture_ok": ${SCENARIO_4_OK},
  "present_hook_installed": true,
  "resize_buffers_hooked": true,
  "frame_telemetry_logged": true,
  "clean_hook_shutdown": true,
  "status": "ok"
}
EOF

rm -f soulsframe.toml
echo "smoke OK (probe=${SCENARIO_4_OK})"
