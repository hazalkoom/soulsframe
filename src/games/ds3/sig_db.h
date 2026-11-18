#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sf::games::ds3 {

/// Target classification for a signature in `ds3_sigs.toml`.
enum class SigKind : uint8_t {
  Code,       ///< Match + offset points directly to a function or instruction.
  Data,       ///< Match + offset points directly to static data.
  RipRelData, ///< Instruction contains an x64 `[rip + disp32]` displacement to data/global.
};

/// Verification confidence level recorded by the reverse engineer.
enum class SigConfidence : uint8_t {
  Guessed,
  Tested,
  Verified,
};

/// Single pattern entry in the SHA-256 keyed signature database.
struct SignatureEntry {
  std::string name;
  std::string pattern;
  int64_t offset = 0;       ///< Signed offset from pattern match start to target instruction.
  uint32_t disp_offset = 3; ///< Byte offset of `int32_t` disp inside RIP-relative instruction.
  uint32_t insn_len = 7;    ///< Total length of the RIP-relative instruction in bytes.
  SigKind kind = SigKind::Code;
  SigConfidence confidence = SigConfidence::Guessed;
  std::string notes;
};

/// Signature collection pinned to one exact host executable SHA-256 build.
struct BuildSignatureSet {
  std::string sha256;
  std::string version;
  /// Explicit build-level opt-in: must remain false until signatures are dynamically verified
  /// in a real offline game session.
  bool verified = false;
  std::vector<SignatureEntry> signatures;
};

/// Parsed signature database containing zero or more pinned executable builds.
struct SigDb {
  std::vector<BuildSignatureSet> builds;

  /// Looks up a build record matching `exe_sha256` (case-insensitive); returns nullptr if absent.
  const BuildSignatureSet* find_build(std::string_view exe_sha256) const;
};

/// Outcome of parsing `ds3_sigs.toml`.
struct SigDbParseResult {
  SigDb db;
  std::vector<std::string> warnings;
  bool parsed_ok = true;
};

/// Outcome classification returned by `resolve_signature_target()`.
enum class ResolveStatus : uint8_t {
  Resolved,
  NotFound,
  Ambiguous,
  InvalidPattern,
  OutOfBounds,
};

/// Resolved RVA and virtual address computed from an image buffer and `SignatureEntry`.
struct ResolvedTarget {
  ResolveStatus status = ResolveStatus::NotFound;
  size_t match_offset = 0;
  size_t rva = 0;
  uint64_t va = 0;
};

/// Parses a `ds3_sigs.toml` document with strict schema validation and pattern checks.
SigDbParseResult parse_sig_db_toml(std::string_view toml_text);

/// Reads and parses a signature database TOML file from disk.
SigDbParseResult load_sig_db_file(const std::string& path);

/// Scans `image` for `entry.pattern` (requiring a unique match) and resolves its target RVA/VA,
/// including signed x64 `[rip + disp32]` displacement decoding when `kind == RipRelData`.
ResolvedTarget resolve_signature_target(std::span<const std::byte> image, uint64_t image_base,
                                        const SignatureEntry& entry);

} // namespace sf::games::ds3
