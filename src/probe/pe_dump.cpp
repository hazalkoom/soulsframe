#include "probe/pe_dump.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace sf::probe {

namespace {

constexpr uint16_t kDosMagic = 0x5A4D;        // "MZ"
constexpr uint32_t kPeSignature = 0x00004550; // "PE\0\0"
constexpr uint16_t kPe32PlusMagic = 0x020B;
constexpr uint32_t kDosElfanewOffset = 0x3C;
constexpr uint32_t kCoffHeaderSize = 20;
constexpr uint32_t kMinOptionalHeaderSize = 112;
constexpr uint32_t kSectionHeaderSize = 40;
constexpr uint16_t kMaxReasonableSections = 96;

bool has_range(std::span<const std::byte> buf, size_t offset, size_t len) noexcept {
  return len <= buf.size() && offset <= buf.size() - len;
}

uint16_t read_u16(std::span<const std::byte> buf, size_t offset) noexcept {
  const auto b0 = std::to_integer<uint16_t>(buf[offset]);
  const auto b1 = std::to_integer<uint16_t>(buf[offset + 1U]);
  return static_cast<uint16_t>(b0 | (b1 << 8U));
}

uint32_t read_u32(std::span<const std::byte> buf, size_t offset) noexcept {
  const auto b0 = std::to_integer<uint32_t>(buf[offset]);
  const auto b1 = std::to_integer<uint32_t>(buf[offset + 1U]);
  const auto b2 = std::to_integer<uint32_t>(buf[offset + 2U]);
  const auto b3 = std::to_integer<uint32_t>(buf[offset + 3U]);
  return b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
}

uint64_t read_u64(std::span<const std::byte> buf, size_t offset) noexcept {
  const uint64_t lo = read_u32(buf, offset);
  const uint64_t hi = read_u32(buf, offset + 4U);
  return lo | (hi << 32U);
}

void write_u32(std::span<std::byte> buf, size_t offset, uint32_t val) noexcept {
  buf[offset + 0U] = static_cast<std::byte>(val & 0xFFU);
  buf[offset + 1U] = static_cast<std::byte>((val >> 8U) & 0xFFU);
  buf[offset + 2U] = static_cast<std::byte>((val >> 16U) & 0xFFU);
  buf[offset + 3U] = static_cast<std::byte>((val >> 24U) & 0xFFU);
}

void write_u64(std::span<std::byte> buf, size_t offset, uint64_t val) noexcept {
  write_u32(buf, offset, static_cast<uint32_t>(val & 0xFFFFFFFFULL));
  write_u32(buf, offset + 4U, static_cast<uint32_t>(val >> 32U));
}

std::string read_section_name(std::span<const std::byte> buf, size_t offset) {
  std::string name;
  name.reserve(8);
  for (size_t i = 0; i < 8; ++i) {
    const char ch = std::to_integer<char>(buf[offset + i]);
    if (ch == '\0') {
      break;
    }
    name.push_back(ch);
  }
  return name;
}

} // namespace

PeImageInfo inspect_pe64(std::span<const std::byte> image) {
  PeImageInfo info;
  if (!has_range(image, 0, kDosElfanewOffset + 4U)) {
    info.error = "buffer smaller than DOS header";
    return info;
  }
  if (read_u16(image, 0) != kDosMagic) {
    info.error = "missing MZ signature";
    return info;
  }

  const uint32_t nt_off = read_u32(image, kDosElfanewOffset);
  if (!has_range(image, nt_off, 4U + kCoffHeaderSize)) {
    info.error = "PE header offset out of bounds";
    return info;
  }
  if (read_u32(image, nt_off) != kPeSignature) {
    info.error = "missing PE signature";
    return info;
  }

  const size_t coff_off = static_cast<size_t>(nt_off) + 4U;
  const uint16_t num_sections = read_u16(image, coff_off + 2U);
  const uint16_t opt_size = read_u16(image, coff_off + 16U);
  const size_t opt_off = coff_off + kCoffHeaderSize;

  if (opt_size < kMinOptionalHeaderSize || !has_range(image, opt_off, opt_size)) {
    info.error = "PE optional header truncated or too small";
    return info;
  }
  if (read_u16(image, opt_off) != kPe32PlusMagic) {
    info.error = "not a PE32+ (64-bit) image";
    return info;
  }
  if (num_sections > kMaxReasonableSections) {
    info.error = "section count exceeds safety cap";
    return info;
  }

  const size_t sec_table_off = opt_off + opt_size;
  const size_t sec_table_bytes = static_cast<size_t>(num_sections) * kSectionHeaderSize;
  if (!has_range(image, sec_table_off, sec_table_bytes)) {
    info.error = "section table extends past buffer";
    return info;
  }

  info.image_base_field_offset = static_cast<uint32_t>(opt_off + 24U);
  info.image_base = read_u64(image, info.image_base_field_offset);
  info.entry_point_rva = read_u32(image, opt_off + 16U);
  info.size_of_image = read_u32(image, opt_off + 56U);
  info.size_of_headers = read_u32(image, opt_off + 60U);
  info.section_table_offset = static_cast<uint32_t>(sec_table_off);

  info.sections.reserve(num_sections);
  for (uint16_t i = 0; i < num_sections; ++i) {
    const size_t s_off = sec_table_off + (static_cast<size_t>(i) * kSectionHeaderSize);
    PeSectionInfo sec;
    sec.name = read_section_name(image, s_off);
    sec.virtual_size = read_u32(image, s_off + 8U);
    sec.virtual_address = read_u32(image, s_off + 12U);
    sec.raw_size = read_u32(image, s_off + 16U);
    sec.raw_offset = read_u32(image, s_off + 20U);
    sec.characteristics = read_u32(image, s_off + 36U);
    info.sections.push_back(std::move(sec));
  }

  info.valid = true;
  return info;
}

PeFixupResult fixup_pe_dump_for_ghidra(std::span<std::byte> dump_bytes,
                                       uint64_t runtime_image_base) {
  PeFixupResult res;
  const PeImageInfo info = inspect_pe64(dump_bytes);
  if (!info.valid) {
    res.error = info.error;
    return res;
  }

  uint64_t effective_base = info.image_base;
  if (runtime_image_base != 0) {
    write_u64(dump_bytes, info.image_base_field_offset, runtime_image_base);
    effective_base = runtime_image_base;
  }

  const size_t sec_count = info.sections.size();
  for (size_t i = 0; i < sec_count; ++i) {
    const auto& sec = info.sections[i];
    const size_t s_off = static_cast<size_t>(info.section_table_offset) + (i * kSectionHeaderSize);

    const uint32_t desired_vsize = sec.virtual_size > 0 ? sec.virtual_size : sec.raw_size;
    if (sec.virtual_address >= dump_bytes.size()) {
      write_u32(dump_bytes, s_off + 16U, 0);
      write_u32(dump_bytes, s_off + 20U, 0);
    } else {
      const auto max_avail = static_cast<uint32_t>(dump_bytes.size() - sec.virtual_address);
      const uint32_t clamped_size = std::min(desired_vsize, max_avail);
      write_u32(dump_bytes, s_off + 16U, clamped_size);
      write_u32(dump_bytes, s_off + 20U, sec.virtual_address);
    }
    ++res.sections_fixed;
  }

  res.ok = true;
  res.image_base = effective_base;
  return res;
}

} // namespace sf::probe
