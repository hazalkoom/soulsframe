#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// Milestone 9 pure texture-pack core: content keys, DDS header parsing, manifest parsing and
/// VRAM budget policy. Pure C++ (no Win32/D3D11) so every decision is unit-tested on Linux;
/// the D3D11 `CreateTexture2D` hook in `gfx/d3d11/texture_module.cpp` only does plumbing.
namespace sf::tex {

/// Numeric DXGI_FORMAT values we understand, mirrored here so `tex/` stays D3D-free.
enum class DxgiFormat : uint32_t {
  Unknown = 0,
  R8G8B8A8_UNORM = 28,
  R8G8B8A8_UNORM_SRGB = 29,
  BC1_UNORM = 71,
  BC1_UNORM_SRGB = 72,
  BC2_UNORM = 74,
  BC2_UNORM_SRGB = 75,
  BC3_UNORM = 77,
  BC3_UNORM_SRGB = 78,
  BC4_UNORM = 80,
  BC4_SNORM = 81,
  BC5_UNORM = 83,
  BC5_SNORM = 84,
  B8G8R8A8_UNORM = 87,
  B8G8R8A8_UNORM_SRGB = 91,
  BC6H_UF16 = 95,
  BC6H_SF16 = 96,
  BC7_UNORM = 98,
  BC7_UNORM_SRGB = 99,
};

/// Immutable description of a 2D texture creation request (subset of D3D11_TEXTURE2D_DESC).
struct TextureDesc {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t mip_levels = 1;
  uint32_t array_size = 1;
  uint32_t format = 0; ///< Raw DXGI_FORMAT value.
};

/// Bytes per 4x4 block for BC formats, 0 if not block-compressed.
uint32_t bc_block_bytes(uint32_t format) noexcept;

/// Bytes per pixel for supported uncompressed formats, 0 if unsupported.
uint32_t pixel_bytes(uint32_t format) noexcept;

/// True when `format` is one we can key and replace (BC1-7 or 32-bit RGBA/BGRA).
bool is_supported_format(uint32_t format) noexcept;

/// Number of pitch rows in one mip level (block rows for BC formats). Why: SysMemPitch is per
/// row of *blocks* for compressed data, so hashing must use the same row count.
uint32_t mip_row_count(uint32_t format, uint32_t height) noexcept;

/// Tightly-packed row pitch in bytes of a mip level, 0 for unsupported formats.
uint64_t mip_row_pitch(uint32_t format, uint32_t width) noexcept;

/// Total tightly-packed bytes of `mips` mip levels starting at `width` x `height`.
uint64_t mip_chain_bytes(uint32_t format, uint32_t width, uint32_t height, uint32_t mips) noexcept;

/// Stable 64-bit key for a texture: XXH64 of the top mip's bytes seeded with the descriptor.
/// Why both: identical pixels at different sizes/formats must never collide into one swap.
uint64_t compute_texture_key(const TextureDesc& desc, std::span<const std::byte> top_mip) noexcept;

/// Parsed DDS container header. `ok == false` means `error` explains the rejection.
struct DdsInfo {
  bool ok = false;
  std::string error;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t mip_levels = 1;
  uint32_t format = 0;
  size_t data_offset = 0;  ///< Byte offset of the first mip's payload.
  uint64_t data_bytes = 0; ///< Bytes required for the whole mip chain.
};

/// Validates a DDS file image (legacy DXT1/3/5, ATI1/2, RGBA32 or DX10-extended 2D, no cubes,
/// no arrays) and checks the payload is large enough for the declared mip chain.
DdsInfo parse_dds(std::span<const std::byte> file) noexcept;

/// True when `rel` is a safe pack-relative path: non-empty, no `..` segment, no absolute or
/// drive prefix, no control characters. Why: manifests are user-supplied files.
bool is_safe_relative_path(std::string_view rel) noexcept;

/// One `key -> relative DDS path` replacement entry.
struct ManifestEntry {
  uint64_t key = 0;
  std::string path;
};

/// Result of parsing a `manifest.txt` pack index.
struct Manifest {
  std::unordered_map<uint64_t, std::string> entries;
  std::vector<std::string> warnings;
};

/// Parses lines of the form `<16 hex digits> = <relative/path.dds>`; `#` starts a comment.
/// Invalid lines are skipped with a warning; later duplicates override earlier ones.
Manifest parse_manifest(std::string_view text);

/// Formats a key as 16 lowercase hex digits (the manifest/log spelling).
std::string key_hex(uint64_t key);

/// Running counters reported at shutdown.
struct TextureSwapStats {
  uint64_t observed = 0;    ///< Eligible CreateTexture2D calls hashed.
  uint64_t replaced = 0;    ///< Successfully swapped textures.
  uint64_t over_budget = 0; ///< Matches rejected by the VRAM budget.
  uint64_t invalid = 0;     ///< Matches rejected due to bad/missing DDS files.
  uint64_t bytes_loaded = 0;
};

/// Budget policy deciding whether a matched replacement may be uploaded.
class TextureSwapPolicy {
public:
  /// `budget_bytes == 0` means unlimited.
  explicit TextureSwapPolicy(uint64_t budget_bytes = 0) : budget_(budget_bytes) {}

  /// Records an eligible creation request.
  void note_observed() noexcept { ++stats_.observed; }

  /// Returns true and charges the budget if `bytes` fits; otherwise counts `over_budget`.
  bool try_reserve(uint64_t bytes) noexcept;

  /// Counts a match rejected because its file was unreadable or invalid.
  void note_invalid() noexcept { ++stats_.invalid; }

  /// Returns a snapshot of the counters.
  const TextureSwapStats& stats() const noexcept { return stats_; }

private:
  uint64_t budget_ = 0;
  TextureSwapStats stats_{};
};

} // namespace sf::tex
