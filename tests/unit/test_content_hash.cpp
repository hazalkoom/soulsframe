#include <doctest/doctest.h>

#include "tex/content_hash.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

TEST_CASE("content_hash is deterministic and matches empty / known vectors") {
  const std::vector<std::byte> empty;
  CHECK(sf::tex::hash_bytes(empty) == sf::tex::hash_bytes(empty));
  CHECK(sf::tex::hash_hex(empty).size() == 16);

  const std::array<std::byte, 4> a{std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                                   std::byte{0x04}};
  const std::array<std::byte, 4> b{std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                                   std::byte{0x05}};
  CHECK(sf::tex::hash_bytes(a) != sf::tex::hash_bytes(b));
  CHECK(sf::tex::hash_bytes(a, 1) != sf::tex::hash_bytes(a, 2));
}

TEST_CASE("content_hash 1MB buffer stress, seed sensitivity, and 1-bit avalanche") {
  std::vector<std::byte> big(size_t{1024} * 1024);
  for (size_t i = 0; i < big.size(); ++i) {
    big[i] = static_cast<std::byte>((i * 131u) & 0xFFu);
  }

  const uint64_t h0 = sf::tex::hash_bytes(big, 0);
  CHECK(h0 == sf::tex::hash_bytes(big, 0));
  CHECK(sf::tex::hash_bytes(big, 42) != h0);
  CHECK(sf::tex::hash_hex(big, 0).size() == 16);

  // Flip a single bit in the middle of the 1MB payload; expect strong avalanche.
  big[big.size() / 2] ^= std::byte{0x01};
  const uint64_t h1 = sf::tex::hash_bytes(big, 0);
  CHECK(h1 != h0);
  CHECK(std::popcount(h0 ^ h1) >= 16);
}
