#include "hooks/sigscan.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

/// libFuzzer entry point splitting arbitrary input into a pattern string and memory image buffer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0) {
    return 0;
  }

  // First byte chooses split offset between pattern text and scanned memory region.
  const size_t split = static_cast<size_t>(data[0]) % size;
  const std::string_view pattern(reinterpret_cast<const char*>(data + 1),
                                 split > 0 ? split - 1 : 0);
  const size_t img_offset = 1 + (split > 0 ? split - 1 : 0);
  const auto* img_ptr = reinterpret_cast<const std::byte*>(data + img_offset);
  const std::span<const std::byte> image(img_ptr, size - img_offset);

  const auto res = sf::hooks::sigscan(image, pattern);
  (void)res.status;
  return 0;
}
