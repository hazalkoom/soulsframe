#include <doctest/doctest.h>

#include "core/offline_guard.h"

TEST_CASE("offline guard requires opt-in") {
  sf::OfflineCheckInput in;
  in.config_offline_acknowledged = false;
  const auto v = sf::evaluate_offline_guard(in);
  CHECK_FALSE(v.allowed);
}

TEST_CASE("offline guard blocks online flags and forced online state") {
  sf::OfflineCheckInput in;
  in.config_offline_acknowledged = true;
  in.command_line_args = {"DarkSoulsIII.exe", "--Online"};
  CHECK_FALSE(sf::evaluate_offline_guard(in).allowed);

  in.command_line_args = {"DarkSoulsIII.exe"};
  in.force_online_detected = true;
  CHECK_FALSE(sf::evaluate_offline_guard(in).allowed);
}

TEST_CASE("offline guard allows when acknowledged and clean") {
  sf::OfflineCheckInput in;
  in.config_offline_acknowledged = true;
  in.command_line_args = {"DarkSoulsIII.exe", "-windowed"};
  const auto v = sf::evaluate_offline_guard(in);
  CHECK(v.allowed);
}
