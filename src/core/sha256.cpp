#include "core/sha256.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace sf {

namespace {

constexpr std::array<uint32_t, 64> kRound = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

constexpr uint32_t rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32U - n));
}

void compress_block(const uint8_t* block, std::array<uint32_t, 8>& state) {
  std::array<uint32_t, 64> w{};
  for (size_t i = 0; i < 16; ++i) {
    const size_t base = i * 4U;
    w[i] = (static_cast<uint32_t>(block[base]) << 24U) |
           (static_cast<uint32_t>(block[base + 1U]) << 16U) |
           (static_cast<uint32_t>(block[base + 2U]) << 8U) |
           static_cast<uint32_t>(block[base + 3U]);
  }
  for (size_t i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3U);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10U);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];
  uint32_t e = state[4];
  uint32_t f = state[5];
  uint32_t g = state[6];
  uint32_t h = state[7];

  for (size_t i = 0; i < 64; ++i) {
    const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ ((~e) & g);
    const uint32_t temp1 = h + s1 + ch + kRound[i] + w[i];
    const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = s0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

} // namespace

std::array<uint8_t, 32> sha256_bytes(std::span<const std::byte> data) {
  std::array<uint32_t, 8> state = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                   0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

  const size_t full_blocks = data.size() / 64U;
  for (size_t i = 0; i < full_blocks; ++i) {
    compress_block(reinterpret_cast<const uint8_t*>(data.data() + (i * 64U)), state);
  }

  std::array<uint8_t, 128> tail{};
  const size_t rem = data.size() % 64U;
  if (rem > 0) {
    std::memcpy(tail.data(), data.data() + (full_blocks * 64U), rem);
  }
  tail[rem] = 0x80;
  const size_t total_tail_len = (rem < 56U) ? 64U : 128U;

  const uint64_t bit_len = static_cast<uint64_t>(data.size()) * 8ULL;
  for (size_t i = 0; i < 8; ++i) {
    tail[total_tail_len - 1U - i] = static_cast<uint8_t>((bit_len >> (i * 8U)) & 0xffU);
  }

  compress_block(tail.data(), state);
  if (total_tail_len == 128U) {
    compress_block(tail.data() + 64U, state);
  }

  std::array<uint8_t, 32> digest{};
  for (size_t i = 0; i < 8; ++i) {
    const size_t base = i * 4U;
    digest[base] = static_cast<uint8_t>((state[i] >> 24U) & 0xffU);
    digest[base + 1U] = static_cast<uint8_t>((state[i] >> 16U) & 0xffU);
    digest[base + 2U] = static_cast<uint8_t>((state[i] >> 8U) & 0xffU);
    digest[base + 3U] = static_cast<uint8_t>(state[i] & 0xffU);
  }
  return digest;
}

std::string sha256_hex(std::span<const std::byte> data) {
  const auto digest = sha256_bytes(data);
  std::string out(64, '0');
  for (size_t i = 0; i < 32; ++i) {
    std::snprintf(&out[i * 2], 3, "%02x", digest[i]);
  }
  return out;
}

} // namespace sf
