#include "games/ds3/sig_db.h"

#include "hooks/sigscan.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#define TOML_ASSERT(expr) static_assert(true)
#include <toml++/toml.hpp>

namespace sf::games::ds3 {

namespace {

std::string normalize_hex(std::string_view sv) {
  std::string out(sv);
  std::ranges::transform(out, out.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

bool has_range(std::span<const std::byte> buf, size_t offset, size_t len) noexcept {
  return len <= buf.size() && offset <= buf.size() - len;
}

int32_t read_i32_le(std::span<const std::byte> buf, size_t offset) noexcept {
  const auto b0 = std::to_integer<uint32_t>(buf[offset]);
  const auto b1 = std::to_integer<uint32_t>(buf[offset + 1U]);
  const auto b2 = std::to_integer<uint32_t>(buf[offset + 2U]);
  const auto b3 = std::to_integer<uint32_t>(buf[offset + 3U]);
  return static_cast<int32_t>(b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U));
}

SigKind parse_kind(std::string_view val, std::vector<std::string>& warnings,
                   std::string_view sig_name) {
  if (val == "code") {
    return SigKind::Code;
  }
  if (val == "data") {
    return SigKind::Data;
  }
  if (val == "rip_rel_data") {
    return SigKind::RipRelData;
  }
  warnings.emplace_back("Signature '" + std::string(sig_name) + "' unknown kind '" +
                        std::string(val) + "'; defaulting to 'code'");
  return SigKind::Code;
}

SigConfidence parse_confidence(std::string_view val, std::vector<std::string>& warnings,
                               std::string_view sig_name) {
  if (val == "guessed") {
    return SigConfidence::Guessed;
  }
  if (val == "tested") {
    return SigConfidence::Tested;
  }
  if (val == "verified") {
    return SigConfidence::Verified;
  }
  warnings.emplace_back("Signature '" + std::string(sig_name) + "' unknown confidence '" +
                        std::string(val) + "'; defaulting to 'guessed'");
  return SigConfidence::Guessed;
}

} // namespace

const BuildSignatureSet* SigDb::find_build(std::string_view exe_sha256) const {
  if (exe_sha256.empty()) {
    return nullptr;
  }
  const std::string needle = normalize_hex(exe_sha256);
  const auto it =
      std::ranges::find_if(builds, [&](const BuildSignatureSet& b) { return b.sha256 == needle; });
  return it != builds.end() ? &(*it) : nullptr;
}

SigDbParseResult parse_sig_db_toml(std::string_view toml_text) {
  SigDbParseResult out;
  if (toml_text.size() >= 3 && static_cast<unsigned char>(toml_text[0]) == 0xEFU &&
      static_cast<unsigned char>(toml_text[1]) == 0xBBU &&
      static_cast<unsigned char>(toml_text[2]) == 0xBFU) {
    toml_text.remove_prefix(3);
  }
  const bool has_disallowed = std::ranges::any_of(toml_text, [](unsigned char ch) {
    return (ch < 0x20U && ch != '\t' && ch != '\n' && ch != '\r') || ch >= 0x7FU;
  });
  if (has_disallowed) {
    out.parsed_ok = false;
    out.warnings.emplace_back("TOML parse error: unsupported control or non-ASCII byte");
    return out;
  }

  toml::table root;
  try {
    root = toml::parse(toml_text);
  } catch (const toml::parse_error& err) {
    out.parsed_ok = false;
    out.warnings.emplace_back(std::string("TOML parse error: ") + std::string(err.description()));
    return out;
  }

  const auto* builds_tbl = root["build"].as_table();
  if (!builds_tbl) {
    return out;
  }

  for (const auto& [sha_key, node] : *builds_tbl) {
    const auto* btbl = node.as_table();
    if (!btbl) {
      out.warnings.emplace_back("Ignoring non-table entry under build." +
                                std::string(sha_key.str()));
      continue;
    }

    BuildSignatureSet bset;
    bset.sha256 = normalize_hex(sha_key.str());
    if (auto ver = (*btbl)["version"].value<std::string>()) {
      bset.version = *ver;
    }
    if (auto ver_flag = (*btbl)["verified"].value<bool>()) {
      bset.verified = *ver_flag;
    }

    if (const auto* sigs_arr = (*btbl)["sig"].as_array()) {
      for (const auto& item : *sigs_arr) {
        const auto* stbl = item.as_table();
        if (!stbl) {
          out.warnings.emplace_back("Skipping non-table signature entry");
          continue;
        }
        SignatureEntry entry;
        if (auto n = (*stbl)["name"].value<std::string>()) {
          entry.name = *n;
        }
        if (auto p = (*stbl)["pattern"].value<std::string>()) {
          entry.pattern = *p;
        }
        if (auto o = (*stbl)["offset"].value<int64_t>()) {
          entry.offset = *o;
        }
        if (auto d = (*stbl)["disp_offset"].value<int64_t>(); d && *d >= 0) {
          entry.disp_offset = static_cast<uint32_t>(*d);
        }
        if (auto l = (*stbl)["insn_len"].value<int64_t>(); l && *l >= 4) {
          entry.insn_len = static_cast<uint32_t>(*l);
        }
        if (auto k = (*stbl)["kind"].value<std::string>()) {
          entry.kind = parse_kind(*k, out.warnings, entry.name);
        }
        if (auto c = (*stbl)["confidence"].value<std::string>()) {
          entry.confidence = parse_confidence(*c, out.warnings, entry.name);
        }
        if (auto notes = (*stbl)["notes"].value<std::string>()) {
          entry.notes = *notes;
        }

        if (entry.name.empty() || !sf::hooks::parse_pattern(entry.pattern).has_value()) {
          out.warnings.emplace_back("Skipping invalid signature entry '" + entry.name + "'");
          continue;
        }
        bset.signatures.push_back(std::move(entry));
      }
    }

    out.db.builds.push_back(std::move(bset));
  }

  return out;
}

SigDbParseResult load_sig_db_file(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    SigDbParseResult res;
    res.parsed_ok = false;
    res.warnings.emplace_back("Failed to open signature DB file: " + path);
    return res;
  }
  std::ostringstream ss;
  ss << ifs.rdbuf();
  return parse_sig_db_toml(ss.str());
}

ResolvedTarget resolve_signature_target(std::span<const std::byte> image, uint64_t image_base,
                                        const SignatureEntry& entry) {
  ResolvedTarget res;
  const auto scan = sf::hooks::sigscan(image, entry.pattern);
  switch (scan.status) {
  case sf::hooks::ScanStatus::InvalidPattern:
    res.status = ResolveStatus::InvalidPattern;
    return res;
  case sf::hooks::ScanStatus::NotFound:
    res.status = ResolveStatus::NotFound;
    return res;
  case sf::hooks::ScanStatus::Ambiguous:
    res.status = ResolveStatus::Ambiguous;
    res.match_offset = scan.offset;
    return res;
  case sf::hooks::ScanStatus::Found:
    break;
  }

  res.match_offset = scan.offset;
  const int64_t insn_rva_signed = static_cast<int64_t>(scan.offset) + entry.offset;
  if (insn_rva_signed < 0 || static_cast<size_t>(insn_rva_signed) >= image.size()) {
    res.status = ResolveStatus::OutOfBounds;
    return res;
  }
  const auto insn_rva = static_cast<size_t>(insn_rva_signed);

  if (entry.kind != SigKind::RipRelData) {
    res.status = ResolveStatus::Resolved;
    res.rva = insn_rva;
    res.va = image_base + res.rva;
    return res;
  }

  const size_t disp_pos = insn_rva + entry.disp_offset;
  if (entry.insn_len < entry.disp_offset + 4U || !has_range(image, disp_pos, 4U)) {
    res.status = ResolveStatus::OutOfBounds;
    return res;
  }

  const int32_t disp32 = read_i32_le(image, disp_pos);
  const int64_t target_signed = static_cast<int64_t>(insn_rva) +
                                static_cast<int64_t>(entry.insn_len) + static_cast<int64_t>(disp32);
  if (target_signed < 0 || static_cast<size_t>(target_signed) >= image.size()) {
    res.status = ResolveStatus::OutOfBounds;
    return res;
  }

  res.status = ResolveStatus::Resolved;
  res.rva = static_cast<size_t>(target_signed);
  res.va = image_base + res.rva;
  return res;
}

} // namespace sf::games::ds3
