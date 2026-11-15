#include "core/offline_guard.h"

#include <algorithm>
#include <cctype>

namespace sf {

namespace {

std::string to_lower_ascii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

} // namespace

OfflineVerdict evaluate_offline_guard(const OfflineCheckInput& input) {
  if (input.force_online_detected) {
    return {false, "Online session state detected; mod hooks refused for ban safety."};
  }

  for (const auto& arg : input.command_line_args) {
    const std::string lower = to_lower_ascii(arg);
    if (lower == "--online" || lower == "-online" || lower == "--force-online") {
      return {false, "Online command-line flag detected (" + arg + "); mod hooks refused."};
    }
  }

  if (!input.config_offline_acknowledged) {
    return {false, "Offline-only opt-in missing: set general.offline_acknowledged = true in "
                   "soulsframe.toml to acknowledge offline usage."};
  }

  return {true, "Offline opt-in verified."};
}

} // namespace sf
