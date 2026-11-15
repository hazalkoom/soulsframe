#include <doctest/doctest.h>

#include "games/ds3/profile.h"

TEST_CASE("ds3 profile normalizes hash and guards engine hooks on unknown builds") {
  const auto prof = sf::games::ds3::resolve_profile("ABCDEF0123456789");
  CHECK(prof.id == "ds3");
  CHECK(prof.exe_sha256 == "abcdef0123456789");
  CHECK_FALSE(prof.engine_hooks_allowed);
  CHECK_FALSE(prof.frame_dt_ptr.has_value());
  CHECK_FALSE(prof.frame_limiter.has_value());
}
