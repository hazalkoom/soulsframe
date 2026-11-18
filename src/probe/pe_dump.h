#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf::probe {

/// Metadata parsed from a single `IMAGE_SECTION_HEADER` (40 bytes).
struct PeSectionInfo {
  std::string name;
  uint32_t virtual_size = 0;
  uint32_t virtual_address = 0;
  uint32_t raw_size = 0;
  uint32_t raw_offset = 0;
  uint32_t characteristics = 0;
};

/// Structural summary extracted from a 64-bit PE (`PE32+`) image or memory dump.
/// Pure C++20: uses explicit byte-span decoding with zero `<windows.h>` dependency.
struct PeImageInfo {
  bool valid = false;
  std::string error;
  uint64_t image_base = 0;
  uint32_t size_of_image = 0;
  uint32_t size_of_headers = 0;
  uint32_t entry_point_rva = 0;
  uint32_t image_base_field_offset = 0;
  uint32_t section_table_offset = 0;
  std::vector<PeSectionInfo> sections;
};

/// Outcome of converting an unaligned runtime memory dump into a Ghidra-loadable PE image.
struct PeFixupResult {
  bool ok = false;
  std::string error;
  uint32_t sections_fixed = 0;
  uint64_t image_base = 0;
};

/// Inspects a 64-bit PE header from an arbitrary byte span without out-of-bounds reads.
PeImageInfo inspect_pe64(std::span<const std::byte> image);

/// Rewrites `PointerToRawData`/`SizeOfRawData` of every section to match `VirtualAddress`/
/// `VirtualSize` (clamped to `dump_bytes.size()`) and updates `OptionalHeader.ImageBase` when
/// `runtime_image_base != 0`, producing a clean unaligned dump ready for headless Ghidra.
PeFixupResult fixup_pe_dump_for_ghidra(std::span<std::byte> dump_bytes,
                                       uint64_t runtime_image_base = 0);

} // namespace sf::probe
