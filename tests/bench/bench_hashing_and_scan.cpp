#include "bench_harness.h"

#include "core/config.h"
#include "core/sha256.h"
#include "hooks/sigscan.h"
#include "tex/content_hash.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> make_payload(size_t size_bytes) {
  std::vector<std::byte> buf(size_bytes);
  for (size_t i = 0; i < size_bytes; ++i) {
    buf[i] = static_cast<std::byte>((i * 131U + 17U) & 0xFFU);
  }
  return buf;
}

void bench_xxh64_64kb(uint64_t iters) {
  const auto payload = make_payload(64ULL * 1024ULL);
  for (uint64_t i = 0; i < iters; ++i) {
    const uint64_t h = sf::tex::hash_bytes(payload, i);
    sf::bench::do_not_optimize(h);
  }
}

void bench_sha256_64kb(uint64_t iters) {
  const auto payload = make_payload(64ULL * 1024ULL);
  for (uint64_t i = 0; i < iters; ++i) {
    const auto digest = sf::sha256_bytes(payload);
    sf::bench::do_not_optimize(digest[0]);
  }
}

void bench_sigscan_4mb(uint64_t iters) {
  constexpr size_t kImageSize = 4ULL * 1024ULL * 1024ULL;
  auto image = make_payload(kImageSize);
  // Inject a unique signature near the tail of the 4 MB segment (3/4 of the way in).
  const size_t target = (kImageSize * 3U) / 4U;
  image[target + 0] = std::byte{0x48};
  image[target + 1] = std::byte{0x8B};
  image[target + 2] = std::byte{0x05};
  image[target + 3] = std::byte{0xAA};
  image[target + 4] = std::byte{0xBB};
  image[target + 5] = std::byte{0xCC};
  image[target + 6] = std::byte{0xDD};
  image[target + 7] = std::byte{0x48};
  image[target + 8] = std::byte{0x85};
  image[target + 9] = std::byte{0xC0};
  image[target + 10] = std::byte{0x74};
  image[target + 11] = std::byte{0x2A};

  const auto pattern = sf::hooks::parse_pattern("48 8B 05 ? ? ? ? 48 85 C0 74 ?");
  if (!pattern) {
    return;
  }

  for (uint64_t i = 0; i < iters; ++i) {
    const auto res = sf::hooks::sigscan(image, *pattern);
    sf::bench::do_not_optimize(res.offset);
  }
}

void bench_parse_pattern(uint64_t iters) {
  constexpr std::string_view kPat = "48 8B 05 ? ? ? ? 48 85 C0 74 ? E8 ? ? ? ?";
  for (uint64_t i = 0; i < iters; ++i) {
    const auto parsed = sf::hooks::parse_pattern(kPat);
    sf::bench::do_not_optimize(parsed.has_value());
  }
}

void bench_parse_config_toml(uint64_t iters) {
  constexpr std::string_view kToml = R"(
[general]
offline_acknowledged = false
mod_enabled = true
log_level = "info"

[present]
mode = "game"
max_fps = 60

[modules]
frame_telemetry = true
presentation = false
probe_tracer = true

[probe]
capture_frames = 120
output_file = "soulsframe_capture.jsonl"
trace_resources = true
)";

  for (uint64_t i = 0; i < iters; ++i) {
    const auto res = sf::parse_config_toml(kToml);
    sf::bench::do_not_optimize(res.config.present.max_fps);
  }
}

SF_REGISTER_BENCH("tex::hash_bytes (64KB XXH64)", "hash", 20'000, 65'536, bench_xxh64_64kb);
SF_REGISTER_BENCH("core::sha256_bytes (64KB)", "hash", 1'500, 65'536, bench_sha256_64kb);
SF_REGISTER_BENCH("hooks::sigscan (4MB pattern)", "sigscan", 120, 4ULL * 1024ULL * 1024ULL,
                  bench_sigscan_4mb);
SF_REGISTER_BENCH("hooks::parse_pattern", "sigscan", 100'000, 0, bench_parse_pattern);
SF_REGISTER_BENCH("core::parse_config_toml", "config", 5'000, 0, bench_parse_config_toml);

} // namespace
