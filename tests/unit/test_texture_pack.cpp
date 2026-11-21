#include "tex/texture_pack.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <limits>
#include <vector>

using namespace sf::tex;

namespace {

void put32(std::vector<std::byte>& f, size_t off, uint32_t v) {
  std::memcpy(f.data() + off, &v, sizeof(v));
}

/// Builds a minimal DDS image; `fourcc == 0` produces an RGB32 legacy header.
std::vector<std::byte> make_dds(uint32_t w, uint32_t h, uint32_t mips, uint32_t fourcc,
                                uint32_t dx10_format, uint64_t payload) {
  const bool dx10 = dx10_format != 0U;
  const size_t header = dx10 ? 148U : 128U;
  std::vector<std::byte> f(header + payload);
  put32(f, 0, 0x20534444U);
  put32(f, 4, 124U);
  put32(f, 12, h);
  put32(f, 16, w);
  put32(f, 28, mips);
  put32(f, 76, 32U);
  if (dx10) {
    put32(f, 80, 0x4U);
    put32(f, 84, 0x30315844U); // "DX10"
    put32(f, 128, dx10_format);
    put32(f, 132, 3U);
    put32(f, 140, 1U);
  } else if (fourcc != 0U) {
    put32(f, 80, 0x4U);
    put32(f, 84, fourcc);
  } else {
    put32(f, 80, 0x40U);
    put32(f, 88, 32U);
    put32(f, 92, 0x00FF0000U);
  }
  return f;
}

constexpr uint32_t kDxt1 = 0x31545844U;
constexpr uint32_t kDxt5 = 0x35545844U;

} // namespace

TEST_CASE("format helpers compute BC and RGBA sizes") {
  CHECK(bc_block_bytes(71U) == 8U);
  CHECK(bc_block_bytes(98U) == 16U);
  CHECK(pixel_bytes(28U) == 4U);
  CHECK_FALSE(is_supported_format(2U));
  CHECK(mip_row_count(71U, 6U) == 2U);
  CHECK(mip_row_count(28U, 6U) == 6U);
  CHECK(mip_row_pitch(71U, 5U) == 16U);
  CHECK(mip_row_pitch(87U, 5U) == 20U);
  // 8x8 BC1: 32 + 8 + 8 (2x2 and 1x1 still occupy one block).
  CHECK(mip_chain_bytes(71U, 8U, 8U, 4U) == 32U + 8U + 8U + 8U);
  CHECK(mip_chain_bytes(28U, 4U, 2U, 3U) == 32U + 8U + 4U);
}

TEST_CASE("texture key depends on bytes and descriptor") {
  const std::vector<std::byte> a(64, std::byte{1});
  const std::vector<std::byte> b(64, std::byte{2});
  const TextureDesc d1{.width = 8, .height = 8, .mip_levels = 1, .array_size = 1, .format = 71};
  TextureDesc d2 = d1;
  d2.format = 77;
  CHECK(compute_texture_key(d1, a) == compute_texture_key(d1, a));
  CHECK(compute_texture_key(d1, a) != compute_texture_key(d1, b));
  CHECK(compute_texture_key(d1, a) != compute_texture_key(d2, a));
  // Mip count is excluded so a pack with a fuller mip chain still matches.
  TextureDesc d3 = d1;
  d3.mip_levels = 4;
  CHECK(compute_texture_key(d1, a) == compute_texture_key(d3, a));
}

TEST_CASE("format helpers cover BC2 through BC7 and sRGB DXGI formats") {
  const std::array<uint32_t, 12> kBcFormats = {74U, 75U, 77U, 78U, 80U, 81U,
                                               83U, 84U, 95U, 96U, 98U, 99U};
  for (const uint32_t fmt : kBcFormats) {
    CHECK(is_supported_format(fmt));
    CHECK(bc_block_bytes(fmt) != 0U);
  }
  CHECK(bc_block_bytes(71U) == 8U);
  CHECK(bc_block_bytes(72U) == 8U);
  CHECK(pixel_bytes(29U) == 4U);
  CHECK(pixel_bytes(91U) == 4U);

  CHECK(mip_row_count(74U, 5U) == 2U);
  CHECK(mip_row_pitch(98U, 5U) == 32U);
  CHECK(mip_row_pitch(28U, 5U) == 20U);
}

TEST_CASE("mip chains handle odd dimensions and 0/15/16 mip boundaries") {
  CHECK(mip_chain_bytes(71U, 1U, 1U, 0U) == 0U);
  CHECK(mip_chain_bytes(71U, 1U, 1U, 1U) == 8U);
  CHECK(mip_chain_bytes(71U, 5U, 3U, 1U) == 16U);

  const uint64_t mips15 = mip_chain_bytes(28U, 3U, 5U, 15U);
  CHECK(mips15 > 0U);

  auto bc_mips0 = make_dds(4U, 4U, 0U, kDxt1, 0U, 8U);
  const auto parsed0 = parse_dds(bc_mips0);
  CHECK(parsed0.ok);
  CHECK(parsed0.mip_levels == 1U);

  CHECK_FALSE(parse_dds(make_dds(4U, 4U, 16U, kDxt1, 0U, 4096U)).ok);
}

TEST_CASE("DDS DX10 covers BC2/3/4/5/6H/7 and sRGB formats") {
  const std::array<uint32_t, 16> kFormats = {74U, 75U, 77U, 78U, 80U, 81U, 83U, 84U,
                                             95U, 96U, 98U, 99U, 29U, 28U, 91U, 87U};
  for (const uint32_t fmt : kFormats) {
    const uint64_t payload = mip_chain_bytes(fmt, 4U, 4U, 1U);
    const auto dds = parse_dds(make_dds(4U, 4U, 1U, 0U, fmt, payload));
    CHECK(dds.ok);
    CHECK(dds.format == fmt);
  }
}

TEST_CASE("texture key is deterministic and budget arithmetic rejects overflow") {
  const std::vector<std::byte> pixels(16, std::byte{0xA5});
  const TextureDesc desc{
      .width = 4U, .height = 4U, .mip_levels = 1U, .array_size = 1U, .format = 98U};
  const uint64_t first = compute_texture_key(desc, pixels);
  CHECK(first == compute_texture_key(desc, pixels));
  CHECK(
      first !=
      compute_texture_key(
          TextureDesc{.width = 4U, .height = 4U, .mip_levels = 1U, .array_size = 1U, .format = 99U},
          pixels));

  TextureSwapPolicy unlimited;
  CHECK(unlimited.try_reserve(std::numeric_limits<uint64_t>::max()));
  CHECK_FALSE(unlimited.try_reserve(1U));
  CHECK(unlimited.stats().over_budget == 1U);

  TextureSwapPolicy limited(std::numeric_limits<uint64_t>::max());
  CHECK(limited.try_reserve(std::numeric_limits<uint64_t>::max()));
  CHECK_FALSE(limited.try_reserve(1U));
  CHECK(limited.stats().over_budget == 1U);
}

TEST_CASE("parse_dds accepts legacy, RGB32 and DX10 headers") {
  auto dxt1 = parse_dds(make_dds(8, 8, 1, kDxt1, 0, 32));
  CHECK(dxt1.ok);
  CHECK(dxt1.format == 71U);
  CHECK(dxt1.data_offset == 128U);

  auto dxt5 = parse_dds(make_dds(4, 4, 0, kDxt5, 0, 16));
  CHECK(dxt5.ok);
  CHECK(dxt5.mip_levels == 1U);
  CHECK(dxt5.format == 77U);

  auto rgb = parse_dds(make_dds(2, 2, 1, 0, 0, 16));
  CHECK(rgb.ok);
  CHECK(rgb.format == 87U);

  auto bc7 = parse_dds(make_dds(8, 8, 2, 0, 98U, 64 + 16));
  CHECK(bc7.ok);
  CHECK(bc7.format == 98U);
  CHECK(bc7.data_offset == 148U);
  CHECK(bc7.data_bytes == 80U);
}

TEST_CASE("parse_dds rejects malformed files") {
  CHECK_FALSE(parse_dds({}).ok);
  auto bad_magic = make_dds(8, 8, 1, kDxt1, 0, 32);
  bad_magic[0] = std::byte{0};
  CHECK_FALSE(parse_dds(bad_magic).ok);
  CHECK_FALSE(parse_dds(make_dds(8, 8, 1, kDxt1, 0, 31)).ok);    // truncated payload
  CHECK_FALSE(parse_dds(make_dds(0, 8, 1, kDxt1, 0, 32)).ok);    // zero width
  CHECK_FALSE(parse_dds(make_dds(32768, 8, 1, kDxt1, 0, 0)).ok); // too large
  CHECK_FALSE(parse_dds(make_dds(8, 8, 1, 0x12345678U, 0, 64)).ok);
  CHECK_FALSE(parse_dds(make_dds(8, 8, 16, kDxt1, 0, 4096)).ok); // too many mips
  auto cube = make_dds(8, 8, 1, kDxt1, 0, 32);
  put32(cube, 112, 0x200U);
  CHECK_FALSE(parse_dds(cube).ok);
  auto arr = make_dds(8, 8, 1, 0, 98U, 64);
  put32(arr, 140, 6U);
  CHECK_FALSE(parse_dds(arr).ok);
  auto trunc10 = make_dds(8, 8, 1, 0, 98U, 64);
  trunc10.resize(140);
  CHECK_FALSE(parse_dds(trunc10).ok);
}

TEST_CASE("is_safe_relative_path blocks traversal and absolute paths") {
  CHECK(is_safe_relative_path("chr/c1000.dds"));
  CHECK(is_safe_relative_path("a..b.dds"));
  CHECK_FALSE(is_safe_relative_path(""));
  CHECK_FALSE(is_safe_relative_path("../x.dds"));
  CHECK_FALSE(is_safe_relative_path("a\\..\\x.dds"));
  CHECK_FALSE(is_safe_relative_path("a/.."));
  CHECK_FALSE(is_safe_relative_path("/etc/x"));
  CHECK_FALSE(is_safe_relative_path("\\x"));
  CHECK_FALSE(is_safe_relative_path("C:x.dds"));
  CHECK_FALSE(is_safe_relative_path(std::string_view("a\x01.dds")));
}

TEST_CASE("parse_manifest reads entries and warns on bad lines") {
  const auto m = parse_manifest("# pack\n"
                                "00000000000000ff = a.dds  # comment\r\n"
                                "\n"
                                "nonsense\n"
                                "123 = short.dds\n"
                                "zzzzzzzzzzzzzzzz = bad.dds\n"
                                "00000000000000AA = ../escape.dds\n"
                                "00000000000000ff = b.dds");
  REQUIRE(m.entries.size() == 1U);
  CHECK(m.entries.at(0xffU) == "b.dds");
  CHECK(m.warnings.size() == 4U);
  CHECK(key_hex(0xffU) == "00000000000000ff");
}

TEST_CASE("TextureSwapPolicy enforces the byte budget") {
  TextureSwapPolicy p(100);
  p.note_observed();
  CHECK(p.try_reserve(60));
  CHECK_FALSE(p.try_reserve(50));
  CHECK(p.try_reserve(40));
  CHECK_FALSE(p.try_reserve(1000));
  p.note_invalid();
  CHECK(p.stats().observed == 1U);
  CHECK(p.stats().replaced == 2U);
  CHECK(p.stats().over_budget == 2U);
  CHECK(p.stats().invalid == 1U);
  CHECK(p.stats().bytes_loaded == 100U);

  TextureSwapPolicy unlimited;
  CHECK(unlimited.try_reserve(1ULL << 40U));
}

#include "core/config.h"

TEST_CASE("config parses [textures] section and kill switch") {
  const auto r = sf::parse_config_toml("[modules]\ntextures = true\n"
                                       "[textures]\npack_dir = \"hd\"\nmax_pack_mb = 1\n"
                                       "dump_keys = true\nbogus = 1\n");
  CHECK(r.config.modules.textures);
  CHECK(r.config.textures.pack_dir == "hd");
  CHECK(r.config.textures.max_pack_mb == 16U);
  CHECK(r.config.textures.dump_keys);
  CHECK(r.warnings.size() == 2U);
  const auto bad = sf::parse_config_toml("[textures]\npack_dir = \"../x\"\n");
  CHECK(bad.config.textures.pack_dir == "soulsframe_textures");
  CHECK_FALSE(sf::Config::defaults().modules.textures);
}
