#include "tex/texture_pack.h"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const auto* bytes = reinterpret_cast<const std::byte*>(data);
  const auto input = std::span<const std::byte>(bytes, size);
  const auto info = sf::tex::parse_dds(input);

  if (info.ok) {
    const volatile size_t offset = info.data_offset;
    const volatile uint64_t payload = info.data_bytes;
    (void)offset;
    (void)payload;
  }
  return 0;
}
