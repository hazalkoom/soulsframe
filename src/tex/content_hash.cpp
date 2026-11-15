#include "tex/content_hash.h"

#include <array>
#include <cstdio>
#include <xxhash.h>

namespace sf::tex {

uint64_t hash_bytes(std::span<const std::byte> data, uint64_t seed) {
  return XXH64(data.data(), data.size(), seed);
}

std::string hash_hex(std::span<const std::byte> data, uint64_t seed) {
  const uint64_t h = hash_bytes(data, seed);
  std::array<char, 17> buf{};
  std::snprintf(buf.data(), buf.size(), "%016llx", static_cast<unsigned long long>(h));
  return {buf.data()};
}

} // namespace sf::tex
