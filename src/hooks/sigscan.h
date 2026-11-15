#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace sf::hooks {

/// Single byte slot within an IDA-style signature pattern.
struct PatternByte {
  uint8_t value = 0;
  bool wildcard = false;
};

using ParsedPattern = std::vector<PatternByte>;

/// Classification returned by `sigscan()`.
enum class ScanStatus {
  Found,          ///< Exactly one unique match found in the scanned memory image.
  NotFound,       ///< Zero matches found.
  Ambiguous,      ///< Two or more matches found (refused to prevent hooking the wrong site).
  InvalidPattern, ///< Pattern string was empty or contained invalid hex tokens.
};

/// Result of scanning a byte buffer for a signature pattern.
struct ScanResult {
  ScanStatus status = ScanStatus::NotFound;
  size_t offset = 0;      ///< Byte offset of the first match when Found/Ambiguous.
  size_t match_count = 0; ///< Total number of matches discovered across the image.
};

/// Parses a whitespace-delimited IDA-style pattern such as `"48 8B ? ? 89"`.
/// Accepts `"?"` or `"??"` as wildcards. Returns `std::nullopt` on invalid syntax.
std::optional<ParsedPattern> parse_pattern(std::string_view pattern_str);

/// Scans `image` for a pre-parsed `pattern`.
ScanResult sigscan(std::span<const std::byte> image, const ParsedPattern& pattern);

/// Parses `pattern_str` and scans `image` in one call.
ScanResult sigscan(std::span<const std::byte> image, std::string_view pattern_str);

} // namespace sf::hooks
