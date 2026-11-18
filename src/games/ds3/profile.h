#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sf {

/// Game-specific D3D11 pass detection heuristics forwarded to `gfx/d3d11/`.
struct RenderProfile {
  bool detect_particles_by_blend = true;
};

/// Resolved capability table for a host executable build.
/// This abstraction forms the seam separating game-specific offsets (`games/ds3/`)
/// from generic rendering and timing layers (`gfx/d3d11/`, future Elden Ring D3D12).
struct GameProfile {
  std::string id;
  std::string exe_sha256;

  /// Must only be true when `exe_sha256` matches a verified build in the offsets DB.
  /// Unknown builds stay restricted to safe D3D/telemetry hooks.
  bool engine_hooks_allowed = false;

  std::optional<uintptr_t> frame_dt_ptr;
  std::optional<uintptr_t> frame_limiter;
  RenderProfile render;
};

} // namespace sf

namespace sf::games::ds3 {

struct SigDb;

/// Looks up a Dark Souls III `GameProfile` keyed by the host executable's SHA-256 hash.
GameProfile resolve_profile(std::string_view exe_sha256);

/// Looks up a Dark Souls III `GameProfile` keyed by `exe_sha256` against a parsed signature DB.
/// Engine hooks remain disabled unless `exe_sha256` matches a build explicitly marked `verified`.
GameProfile resolve_profile(std::string_view exe_sha256, const SigDb& db);

} // namespace sf::games::ds3
