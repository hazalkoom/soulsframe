#include <doctest/doctest.h>

#include "core/offline_guard.h"

TEST_CASE("offline guard requires opt-in") {
  sf::OfflineCheckInput in;
  in.config_offline_acknowledged = false;
  const auto v = sf::evaluate_offline_guard(in);
  CHECK_FALSE(v.allowed);
  CHECK_FALSE(v.reason.empty());
}

TEST_CASE("offline guard blocks online flags and forced online state") {
  sf::OfflineCheckInput in;
  in.config_offline_acknowledged = true;
  in.command_line_args = {"DarkSoulsIII.exe", "--Online"};
  const auto v_online = sf::evaluate_offline_guard(in);
  CHECK_FALSE(v_online.allowed);
  CHECK_FALSE(v_online.reason.empty());

  in.command_line_args = {"DarkSoulsIII.exe"};
  in.force_online_detected = true;
  const auto v_forced = sf::evaluate_offline_guard(in);
  CHECK_FALSE(v_forced.allowed);
  CHECK_FALSE(v_forced.reason.empty());
}

TEST_CASE("offline guard blocks mixed-case online flags and preserves precedence") {
  for (const char* flag : {"--ONLINE", "-Online", "--Force-Online", "--fOrCe-OnLiNe"}) {
    sf::OfflineCheckInput in;
    in.config_offline_acknowledged = true;
    in.command_line_args = {"DarkSoulsIII.exe", "-windowed", flag};
    const sf::OfflineVerdict v = sf::evaluate_offline_guard(in);
    CHECK_FALSE(v.allowed);
    CHECK_FALSE(v.reason.empty());
  }

  // Forced online overrides both opt-in and benign CLI arguments.
  sf::OfflineCheckInput both;
  both.config_offline_acknowledged = true;
  both.force_online_detected = true;
  both.command_line_args = {"DarkSoulsIII.exe", "--offline"};
  const sf::OfflineVerdict v_both = sf::evaluate_offline_guard(both);
  CHECK_FALSE(v_both.allowed);
  CHECK_FALSE(v_both.reason.empty());
}

TEST_CASE("offline guard allows when acknowledged and clean") {
  sf::OfflineCheckInput in;
  in.config_offline_acknowledged = true;
  in.command_line_args = {"DarkSoulsIII.exe", "-windowed", "--offline"};
  const auto v = sf::evaluate_offline_guard(in);
  CHECK(v.allowed);
  CHECK_FALSE(v.reason.empty());

  sf::OfflineCheckInput no_args;
  no_args.config_offline_acknowledged = true;
  const auto v_empty = sf::evaluate_offline_guard(no_args);
  CHECK(v_empty.allowed);
  CHECK_FALSE(v_empty.reason.empty());
}
