#include "hooks/sigscan.h"

#include <cctype>

namespace sf::hooks {

namespace {

int hex_val(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

} // namespace

std::optional<ParsedPattern> parse_pattern(std::string_view pattern_str) {
  ParsedPattern out;
  size_t i = 0;
  const size_t n = pattern_str.size();

  while (i < n) {
    while (i < n && std::isspace(static_cast<unsigned char>(pattern_str[i])) != 0) {
      ++i;
    }
    if (i >= n) {
      break;
    }

    const size_t start = i;
    while (i < n && std::isspace(static_cast<unsigned char>(pattern_str[i])) == 0) {
      ++i;
    }
    const std::string_view tok = pattern_str.substr(start, i - start);

    if (tok == "?" || tok == "??") {
      out.push_back(PatternByte{.value = 0, .wildcard = true});
    } else if (tok.size() == 2) {
      const int hi = hex_val(tok[0]);
      const int lo = hex_val(tok[1]);
      if (hi < 0 || lo < 0) {
        return std::nullopt;
      }
      out.push_back(PatternByte{.value = static_cast<uint8_t>((hi << 4) | lo), .wildcard = false});
    } else {
      return std::nullopt;
    }
  }

  if (out.empty()) {
    return std::nullopt;
  }
  return out;
}

ScanResult sigscan(std::span<const std::byte> image, const ParsedPattern& pattern) {
  if (pattern.empty()) {
    return {.status = ScanStatus::InvalidPattern, .offset = 0, .match_count = 0};
  }
  if (image.size() < pattern.size()) {
    return {.status = ScanStatus::NotFound, .offset = 0, .match_count = 0};
  }

  size_t first_offset = 0;
  size_t matches = 0;
  const size_t limit = image.size() - pattern.size();

  for (size_t pos = 0; pos <= limit; ++pos) {
    bool ok = true;
    for (size_t j = 0; j < pattern.size(); ++j) {
      if (!pattern[j].wildcard && static_cast<uint8_t>(image[pos + j]) != pattern[j].value) {
        ok = false;
        break;
      }
    }
    if (ok) {
      if (matches == 0) {
        first_offset = pos;
      }
      ++matches;
    }
  }

  if (matches == 0) {
    return {.status = ScanStatus::NotFound, .offset = 0, .match_count = 0};
  }
  if (matches > 1) {
    return {.status = ScanStatus::Ambiguous, .offset = first_offset, .match_count = matches};
  }
  return {.status = ScanStatus::Found, .offset = first_offset, .match_count = 1};
}

ScanResult sigscan(std::span<const std::byte> image, std::string_view pattern_str) {
  const auto parsed = parse_pattern(pattern_str);
  if (!parsed) {
    return {.status = ScanStatus::InvalidPattern, .offset = 0, .match_count = 0};
  }
  return sigscan(image, *parsed);
}

} // namespace sf::hooks
