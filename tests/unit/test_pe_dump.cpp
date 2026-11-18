#include <doctest/doctest.h>

#include "probe/pe_dump.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

void put_u16(std::vector<std::byte>& buf, size_t off, uint16_t val) {
  buf[off] = static_cast<std::byte>(val & 0xFFU);
  buf[off + 1U] = static_cast<std::byte>((val >> 8U) & 0xFFU);
}

void put_u32(std::vector<std::byte>& buf, size_t off, uint32_t val) {
  buf[off + 0U] = static_cast<std::byte>(val & 0xFFU);
  buf[off + 1U] = static_cast<std::byte>((val >> 8U) & 0xFFU);
  buf[off + 2U] = static_cast<std::byte>((val >> 16U) & 0xFFU);
  buf[off + 3U] = static_cast<std::byte>((val >> 24U) & 0xFFU);
}

void put_u64(std::vector<std::byte>& buf, size_t off, uint64_t val) {
  put_u32(buf, off, static_cast<uint32_t>(val & 0xFFFFFFFFULL));
  put_u32(buf, off + 4U, static_cast<uint32_t>(val >> 32U));
}

void write_section(std::vector<std::byte>& buf, size_t off, const char* name, uint32_t vsize,
                   uint32_t vaddr, uint32_t rsize, uint32_t roff, uint32_t chars) {
  for (size_t i = 0; i < 8 && name[i] != '\0'; ++i) {
    buf[off + i] = static_cast<std::byte>(name[i]);
  }
  put_u32(buf, off + 8U, vsize);
  put_u32(buf, off + 12U, vaddr);
  put_u32(buf, off + 16U, rsize);
  put_u32(buf, off + 20U, roff);
  put_u32(buf, off + 36U, chars);
}

/// Builds a minimal synthetic 64-bit PE image layout in memory.
std::vector<std::byte> make_synthetic_pe64() {
  constexpr size_t kImageSize = 0x3000;
  std::vector<std::byte> img(kImageSize, std::byte{0});

  // DOS header
  put_u16(img, 0, 0x5A4D);  // MZ
  put_u32(img, 0x3C, 0x80); // e_lfanew

  // NT headers @ 0x80
  put_u32(img, 0x80, 0x00004550); // PE\0\0
  // COFF FileHeader @ 0x84 (20 bytes)
  put_u16(img, 0x84 + 2U, 2);    // NumberOfSections = 2
  put_u16(img, 0x84 + 16U, 240); // SizeOfOptionalHeader = 240 (PE32+ standard)

  // OptionalHeader @ 0x98
  constexpr size_t kOpt = 0x84 + 20;
  put_u16(img, kOpt, 0x020B);               // PE32+
  put_u32(img, kOpt + 16U, 0x1050);         // AddressOfEntryPoint
  put_u64(img, kOpt + 24U, 0x140000000ULL); // ImageBase
  put_u32(img, kOpt + 56U, kImageSize);     // SizeOfImage
  put_u32(img, kOpt + 60U, 0x400);          // SizeOfHeaders

  // Section table @ kOpt + 240 = 0x188
  constexpr size_t kSec = kOpt + 240;
  write_section(img, kSec, ".text", 0xE00, 0x1000, 0x600, 0x400, 0x60000020U);
  write_section(img, kSec + 40U, ".rdata", 0x800, 0x2000, 0x400, 0xA00, 0x40000040U);

  return img;
}

} // namespace

TEST_CASE("pe_dump inspect_pe64 parses valid synthetic PE32+ headers") {
  const auto pe = make_synthetic_pe64();
  const auto info = sf::probe::inspect_pe64(pe);
  REQUIRE(info.valid);
  CHECK(info.image_base == 0x140000000ULL);
  CHECK(info.entry_point_rva == 0x1050);
  CHECK(info.size_of_image == 0x3000);
  CHECK(info.size_of_headers == 0x400);
  REQUIRE(info.sections.size() == 2);
  CHECK(info.sections[0].name == ".text");
  CHECK(info.sections[0].virtual_address == 0x1000);
  CHECK(info.sections[0].raw_offset == 0x400);
  CHECK(info.sections[1].name == ".rdata");
}

TEST_CASE("pe_dump fixup_pe_dump_for_ghidra aligns raw offsets to virtual offsets") {
  auto pe = make_synthetic_pe64();
  const auto res = sf::probe::fixup_pe_dump_for_ghidra(pe, 0x7FF612340000ULL);
  REQUIRE(res.ok);
  CHECK(res.sections_fixed == 2);
  CHECK(res.image_base == 0x7FF612340000ULL);

  const auto updated = sf::probe::inspect_pe64(pe);
  REQUIRE(updated.valid);
  CHECK(updated.image_base == 0x7FF612340000ULL);
  CHECK(updated.sections[0].raw_offset == 0x1000);
  CHECK(updated.sections[0].raw_size == 0xE00);
  CHECK(updated.sections[1].raw_offset == 0x2000);
  CHECK(updated.sections[1].raw_size == 0x800);
}

TEST_CASE("pe_dump rejects malformed headers, 32-bit PE32, and truncated buffers") {
  std::vector<std::byte> empty;
  CHECK_FALSE(sf::probe::inspect_pe64(empty).valid);

  auto bad_mz = make_synthetic_pe64();
  bad_mz[0] = std::byte{0};
  CHECK_FALSE(sf::probe::inspect_pe64(bad_mz).valid);

  auto bad_lfanew = make_synthetic_pe64();
  put_u32(bad_lfanew, 0x3C, 0xFFFFFFF0U);
  CHECK_FALSE(sf::probe::inspect_pe64(bad_lfanew).valid);

  auto bad_pe_sig = make_synthetic_pe64();
  put_u32(bad_pe_sig, 0x80, 0xDEAD0000U);
  CHECK_FALSE(sf::probe::inspect_pe64(bad_pe_sig).valid);

  auto pe32_only = make_synthetic_pe64();
  put_u16(pe32_only, 0x84 + 20, 0x010B); // 32-bit PE magic
  CHECK_FALSE(sf::probe::inspect_pe64(pe32_only).valid);

  auto too_many_secs = make_synthetic_pe64();
  put_u16(too_many_secs, 0x84 + 2U, 200);
  CHECK_FALSE(sf::probe::inspect_pe64(too_many_secs).valid);

  auto fix_err = sf::probe::fixup_pe_dump_for_ghidra(bad_mz);
  CHECK_FALSE(fix_err.ok);
}
