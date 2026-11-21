#!/usr/bin/env bash
set -euo pipefail

TAG="${1:-v1.0.0}"
OUT_DIR="${2:-out/release}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

RELEASE_DLL="build/win64-release/dinput8.dll"
PROBE_DLL="build/win64-probe/dinput8.dll"

if [ ! -f "$RELEASE_DLL" ]; then
  echo "ERROR: Missing $RELEASE_DLL. Build win64-release first." >&2
  exit 1
fi

package_variant() {
  local dll_path="$1"
  local pkg_name="$2"
  local include_probe_tools="$3"

  local stage_dir="${OUT_DIR}/${pkg_name}"
  local zip_path="${OUT_DIR}/${pkg_name}.zip"

  rm -rf "$stage_dir" "$zip_path"
  mkdir -p "${stage_dir}/presets"

  cp "$dll_path" "${stage_dir}/dinput8.dll"
  cp "config/soulsframe.toml.example" "${stage_dir}/soulsframe.toml"
  cp "config/README-INSTALL.txt" "${stage_dir}/README.txt"
  cp config/presets/*.toml "${stage_dir}/presets/"
  cp "LICENSE" "THIRD_PARTY_LICENSES.txt" "${stage_dir}/"

  if [ "$include_probe_tools" = "1" ]; then
    mkdir -p "${stage_dir}/tools"
    cp "tools/py/bake_textures.py" "tools/py/analyze_capture.py" "${stage_dir}/tools/"
  fi

  local zip_abs
  zip_abs="$(cd "$OUT_DIR" && pwd)/${pkg_name}.zip"
  (
    cd "$stage_dir"
    sha256sum dinput8.dll soulsframe.toml README.txt > SHA256SUMS.txt
    zip -r "$zip_abs" . >/dev/null
  )
  echo "Packaged: $zip_path"
}

mkdir -p "$OUT_DIR"
package_variant "$RELEASE_DLL" "soulsframe-${TAG}-win64" "0"

if [ -f "$PROBE_DLL" ]; then
  package_variant "$PROBE_DLL" "soulsframe-${TAG}-win64-probe" "1"
fi

(
  cd "$OUT_DIR"
  sha256sum ./*.zip > SHA256SUMS.txt
)
echo "Wrote checksums: ${OUT_DIR}/SHA256SUMS.txt"
