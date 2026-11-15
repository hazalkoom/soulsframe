#include <doctest/doctest.h>

#include "hooks/sigscan.h"

#include <array>
#include <cstddef>
#include <vector>

namespace {

/// Helper converting byte literals into `std::vector<std::byte>` for readable scan tests.
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

TEST_CASE("sigscan handles boundary offsets, whitespace, wildcards, and malformed tokens") {
  // Single '?' and double '??' wildcards with irregular whitespace must parse identically.
  const auto p_a = sf::hooks::parse_pattern("  48   ?  ??  c0 \t ");
  const auto p_b = sf::hooks::parse_pattern("48 ?? ? C0");
  REQUIRE(p_a.has_value());
  REQUIRE(p_b.has_value());
  REQUIRE(p_a->size() == p_b->size());
  for (size_t i = 0; i < p_a->size(); ++i) {
    CHECK((*p_a)[i].wildcard == (*p_b)[i].wildcard);
    CHECK((*p_a)[i].value == (*p_b)[i].value);
  }

  // All-wildcard pattern on exact-length vs oversized buffers.
  const auto exact_3 = bytes_of({0x10, 0x20, 0x30});
  const auto exact_4 = bytes_of({0x10, 0x20, 0x30, 0x40});
  const auto w3 = sf::hooks::sigscan(exact_3, "? ?? ?");
  CHECK(w3.status == sf::hooks::ScanStatus::Found);
  CHECK(w3.offset == 0);
  const auto w4 = sf::hooks::sigscan(exact_4, "? ?? ?");
  CHECK(w4.status == sf::hooks::ScanStatus::Ambiguous);
  CHECK(w4.match_count == 2);

  // Pattern longer than buffer -> NotFound.
  CHECK(sf::hooks::sigscan(exact_3, "10 20 30 40").status == sf::hooks::ScanStatus::NotFound);

  // Empty pre-parsed pattern passed directly.
  CHECK(sf::hooks::sigscan(exact_3, sf::hooks::ParsedPattern{}).status ==
        sf::hooks::ScanStatus::InvalidPattern);

  // Malformed tokens.
  CHECK_FALSE(sf::hooks::parse_pattern("   \t\n ").has_value());
  CHECK_FALSE(sf::hooks::parse_pattern("48 8B 1G").has_value());
  CHECK_FALSE(sf::hooks::parse_pattern("48 8B 0").has_value());
  CHECK_FALSE(sf::hooks::parse_pattern("???").has_value());

  // 64 KB buffer stress check matching at exact final byte and index 0.
  std::vector<std::byte> large(65536, std::byte{0x90});
  large[0] = std::byte{0xE9};
  large[large.size() - 2] = std::byte{0xDE};
  large[large.size() - 1] = std::byte{0xAD};

  const auto start_hit = sf::hooks::sigscan(large, "E9 90");
  CHECK(start_hit.status == sf::hooks::ScanStatus::Found);
  CHECK(start_hit.offset == 0);

  const auto end_hit = sf::hooks::sigscan(large, "DE AD");
  CHECK(end_hit.status == sf::hooks::ScanStatus::Found);
  CHECK(end_hit.offset == large.size() - 2);
}
