#include "core/config.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

/// libFuzzer entry point exercising TOML config parsing and clamping against arbitrary bytes.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const auto res = sf::parse_config_toml(input);
  (void)res.config.present.max_fps;
  (void)res.parsed_ok;
  return 0;
}
