#include "tex/content_hash.h"

#include <cstdio>
#include <xxhash.h>

namespace sf::tex {

uint64_t hash_bytes(std::span<const std::byte> data, uint64_t seed) {
  return XXH64(data.data(), data.size(), seed);
}

std::string hash_hex(std::span<const std::byte> data, uint64_t seed) {
  const uint64_t h = hash_bytes(data, seed);
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
  return std::string(buf);
}

} // namespace sf::tex
