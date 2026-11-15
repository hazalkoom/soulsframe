#include <doctest/doctest.h>

#include "core/sha256.h"

#include <cstring>
#include <span>
#include <string_view>

namespace {

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
