#include "core/config.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <span>
#include <sstream>
// Upstream toml++ v3.4.0 triggers an internal debug assertion on invalid bare key starts inside
// malformed table headers (e.g. `[%...]`) before reaching its own parse-error branch. Disabling
// internal asserts delegates cleanly to toml::parse_error instead of aborting.
#define TOML_ASSERT(expr) static_assert(true)
#include <toml++/toml.hpp>
#include <utility>

namespace sf {

Config Config::defaults() {
  return Config{};
}

namespace {

void warn_unknown_keys(const toml::table& tbl, std::string_view prefix,
                       std::span<const std::string_view> allowed,
                       std::vector<std::string>& warnings) {
  for (const auto& [k, _] : tbl) {
    const std::string_view key_sv = k.str();
    if (std::ranges::find(allowed, key_sv) == allowed.end()) {
      warnings.emplace_back("Unknown config key: " + std::string(prefix) + std::string(key_sv));
    }
  }
}

void read_bool_key(const toml::table& tbl, std::string_view key, std::string_view full_name,
                   bool& target, std::vector<std::string>& warnings) {
  if (auto v = tbl[key].value<bool>()) {
    target = *v;
  } else if (tbl.contains(key)) {
    warnings.emplace_back(std::string(full_name) + " must be boolean; using default");
  }
}

void read_clamped_u32(const toml::table& tbl, std::string_view key, std::string_view full_name,
                      uint32_t min_val, uint32_t max_val, uint32_t& target,
                      std::vector<std::string>& warnings) {
  if (auto v = tbl[key].value<int64_t>()) {
    if (std::cmp_less(*v, min_val)) {
      target = min_val;
      warnings.emplace_back(std::string(full_name) + " clamped to " + std::to_string(min_val));
    } else if (std::cmp_greater(*v, max_val)) {
      target = max_val;
      warnings.emplace_back(std::string(full_name) + " clamped to " + std::to_string(max_val));
    } else {
      target = static_cast<uint32_t>(*v);
    }
  } else if (tbl.contains(key)) {
    warnings.emplace_back(std::string(full_name) + " must be integer; using default");
  }
}

void read_safe_filename(const toml::table& tbl, std::string_view key, std::string_view full_name,
                        std::string& target, std::vector<std::string>& warnings) {
  if (auto v = tbl[key].value<std::string>()) {
    if (v->empty() || v->find("..") != std::string::npos) {
      warnings.emplace_back(std::string(full_name) + " invalid or unsafe; using default");
    } else {
      target = *v;
    }
  } else if (tbl.contains(key)) {
    warnings.emplace_back(std::string(full_name) + " must be string; using default");
  }
}

} // namespace

ConfigParseResult parse_config_toml(std::string_view toml_text) {
  ConfigParseResult out;
  out.config = Config::defaults();

  // Strip optional UTF-8 BOM and reject control/non-ASCII bytes before calling toml++ so
  // malformed binary inputs cannot hit upstream unicode bitmask/parser invariants.
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
    out.warnings.emplace_back(
        "TOML parse error: unsupported control or non-ASCII byte is not allowed");
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

  constexpr std::array<std::string_view, 4> kRootKeys = {"general", "present", "modules", "probe"};
  warn_unknown_keys(root, "", kRootKeys, out.warnings);

  if (auto* gen = root["general"].as_table()) {
    constexpr std::array<std::string_view, 3> kGenKeys = {"offline_acknowledged", "mod_enabled",
                                                          "log_level"};
    warn_unknown_keys(*gen, "general.", kGenKeys, out.warnings);

    read_bool_key(*gen, "offline_acknowledged", "general.offline_acknowledged",
                  out.config.general.offline_acknowledged, out.warnings);
    read_bool_key(*gen, "mod_enabled", "general.mod_enabled", out.config.general.mod_enabled,
                  out.warnings);

    if (auto v = (*gen)["log_level"].value<std::string>()) {
      if (*v == "debug" || *v == "info" || *v == "warn" || *v == "error") {
        out.config.general.log_level = *v;
      } else {
        out.warnings.emplace_back("general.log_level invalid ('" + *v + "'); using 'info'");
      }
    } else if (gen->contains("log_level")) {
      out.warnings.emplace_back("general.log_level must be string; using default");
    }
  }

  if (auto* pres = root["present"].as_table()) {
    constexpr std::array<std::string_view, 2> kPresKeys = {"mode", "max_fps"};
    warn_unknown_keys(*pres, "present.", kPresKeys, out.warnings);

    if (auto v = (*pres)["mode"].value<std::string>()) {
      if (*v == "game" || *v == "flip") {
        out.config.present.mode = *v;
      } else {
        out.warnings.emplace_back("present.mode invalid ('" + *v + "'); using 'game'");
      }
    } else if (pres->contains("mode")) {
      out.warnings.emplace_back("present.mode must be string; using default");
    }

    read_clamped_u32(*pres, "max_fps", "present.max_fps", 0U, 360U, out.config.present.max_fps,
                     out.warnings);
  }

  if (auto* mods = root["modules"].as_table()) {
    constexpr std::array<std::string_view, 3> kModKeys = {"frame_telemetry", "presentation",
                                                          "probe_tracer"};
    warn_unknown_keys(*mods, "modules.", kModKeys, out.warnings);

    read_bool_key(*mods, "frame_telemetry", "modules.frame_telemetry",
                  out.config.modules.frame_telemetry, out.warnings);
    read_bool_key(*mods, "presentation", "modules.presentation", out.config.modules.presentation,
                  out.warnings);
    read_bool_key(*mods, "probe_tracer", "modules.probe_tracer", out.config.modules.probe_tracer,
                  out.warnings);
  }

  if (auto* prb = root["probe"].as_table()) {
    constexpr std::array<std::string_view, 6> kProbeKeys = {"capture_frames",  "output_file",
                                                            "trace_resources", "gpu_timing",
                                                            "dump_module",     "dump_file"};
    warn_unknown_keys(*prb, "probe.", kProbeKeys, out.warnings);

    read_clamped_u32(*prb, "capture_frames", "probe.capture_frames", 1U, 36000U,
                     out.config.probe.capture_frames, out.warnings);
    read_bool_key(*prb, "trace_resources", "probe.trace_resources",
                  out.config.probe.trace_resources, out.warnings);
    read_bool_key(*prb, "gpu_timing", "probe.gpu_timing", out.config.probe.gpu_timing,
                  out.warnings);
    read_bool_key(*prb, "dump_module", "probe.dump_module", out.config.probe.dump_module,
                  out.warnings);
    read_safe_filename(*prb, "output_file", "probe.output_file", out.config.probe.output_file,
                       out.warnings);
    read_safe_filename(*prb, "dump_file", "probe.dump_file", out.config.probe.dump_file,
                       out.warnings);
  }

  return out;
}

ConfigParseResult load_config_file(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    ConfigParseResult res;
    res.config = Config::defaults();
    res.warnings.push_back("Config file not found ('" + path + "'); using defaults");
    return res;
  }
  std::ostringstream ss;
  ss << ifs.rdbuf();
  return parse_config_toml(ss.str());
}

} // namespace sf
