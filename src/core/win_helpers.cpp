#include "core/win_helpers.h"

#include "core/sha256.h"

#include <windows.h>

#include <cstddef>
#include <fstream>
#include <vector>

namespace sf {

WinClock::WinClock() {
  LARGE_INTEGER freq{};
  QueryPerformanceFrequency(&freq);
  if (freq.QuadPart > 0) {
    inv_freq_ms_ = 1000.0 / static_cast<double>(freq.QuadPart);
  }
}

double WinClock::now_ms() const {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return static_cast<double>(now.QuadPart) * inv_freq_ms_;
}

std::string compute_host_exe_sha256() {
  char path[MAX_PATH]{};
  const DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return "";
  }
  std::ifstream ifs(path, std::ios::binary | std::ios::ate);
  if (!ifs.is_open()) {
    return "";
  }
  const auto size = ifs.tellg();
  if (size <= 0) {
    return "";
  }
  ifs.seekg(0, std::ios::beg);
  std::vector<std::byte> buf(static_cast<size_t>(size));
  if (!ifs.read(reinterpret_cast<char*>(buf.data()), size)) {
    return "";
  }
  return sha256_hex(buf);
}

std::vector<std::string> get_process_args() {
  std::vector<std::string> args;
  const char* cmd = GetCommandLineA();
  if (!cmd) {
    return args;
  }

  std::string current;
  bool in_quotes = false;
  bool has_token = false;

  for (const char* p = cmd; *p != '\0'; ++p) {
    const char c = *p;
    if (c == '"') {
      in_quotes = !in_quotes;
      has_token = true;
    } else if (!in_quotes && (c == ' ' || c == '\t')) {
      if (has_token) {
        args.push_back(std::move(current));
        current.clear();
        has_token = false;
      }
    } else {
      current.push_back(c);
      has_token = true;
    }
  }
  if (has_token) {
    args.push_back(std::move(current));
  }
  return args;
}

std::vector<std::string> detect_known_overlays() {
  constexpr const char* kOverlays[] = {
      "GameOverlayRenderer64.dll",  "RTSSHooks64.dll", "ReShade64.dll", "SpecialK64.dll",
      "d3dcompiler_47_reshade.dll",
  };
  std::vector<std::string> found;
  for (const char* name : kOverlays) {
    if (GetModuleHandleA(name) != nullptr) {
      found.emplace_back(name);
    }
  }
  return found;
}

} // namespace sf
