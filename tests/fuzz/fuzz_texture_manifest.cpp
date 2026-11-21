#include "tex/texture_pack.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  const auto manifest = sf::tex::parse_manifest(text);

  for (const auto& [key, path] : manifest.entries) {
    const volatile uint64_t key_sink = key;
    const volatile bool safe = sf::tex::is_safe_relative_path(path);
    (void)key_sink;
    (void)safe;
  }

  if (size > 0) {
    const std::string_view sample(text.data(), text.size() > 260U ? 260U : text.size());
    (void)sf::tex::is_safe_relative_path(sample);
  }
  return 0;
}
