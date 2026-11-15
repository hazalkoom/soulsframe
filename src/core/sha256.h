#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace sf {

/// Computes the 32-byte FIPS 180-4 SHA-256 digest of `data`.
/// Implemented in portable C++20 so executable hashing needs no extra Win32 DLL imports.
std::array<uint8_t, 32> sha256_bytes(std::span<const std::byte> data);

/// Computes the lowercase 64-character hexadecimal SHA-256 digest of `data`.
std::string sha256_hex(std::span<const std::byte> data);

} // namespace sf
