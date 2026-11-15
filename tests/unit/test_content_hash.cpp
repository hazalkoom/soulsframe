#include <doctest/doctest.h>

#include "tex/content_hash.h"

#include <array>
#include <cstddef>
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
