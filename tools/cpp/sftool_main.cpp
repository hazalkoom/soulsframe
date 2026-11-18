#include "core/sha256.h"
#include "games/ds3/sig_db.h"
#include "hooks/sigscan.h"
#include "probe/pe_dump.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::optional<std::vector<std::byte>> read_binary_file(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary | std::ios::ate);
  if (!ifs.is_open()) {
    return std::nullopt;
  }
  const auto end_pos = ifs.tellg();
  if (end_pos < 0) {
    return std::nullopt;
  }
  const auto size = static_cast<size_t>(end_pos);
  std::vector<std::byte> buf(size);
  ifs.seekg(0, std::ios::beg);
  if (size > 0 &&
      !ifs.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size))) {
    return std::nullopt;
  }
  return buf;
}

std::optional<uint64_t> parse_hex_u64(std::string_view sv) {
  if (sv.starts_with("0x") || sv.starts_with("0X")) {
    sv.remove_prefix(2);
  }
  if (sv.empty()) {
    return std::nullopt;
  }
  uint64_t val = 0;
  for (const char ch : sv) {
    val <<= 4U;
    if (ch >= '0' && ch <= '9') {
      val |= static_cast<uint64_t>(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      val |= static_cast<uint64_t>(ch - 'a' + 10);
    } else if (ch >= 'A' && ch <= 'F') {
      val |= static_cast<uint64_t>(ch - 'A' + 10);
    } else {
      return std::nullopt;
    }
  }
  return val;
}

void print_usage() {
  std::cout << "sftool - SoulsFrame native RE & signature utility\n"
            << "Usage:\n"
            << "  sftool builds hash <file>\n"
            << "  sftool pe-info <file>\n"
            << "  sftool pe-fixup <in_dump> <out_pe> [runtime_base_hex]\n"
            << "  sftool sigmaker --image <file> --rva <hex> [--min-len <N>]\n"
            << "  sftool sigtest --image <file> --db <ds3_sigs.toml> [--sha256 <hex>]\n";
}

int cmd_builds_hash(int argc, char** argv) {
  if (argc < 4 || std::string_view(argv[2]) != "hash") {
    std::cerr << "Usage: sftool builds hash <file>\n";
    return 2;
  }
  const auto data = read_binary_file(argv[3]);
  if (!data) {
    std::cerr << "Failed to read file: " << argv[3] << '\n';
    return 1;
  }
  std::cout << sf::sha256_hex(*data) << "  " << argv[3] << '\n';
  return 0;
}

int cmd_pe_info(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "Usage: sftool pe-info <file>\n";
    return 2;
  }
  const auto data = read_binary_file(argv[2]);
  if (!data) {
    std::cerr << "Failed to read file: " << argv[2] << '\n';
    return 1;
  }
  const auto info = sf::probe::inspect_pe64(*data);
  if (!info.valid) {
    std::cerr << "Invalid PE32+ image: " << info.error << '\n';
    return 1;
  }
  std::cout << "ImageBase:   0x" << std::hex << info.image_base << '\n'
            << "EntryRVA:    0x" << info.entry_point_rva << '\n'
            << "SizeOfImage: 0x" << info.size_of_image << std::dec << '\n'
            << "Sections (" << info.sections.size() << "):\n";
  for (const auto& s : info.sections) {
    std::cout << "  " << std::left << std::setw(8) << s.name << " VA=0x" << std::hex << std::setw(8)
              << std::right << s.virtual_address << " VSize=0x" << std::setw(8) << s.virtual_size
              << " RawOff=0x" << std::setw(8) << s.raw_offset << " RawSize=0x" << std::setw(8)
              << s.raw_size << std::dec << '\n';
  }
  return 0;
}

int cmd_pe_fixup(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "Usage: sftool pe-fixup <in_dump> <out_pe> [runtime_base_hex]\n";
    return 2;
  }
  auto data = read_binary_file(argv[2]);
  if (!data) {
    std::cerr << "Failed to read dump: " << argv[2] << '\n';
    return 1;
  }
  uint64_t base = 0;
  if (argc >= 5) {
    const auto parsed = parse_hex_u64(argv[4]);
    if (!parsed) {
      std::cerr << "Invalid hex base address: " << argv[4] << '\n';
      return 2;
    }
    base = *parsed;
  }
  const auto res = sf::probe::fixup_pe_dump_for_ghidra(*data, base);
  if (!res.ok) {
    std::cerr << "PE fixup failed: " << res.error << '\n';
    return 1;
  }
  std::ofstream ofs(argv[3], std::ios::binary | std::ios::trunc);
  if (!ofs.is_open()) {
    std::cerr << "Failed to open output file: " << argv[3] << '\n';
    return 1;
  }
  ofs.write(reinterpret_cast<const char*>(data->data()),
            static_cast<std::streamsize>(data->size()));
  std::cout << "Fixed " << res.sections_fixed << " sections (ImageBase=0x" << std::hex
            << res.image_base << std::dec << ") -> " << argv[3] << '\n';
  return ofs.good() ? 0 : 1;
}

int cmd_sigmaker(int argc, char** argv) {
  std::string img_path;
  std::string rva_str;
  size_t min_len = 8;

  for (int i = 2; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--image" && i + 1 < argc) {
      img_path = argv[++i];
    } else if (arg == "--rva" && i + 1 < argc) {
      rva_str = argv[++i];
    } else if (arg == "--min-len" && i + 1 < argc) {
      min_len = static_cast<size_t>(std::stoull(argv[++i]));
    }
  }
  if (img_path.empty() || rva_str.empty()) {
    std::cerr << "Usage: sftool sigmaker --image <file> --rva <hex> [--min-len <N>]\n";
    return 2;
  }

  const auto rva_opt = parse_hex_u64(rva_str);
  const auto data = read_binary_file(img_path);
  if (!rva_opt || !data || *rva_opt >= data->size()) {
    std::cerr << "Invalid image or RVA out of bounds\n";
    return 1;
  }

  const size_t rva = static_cast<size_t>(*rva_opt);
  const size_t max_len = std::min<size_t>(64U, data->size() - rva);
  std::string pat;
  for (size_t len = std::min(min_len, max_len); len <= max_len; ++len) {
    pat.clear();
    for (size_t k = 0; k < len; ++k) {
      if (k > 0) {
        pat.push_back(' ');
      }
      std::array<char, 4> hex{};
      std::snprintf(hex.data(), hex.size(), "%02X", std::to_integer<unsigned>((*data)[rva + k]));
      pat += hex.data();
    }
    const auto scan = sf::hooks::sigscan(*data, pat);
    if (scan.status == sf::hooks::ScanStatus::Found && scan.offset == rva) {
      std::cout << pat << '\n';
      return 0;
    }
  }
  std::cerr << "Could not isolate a unique signature within " << max_len << " bytes at RVA 0x"
            << std::hex << rva << std::dec << '\n';
  return 1;
}

int cmd_sigtest(int argc, char** argv) {
  std::string img_path;
  std::string db_path;
  std::string sha_override;

  for (int i = 2; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--image" && i + 1 < argc) {
      img_path = argv[++i];
    } else if (arg == "--db" && i + 1 < argc) {
      db_path = argv[++i];
    } else if (arg == "--sha256" && i + 1 < argc) {
      sha_override = argv[++i];
    }
  }
  if (img_path.empty() || db_path.empty()) {
    std::cerr << "Usage: sftool sigtest --image <file> --db <ds3_sigs.toml> [--sha256 <hex>]\n";
    return 2;
  }

  const auto data = read_binary_file(img_path);
  if (!data) {
    std::cerr << "Failed to read image: " << img_path << '\n';
    return 1;
  }
  const auto db_res = sf::games::ds3::load_sig_db_file(db_path);
  if (!db_res.parsed_ok) {
    for (const auto& w : db_res.warnings) {
      std::cerr << "DB warning: " << w << '\n';
    }
    return 1;
  }

  const std::string sha = sha_override.empty() ? sf::sha256_hex(*data) : sha_override;
  const auto* build = db_res.db.find_build(sha);
  if (!build) {
    std::cout << "No build entry for sha256=" << sha << " (0 signatures tested)\n";
    return 0;
  }

  const auto pe = sf::probe::inspect_pe64(*data);
  const uint64_t image_base = pe.valid ? pe.image_base : 0;

  int failures = 0;
  for (const auto& sig : build->signatures) {
    const auto resolved = sf::games::ds3::resolve_signature_target(*data, image_base, sig);
    const bool ok = resolved.status == sf::games::ds3::ResolveStatus::Resolved;
    if (!ok && sig.confidence == sf::games::ds3::SigConfidence::Verified) {
      ++failures;
    }
    std::cout << (ok ? "[OK]   " : "[FAIL] ") << sig.name << " rva=0x" << std::hex << resolved.rva
              << " va=0x" << resolved.va << std::dec << '\n';
  }
  return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return 2;
  }
  const std::string_view cmd(argv[1]);
  if (cmd == "--help" || cmd == "-h") {
    print_usage();
    return 0;
  }
  if (cmd == "builds") {
    return cmd_builds_hash(argc, argv);
  }
  if (cmd == "pe-info") {
    return cmd_pe_info(argc, argv);
  }
  if (cmd == "pe-fixup") {
    return cmd_pe_fixup(argc, argv);
  }
  if (cmd == "sigmaker") {
    return cmd_sigmaker(argc, argv);
  }
  if (cmd == "sigtest") {
    return cmd_sigtest(argc, argv);
  }
  print_usage();
  return 2;
}
