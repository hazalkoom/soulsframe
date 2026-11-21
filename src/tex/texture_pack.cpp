#include "tex/texture_pack.h"

#include "tex/content_hash.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace sf::tex {

namespace {

constexpr uint32_t kDdsMagic = 0x20534444U; // "DDS "
constexpr size_t kHeaderEnd = 128;
constexpr size_t kDx10End = 148;
constexpr uint32_t kDdpfFourCc = 0x4U;
constexpr uint32_t kDdpfRgb = 0x40U;
constexpr uint32_t kCaps2Cubemap = 0x200U;
constexpr uint32_t kCaps2Volume = 0x200000U;
constexpr uint32_t kDx10Texture2D = 3U;
constexpr uint32_t kMaxDim = 16384U;

constexpr uint32_t fourcc(char a, char b, char c, char d) {
  return static_cast<uint32_t>(static_cast<unsigned char>(a)) |
         (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8U) |
         (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16U) |
         (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24U);
}

uint32_t rd32(std::span<const std::byte> f, size_t off) {
  uint32_t v = 0;
  std::memcpy(&v, f.data() + off, sizeof(v));
  return v;
}

DdsInfo fail(const char* why) {
  DdsInfo d{};
  d.error = why;
  return d;
}

int hex_val(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
    s.remove_suffix(1);
  }
  return s;
}

} // namespace

uint32_t bc_block_bytes(uint32_t format) noexcept {
  switch (static_cast<DxgiFormat>(format)) {
  case DxgiFormat::BC1_UNORM:
  case DxgiFormat::BC1_UNORM_SRGB:
  case DxgiFormat::BC4_UNORM:
  case DxgiFormat::BC4_SNORM:
    return 8U;
  case DxgiFormat::BC2_UNORM:
  case DxgiFormat::BC2_UNORM_SRGB:
  case DxgiFormat::BC3_UNORM:
  case DxgiFormat::BC3_UNORM_SRGB:
  case DxgiFormat::BC5_UNORM:
  case DxgiFormat::BC5_SNORM:
  case DxgiFormat::BC6H_UF16:
  case DxgiFormat::BC6H_SF16:
  case DxgiFormat::BC7_UNORM:
  case DxgiFormat::BC7_UNORM_SRGB:
    return 16U;
  default:
    return 0U;
  }
}

uint32_t pixel_bytes(uint32_t format) noexcept {
  switch (static_cast<DxgiFormat>(format)) {
  case DxgiFormat::R8G8B8A8_UNORM:
  case DxgiFormat::R8G8B8A8_UNORM_SRGB:
  case DxgiFormat::B8G8R8A8_UNORM:
  case DxgiFormat::B8G8R8A8_UNORM_SRGB:
    return 4U;
  default:
    return 0U;
  }
}

bool is_supported_format(uint32_t format) noexcept {
  return bc_block_bytes(format) != 0U || pixel_bytes(format) != 0U;
}

uint32_t mip_row_count(uint32_t format, uint32_t height) noexcept {
  const uint32_t h = height == 0U ? 1U : height;
  return bc_block_bytes(format) != 0U ? (h + 3U) / 4U : h;
}

uint64_t mip_row_pitch(uint32_t format, uint32_t width) noexcept {
  const uint64_t w = width == 0U ? 1U : width;
  if (const uint32_t bb = bc_block_bytes(format); bb != 0U) {
    return ((w + 3U) / 4U) * bb;
  }
  return w * pixel_bytes(format);
}

uint64_t mip_chain_bytes(uint32_t format, uint32_t width, uint32_t height, uint32_t mips) noexcept {
  uint64_t total = 0;
  uint32_t w = width;
  uint32_t h = height;
  for (uint32_t i = 0; i < mips; ++i) {
    total += mip_row_pitch(format, w) * mip_row_count(format, h);
    w = w > 1U ? w / 2U : 1U;
    h = h > 1U ? h / 2U : 1U;
  }
  return total;
}

uint64_t compute_texture_key(const TextureDesc& desc, std::span<const std::byte> top_mip) noexcept {
  const std::array<uint32_t, 3> fields{desc.width, desc.height, desc.format};
  const uint64_t seed = hash_bytes(std::as_bytes(std::span{fields}));
  return hash_bytes(top_mip, seed);
}

DdsInfo parse_dds(std::span<const std::byte> file) noexcept {
  if (file.size() < kHeaderEnd) {
    return fail("file smaller than DDS header");
  }
  if (rd32(file, 0) != kDdsMagic || rd32(file, 4) != 124U || rd32(file, 76) != 32U) {
    return fail("bad DDS magic or header size");
  }
  DdsInfo d{};
  d.height = rd32(file, 12);
  d.width = rd32(file, 16);
  d.mip_levels = rd32(file, 28) == 0U ? 1U : rd32(file, 28);
  const uint32_t pf_flags = rd32(file, 80);
  const uint32_t cc = rd32(file, 84);
  if ((rd32(file, 112) & (kCaps2Cubemap | kCaps2Volume)) != 0U) {
    return fail("cubemap/volume DDS not supported");
  }
  if (d.width == 0U || d.height == 0U || d.width > kMaxDim || d.height > kMaxDim) {
    return fail("DDS dimensions out of range");
  }
  d.data_offset = kHeaderEnd;

  if ((pf_flags & kDdpfFourCc) != 0U && cc == fourcc('D', 'X', '1', '0')) {
    if (file.size() < kDx10End) {
      return fail("truncated DX10 header");
    }
    if (rd32(file, 132) != kDx10Texture2D || rd32(file, 140) > 1U) {
      return fail("DX10 DDS must be a single 2D texture");
    }
    d.format = rd32(file, 128);
    d.data_offset = kDx10End;
  } else if ((pf_flags & kDdpfFourCc) != 0U) {
    if (cc == fourcc('D', 'X', 'T', '1')) {
      d.format = static_cast<uint32_t>(DxgiFormat::BC1_UNORM);
    } else if (cc == fourcc('D', 'X', 'T', '3')) {
      d.format = static_cast<uint32_t>(DxgiFormat::BC2_UNORM);
    } else if (cc == fourcc('D', 'X', 'T', '5')) {
      d.format = static_cast<uint32_t>(DxgiFormat::BC3_UNORM);
    } else if (cc == fourcc('A', 'T', 'I', '1') || cc == fourcc('B', 'C', '4', 'U')) {
      d.format = static_cast<uint32_t>(DxgiFormat::BC4_UNORM);
    } else if (cc == fourcc('A', 'T', 'I', '2') || cc == fourcc('B', 'C', '5', 'U')) {
      d.format = static_cast<uint32_t>(DxgiFormat::BC5_UNORM);
    }
  } else if ((pf_flags & kDdpfRgb) != 0U && rd32(file, 88) == 32U) {
    const uint32_t rmask = rd32(file, 92);
    if (rmask == 0x000000FFU) {
      d.format = static_cast<uint32_t>(DxgiFormat::R8G8B8A8_UNORM);
    } else if (rmask == 0x00FF0000U) {
      d.format = static_cast<uint32_t>(DxgiFormat::B8G8R8A8_UNORM);
    }
  }

  if (!is_supported_format(d.format)) {
    return fail("unsupported DDS pixel format");
  }
  if (d.mip_levels > 15U) {
    return fail("too many mip levels");
  }
  d.data_bytes = mip_chain_bytes(d.format, d.width, d.height, d.mip_levels);
  if (file.size() - d.data_offset < d.data_bytes) {
    return fail("DDS payload truncated");
  }
  d.ok = true;
  return d;
}

bool is_safe_relative_path(std::string_view rel) noexcept {
  if (rel.empty() || rel.size() > 260U || rel.front() == '/' || rel.front() == '\\') {
    return false;
  }
  for (const char c : rel) {
    if (static_cast<unsigned char>(c) < 0x20U || c == ':' || c == 0x7F) {
      return false;
    }
  }
  size_t start = 0;
  while (start <= rel.size()) {
    size_t end = rel.find_first_of("/\\", start);
    if (end == std::string_view::npos) {
      end = rel.size();
    }
    if (rel.substr(start, end - start) == "..") {
      return false;
    }
    start = end + 1;
  }
  return true;
}

std::string key_hex(uint64_t key) {
  std::array<char, 17> buf{};
  std::snprintf(buf.data(), buf.size(), "%016llx", static_cast<unsigned long long>(key));
  return {buf.data()};
}

Manifest parse_manifest(std::string_view text) {
  Manifest m;
  size_t line_no = 0;
  while (!text.empty()) {
    const size_t nl = text.find('\n');
    std::string_view line = text.substr(0, nl);
    text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
    ++line_no;

    if (const size_t hash = line.find('#'); hash != std::string_view::npos) {
      line = line.substr(0, hash);
    }
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    const std::string where = "manifest line " + std::to_string(line_no) + ": ";
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
      m.warnings.emplace_back(where + "missing '='");
      continue;
    }
    const std::string_view key_sv = trim(line.substr(0, eq));
    const std::string_view path_sv = trim(line.substr(eq + 1));
    if (key_sv.size() != 16U) {
      m.warnings.emplace_back(where + "key must be 16 hex digits");
      continue;
    }
    uint64_t key = 0;
    bool hex_ok = true;
    for (const char c : key_sv) {
      const int v = hex_val(c);
      if (v < 0) {
        hex_ok = false;
        break;
      }
      key = (key << 4U) | static_cast<uint64_t>(v);
    }
    if (!hex_ok) {
      m.warnings.emplace_back(where + "key must be 16 hex digits");
      continue;
    }
    if (!is_safe_relative_path(path_sv)) {
      m.warnings.emplace_back(where + "unsafe or empty path");
      continue;
    }
    m.entries[key] = std::string(path_sv);
  }
  return m;
}

bool TextureSwapPolicy::try_reserve(uint64_t bytes) noexcept {
  if (budget_ != 0U && (bytes > budget_ || stats_.bytes_loaded > budget_ - bytes)) {
    ++stats_.over_budget;
    return false;
  }
  stats_.bytes_loaded += bytes;
  ++stats_.replaced;
  return true;
}

} // namespace sf::tex
