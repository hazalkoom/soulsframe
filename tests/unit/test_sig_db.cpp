#include <doctest/doctest.h>

#include "games/ds3/profile.h"
#include "games/ds3/sig_db.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

TEST_CASE("sig_db parses builds, normalizes hashes, and validates signatures") {
  constexpr const char* kToml = R"(
[build."ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"]
version = "1.15.2"
verified = true

[[build."ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789".sig]]
name = "present_caller"
pattern = "48 8B ? ? 48 85 C0"
offset = 2
kind = "code"
confidence = "verified"
notes = "main swapchain wrapper"

[[build."ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789".sig]]
name = "frame_dt_global"
pattern = "F3 0F 10 05 ? ? ? ?"
offset = 0
disp_offset = 4
insn_len = 8
kind = "rip_rel_data"
confidence = "tested"
)";

  const auto parsed = sf::games::ds3::parse_sig_db_toml(kToml);
  REQUIRE(parsed.parsed_ok);
  CHECK(parsed.warnings.empty());
  REQUIRE(parsed.db.builds.size() == 1);

  const auto* build =
      parsed.db.find_build("abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
  REQUIRE(build != nullptr);
  CHECK(build->version == "1.15.2");
  CHECK(build->verified);
  REQUIRE(build->signatures.size() == 2);
  CHECK(build->signatures[0].kind == sf::games::ds3::SigKind::Code);
  CHECK(build->signatures[0].confidence == sf::games::ds3::SigConfidence::Verified);
  CHECK(build->signatures[1].kind == sf::games::ds3::SigKind::RipRelData);
  CHECK(build->signatures[1].disp_offset == 4);
  CHECK(build->signatures[1].insn_len == 8);
  CHECK(build->signatures[1].confidence == sf::games::ds3::SigConfidence::Tested);

  const auto prof_ok = sf::games::ds3::resolve_profile(
      "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789", parsed.db);
  CHECK(prof_ok.engine_hooks_allowed);

  const auto prof_unknown = sf::games::ds3::resolve_profile("deadbeef", parsed.db);
  CHECK_FALSE(prof_unknown.engine_hooks_allowed);
}

TEST_CASE("sig_db resolves code and signed RIP-relative targets safely") {
  using namespace sf::games::ds3;
  // Synthetic 64-byte image:
  // At RVA 0x10: `F3 0F 10 05 <disp32=0x00000018>` (len 8, next RIP = 0x18, target = 0x18+0x18 =
  // 0x30) At RVA 0x20: `48 89 5C 24 ?` (len 5)
  std::vector<std::byte> img(64, std::byte{0x90});
  img[0x10] = std::byte{0xF3};
  img[0x11] = std::byte{0x0F};
  img[0x12] = std::byte{0x10};
  img[0x13] = std::byte{0x05};
  img[0x14] = std::byte{0x18}; // +0x18 LE
  img[0x15] = std::byte{0x00};
  img[0x16] = std::byte{0x00};
  img[0x17] = std::byte{0x00};

  img[0x20] = std::byte{0x48};
  img[0x21] = std::byte{0x89};
  img[0x22] = std::byte{0x5C};
  img[0x23] = std::byte{0x24};
  img[0x24] = std::byte{0x08};

  SignatureEntry rip_entry{};
  rip_entry.name = "dt_read";
  rip_entry.pattern = "F3 0F 10 05 ? ? ? ?";
  rip_entry.offset = 0;
  rip_entry.disp_offset = 4;
  rip_entry.insn_len = 8;
  rip_entry.kind = SigKind::RipRelData;
  const auto res_rip = resolve_signature_target(img, 0x140000000ULL, rip_entry);
  REQUIRE(res_rip.status == ResolveStatus::Resolved);
  CHECK(res_rip.match_offset == 0x10);
  CHECK(res_rip.rva == 0x30);
  CHECK(res_rip.va == 0x140000030ULL);

  // Also verify negative RIP displacement resolves backwards accurately (`-0x10` -> 0x18 - 0x10 =
  // 0x08).
  img[0x14] = std::byte{0xF0};
  img[0x15] = std::byte{0xFF};
  img[0x16] = std::byte{0xFF};
  img[0x17] = std::byte{0xFF};
  const auto res_neg = resolve_signature_target(img, 0x140000000ULL, rip_entry);
  REQUIRE(res_neg.status == ResolveStatus::Resolved);
  CHECK(res_neg.rva == 0x08);
  CHECK(res_neg.va == 0x140000008ULL);

  SignatureEntry code_entry{};
  code_entry.name = "prologue";
  code_entry.pattern = "48 89 5C 24 ?";
  code_entry.offset = 1;
  code_entry.kind = SigKind::Code;
  const auto res_code = resolve_signature_target(img, 0x140000000ULL, code_entry);
  REQUIRE(res_code.status == ResolveStatus::Resolved);
  CHECK(res_code.match_offset == 0x20);
  CHECK(res_code.rva == 0x21);

  // Out-of-bounds target rejected
  SignatureEntry oob_entry = rip_entry;
  oob_entry.offset = 500;
  CHECK(resolve_signature_target(img, 0x140000000ULL, oob_entry).status ==
        ResolveStatus::OutOfBounds);

  // Ambiguous pattern rejected (NOP 0x90 repeats everywhere)
  SignatureEntry ambig_entry{};
  ambig_entry.name = "nop";
  ambig_entry.pattern = "90 90";
  ambig_entry.kind = SigKind::Code;
  CHECK(resolve_signature_target(img, 0, ambig_entry).status == ResolveStatus::Ambiguous);
}

TEST_CASE("sig_db handles invalid TOML, invalid patterns, and missing files safely") {
  const auto bad_toml = sf::games::ds3::parse_sig_db_toml("[[[broken");
  CHECK_FALSE(bad_toml.parsed_ok);
  CHECK_FALSE(bad_toml.warnings.empty());

  const auto bad_bytes = sf::games::ds3::parse_sig_db_toml(std::string("a\x01", 2));
  CHECK_FALSE(bad_bytes.parsed_ok);

  const auto bad_sig = sf::games::ds3::parse_sig_db_toml(R"(
[build."abcd"]
verified = false

[[build."abcd".sig]]
name = "broken_pat"
pattern = "ZZ GG"
kind = "mystery"
confidence = "fake"
)");
  CHECK(bad_sig.parsed_ok);
  CHECK_FALSE(bad_sig.warnings.empty());
  const auto* b = bad_sig.db.find_build("abcd");
  REQUIRE(b != nullptr);
  CHECK_FALSE(b->verified);
  CHECK(b->signatures.empty());
  CHECK_FALSE(sf::games::ds3::resolve_profile("abcd", bad_sig.db).engine_hooks_allowed);

  const auto missing = sf::games::ds3::load_sig_db_file("/nonexistent/ds3_sigs_missing.toml");
  CHECK_FALSE(missing.parsed_ok);
}

TEST_CASE("sig_db rejects missing fields, invalid hex pairs, and out-of-range numeric fields") {
  using namespace sf::games::ds3;

  const auto missing_fields = parse_sig_db_toml(R"(
[build."0123456789abcdef"]
verified = false

[[build."0123456789abcdef".sig]]
name = "missing_pattern"

[[build."0123456789abcdef".sig]]
pattern = "48 8B 05"
name = ""

[[build."0123456789abcdef".sig]]
name = "bad_hex"
pattern = "48 8G 05"
)");
  CHECK(missing_fields.parsed_ok);
  CHECK_FALSE(missing_fields.warnings.empty());
  const auto* missing_build = missing_fields.db.find_build("0123456789abcdef");
  REQUIRE(missing_build != nullptr);
  CHECK(missing_build->signatures.empty());

  const auto numeric_bounds = parse_sig_db_toml(R"(
[build."fedcba9876543210"]
verified = false

[[build."fedcba9876543210".sig]]
name = "bounds"
pattern = "48 8B 05"
disp_offset = 100000
insn_len = 100000
)");
  CHECK(numeric_bounds.parsed_ok);
  CHECK_FALSE(numeric_bounds.warnings.empty());
  const auto* bounds_build = numeric_bounds.db.find_build("fedcba9876543210");
  REQUIRE(bounds_build != nullptr);
  REQUIRE(bounds_build->signatures.size() == 1);
  CHECK(bounds_build->signatures[0].disp_offset == 3U);
  CHECK(bounds_build->signatures[0].insn_len == 7U);

  const auto negative_bounds = parse_sig_db_toml(R"(
[build."aabbccdd"]
verified = false

[[build."aabbccdd".sig]]
name = "negative"
pattern = "48 8B 05"
disp_offset = -4
insn_len = 2
)");
  CHECK(negative_bounds.parsed_ok);
  const auto* negative_build = negative_bounds.db.find_build("aabbccdd");
  REQUIRE(negative_build != nullptr);
  REQUIRE(negative_build->signatures.size() == 1);
  CHECK(negative_build->signatures[0].disp_offset == 3U);
  CHECK(negative_build->signatures[0].insn_len == 7U);
}

TEST_CASE("sig_db resolves targets with extreme offsets and RIP lengths safely") {
  using namespace sf::games::ds3;

  std::vector<std::byte> img(16, std::byte{0x90});

  SignatureEntry entry{};
  entry.name = "extreme";
  entry.pattern = "90";
  entry.offset = 0x7FFF'FFFF'FFFF'FFFFLL;
  const auto huge_offset = resolve_signature_target(img, 0x140000000ULL, entry);
  CHECK_FALSE(huge_offset.status == ResolveStatus::Resolved);

  entry.kind = SigKind::RipRelData;
  entry.offset = 0;
  entry.disp_offset = 0xFFFFU;
  entry.insn_len = 0xFFFFU;
  const auto huge_rip = resolve_signature_target(img, 0x140000000ULL, entry);
  CHECK_FALSE(huge_rip.status == ResolveStatus::Resolved);

  entry.disp_offset = 0;
  entry.insn_len = 8;
  entry.offset = 500;
  const auto out_of_range = resolve_signature_target(img, 0x140000000ULL, entry);
  CHECK_FALSE(out_of_range.status == ResolveStatus::Resolved);
}
