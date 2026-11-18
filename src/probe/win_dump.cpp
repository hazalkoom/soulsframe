#include "probe/win_dump.h"

#include "probe/pe_dump.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace sf::probe {

namespace {

constexpr size_t kMaxDumpBytes = 512ULL * 1024ULL * 1024ULL;

bool is_readable_protect(DWORD protect) noexcept {
  if ((protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0U) {
    return false;
  }
  constexpr DWORD kReadableMask = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE |
                                  PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                  PAGE_EXECUTE_WRITECOPY;
  return (protect & kReadableMask) != 0U;
}

} // namespace

MemDumpResult dump_host_module_to_file(const std::string& output_path) {
  MemDumpResult res;
  HMODULE mod = GetModuleHandleA(nullptr);
  if (!mod) {
    res.error = "GetModuleHandleA(nullptr) failed";
    return res;
  }

  const auto base = reinterpret_cast<uintptr_t>(mod);
  res.image_base = static_cast<uint64_t>(base);

  // Verify the initial header page is readable before inspecting PE headers.
  MEMORY_BASIC_INFORMATION hdr_mbi{};
  if (VirtualQuery(reinterpret_cast<LPCVOID>(base), &hdr_mbi, sizeof(hdr_mbi)) == 0 ||
      hdr_mbi.State != MEM_COMMIT || !is_readable_protect(hdr_mbi.Protect)) {
    res.error = "Host PE header page is unreadable";
    return res;
  }

  const size_t hdr_probe_len = std::min<size_t>(4096U, hdr_mbi.RegionSize);
  const auto hdr_span =
      std::span<const std::byte>(reinterpret_cast<const std::byte*>(base), hdr_probe_len);
  const PeImageInfo info = inspect_pe64(hdr_span);
  if (!info.valid || info.size_of_image == 0) {
    res.error = info.error.empty() ? "Invalid host PE32+ header" : info.error;
    return res;
  }

  const size_t total_size = std::min<size_t>(info.size_of_image, kMaxDumpBytes);
  std::vector<std::byte> image(total_size, std::byte{0});

  size_t cursor = 0;
  while (cursor < total_size) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(base + cursor), &mbi, sizeof(mbi)) == 0 ||
        mbi.RegionSize == 0) {
      break;
    }

    const auto region_start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    const uintptr_t region_end = region_start + mbi.RegionSize;
    const uintptr_t cur_addr = base + cursor;
    const size_t avail_in_region = (region_end > cur_addr) ? (region_end - cur_addr) : 4096U;
    const size_t chunk = std::min<size_t>(avail_in_region, total_size - cursor);

    if (mbi.State == MEM_COMMIT && is_readable_protect(mbi.Protect)) {
      std::memcpy(image.data() + cursor, reinterpret_cast<const void*>(cur_addr), chunk);
      ++res.regions_copied;
    } else {
      ++res.regions_skipped;
    }
    cursor += chunk;
  }

  const PeFixupResult fix = fixup_pe_dump_for_ghidra(image, res.image_base);
  if (!fix.ok) {
    res.error = "PE dump fixup failed: " + fix.error;
    return res;
  }
  res.sections_fixed = fix.sections_fixed;

  std::ofstream ofs(output_path, std::ios::binary | std::ios::trunc);
  if (!ofs.is_open()) {
    res.error = "Failed to open dump output file: " + output_path;
    return res;
  }
  ofs.write(reinterpret_cast<const char*>(image.data()),
            static_cast<std::streamsize>(image.size()));
  if (!ofs.good()) {
    res.error = "Failed while writing dump file: " + output_path;
    return res;
  }

  res.ok = true;
  res.bytes_written = image.size();
  return res;
}

} // namespace sf::probe
