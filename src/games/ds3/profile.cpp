#include "games/ds3/profile.h"

#include <algorithm>
#include <cctype>

namespace sf::games::ds3 {

namespace {

std::string normalize_hex(std::string_view sv) {
  std::string out(sv);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

} // namespace

GameProfile resolve_profile(std::string_view exe_sha256) {
  GameProfile profile;
  profile.id = "ds3";
  profile.exe_sha256 = normalize_hex(exe_sha256);
  // Known verified DarkSoulsIII.exe builds will populate engine_hooks_allowed = true
  // and pinned signatures in M4. Unknown hashes run only the safe D3D/telemetry layer.
  profile.engine_hooks_allowed = false;
  return profile;
}

} // namespace sf::games::ds3
