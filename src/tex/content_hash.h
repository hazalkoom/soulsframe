#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace sf::tex {

/// Computes a fast 64-bit XXH64 content hash over `data` (used for texture/shader keys).
uint64_t hash_bytes(std::span<const std::byte> data, uint64_t seed = 0);

/// Returns `hash_bytes()` formatted as a fixed 16-character lowercase hex string.
std::string hash_hex(std::span<const std::byte> data, uint64_t seed = 0);

} // namespace sf::tex
