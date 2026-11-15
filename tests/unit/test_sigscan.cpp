#include <doctest/doctest.h>

#include "hooks/sigscan.h"

#include <array>
#include <cstddef>
#include <vector>

namespace {

std::vector<std::byte> bytes_of(std::initializer_list<uint8_t> vals) {
  std::vector<std::byte> out;
  out.reserve(vals.size());
  for (uint8_t v : vals) {
    out.push_back(static_cast<std::byte>(v));
  }
  return out;
}

} // namespace

TEST_CASE("sigscan exact and wildcard matches") {
  const auto img = bytes_of({0x90, 0x48, 0x8B, 0x05, 0xAA, 0xBB, 0xCC, 0xDD, 0x48, 0x85, 0xC0});

  const auto r1 = sf::hooks::sigscan(img, "48 8B 05 ? ? ? ? 48 85 C0");
  CHECK(r1.status == sf::hooks::ScanStatus::Found);
  CHECK(r1.offset == 1);
  CHECK(r1.match_count == 1);

  const auto r2 = sf::hooks::sigscan(img, "90 48");
  CHECK(r2.status == sf::hooks::ScanStatus::Found);
  CHECK(r2.offset == 0);

  const auto r3 = sf::hooks::sigscan(img, "85 C0");
  CHECK(r3.status == sf::hooks::ScanStatus::Found);
  CHECK(r3.offset == 9);
}

TEST_CASE("sigscan reports ambiguous, not found, empty and invalid patterns") {
  const auto img = bytes_of({0xCC, 0x48, 0x89, 0xCC, 0x48, 0x89});

  const auto amb = sf::hooks::sigscan(img, "48 89");
  CHECK(amb.status == sf::hooks::ScanStatus::Ambiguous);
  CHECK(amb.match_count == 2);
  CHECK(amb.offset == 1);

  const auto nf = sf::hooks::sigscan(img, "FF FF FF");
  CHECK(nf.status == sf::hooks::ScanStatus::NotFound);

  const std::vector<std::byte> empty_img;
  CHECK(sf::hooks::sigscan(empty_img, "48 89").status == sf::hooks::ScanStatus::NotFound);
  CHECK(sf::hooks::sigscan(img, "").status == sf::hooks::ScanStatus::InvalidPattern);
  CHECK(sf::hooks::sigscan(img, "48 ZZ").status == sf::hooks::ScanStatus::InvalidPattern);
}
