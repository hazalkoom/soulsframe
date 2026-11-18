#include "games/ds3/profile.h"

#include "games/ds3/sig_db.h"

#include <algorithm>
#include <cctype>

namespace sf::games::ds3 {

namespace {

std::string normalize_hex(std::string_view sv) {
  std::string out(sv);
  std::ranges::transform(out, out.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

} // namespace

GameProfile resolve_profile(std::string_view exe_sha256) {
  GameProfile profile;
  profile.id = "ds3";
  profile.exe_sha256 = normalize_hex(exe_sha256);
  // Unknown hashes or sessions without a verified DB run only the safe D3D/telemetry layer.
  profile.engine_hooks_allowed = false;
  return profile;
}

GameProfile resolve_profile(std::string_view exe_sha256, const SigDb& db) {
  GameProfile profile = resolve_profile(exe_sha256);
  if (const auto* build = db.find_build(profile.exe_sha256); build != nullptr && build->verified) {
    profile.engine_hooks_allowed = true;
  }
  return profile;
}

} // namespace sf::games::ds3
