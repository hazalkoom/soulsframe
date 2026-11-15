#include <doctest/doctest.h>

#include "core/sha256.h"

#include <bit>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

/// Convenience wrapper computing lowercase hex SHA-256 of an ASCII string.
std::string sha256_str(std::string_view s) {
  return sf::sha256_hex(
      std::span<const std::byte>(reinterpret_cast<const std::byte*>(s.data()), s.size()));
}

} // namespace

TEST_CASE("sha256 matches FIPS 180-4 / NIST test vectors") {
  CHECK(sha256_str("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha256_str("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(sha256_str("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("sha256 handles padding boundaries, 1MB deterministic stress, and avalanche") {
  // Check exact SHA-256 block-padding boundary lengths (55, 56, 64, 65, 128 bytes)
  // against sha256_bytes() <-> sha256_hex() consistency.
  for (size_t len : {size_t{55}, size_t{56}, size_t{64}, size_t{65}, size_t{128}}) {
    std::vector<std::byte> buf(len, std::byte{0x41});
    const auto raw = sf::sha256_bytes(buf);
    const auto hex = sf::sha256_hex(buf);
    CHECK(hex.size() == 64);

    static constexpr char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < raw.size(); ++i) {
      CHECK(hex[2 * i] == kHex[(raw[i] >> 4) & 0x0F]);
      CHECK(hex[2 * i + 1] == kHex[raw[i] & 0x0F]);
    }
  }

  // NIST vector: 1,000,000 repetitions of 'a' ->
  // cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0
  const std::string one_million_a(1000000, 'a');
  CHECK(sha256_str(one_million_a) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

  // Single-bit flip avalanche: flipping 1 bit must flip ~128 bits (>= 96 required).
  std::vector<std::byte> a(1024, std::byte{0x5A});
  std::vector<std::byte> b = a;
  b[512] ^= std::byte{0x01};
  const auto ha = sf::sha256_bytes(a);
  const auto hb = sf::sha256_bytes(b);
  int diff_bits = 0;
  for (size_t i = 0; i < ha.size(); ++i) {
    diff_bits += std::popcount(static_cast<unsigned>(ha[i] ^ hb[i]));
  }
  CHECK(diff_bits >= 96);
}
