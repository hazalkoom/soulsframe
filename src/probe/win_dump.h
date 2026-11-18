#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sf::probe {

/// Summary of a read-only runtime module memory dump (probe builds only).
struct MemDumpResult {
  bool ok = false;
  std::string error;
  uint64_t image_base = 0;
  size_t bytes_written = 0;
  uint32_t regions_copied = 0;
  uint32_t regions_skipped = 0;
  uint32_t sections_fixed = 0;
};

/// Safely reads committed readable virtual pages of the host executable via `VirtualQuery`,
/// zero-fills guard/uncommitted pages without altering game memory protections, fixes up PE
/// section alignment for Ghidra via `fixup_pe_dump_for_ghidra()`, and writes `output_path`.
MemDumpResult dump_host_module_to_file(const std::string& output_path);

} // namespace sf::probe
