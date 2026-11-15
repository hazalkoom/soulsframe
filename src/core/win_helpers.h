#pragma once

#include "timing/frame_stats.h"

#include <string>
#include <vector>

namespace sf {

/// High-resolution `IClock` implementation backed by `QueryPerformanceCounter`.
class WinClock final : public timing::IClock {
public:
  WinClock();
  double now_ms() const override;

private:
  double inv_freq_ms_ = 0.0;
};

/// Computes the lowercase hex SHA-256 of the running host executable file on disk.
std::string compute_host_exe_sha256();

/// Tokenizes `GetCommandLineA()` into individual arguments (preserving quoted strings).
std::vector<std::string> get_process_args();

/// Scans loaded process modules for known graphics overlays (Steam, RTSS, ReShade, Special K)
/// whose DXGI/D3D11 hooks can conflict with SoulsFrame.
std::vector<std::string> detect_known_overlays();

} // namespace sf
