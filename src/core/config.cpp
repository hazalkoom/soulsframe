#include "core/config.h"

#include <algorithm>
#include <fstream>
#include <span>
#include <sstream>
#include <toml++/toml.hpp>

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
    if (std::find(allowed.begin(), allowed.end(), key_sv) == allowed.end()) {
      warnings.push_back("Unknown config key: " + std::string(prefix) + std::string(key_sv));
    }
  }
}

} // namespace

ConfigParseResult parse_config_toml(std::string_view toml_text) {
  ConfigParseResult out;
  out.config = Config::defaults();

  toml::table root;
  try {
    root = toml::parse(toml_text);
  } catch (const toml::parse_error& err) {
    out.parsed_ok = false;
    out.warnings.push_back(std::string("TOML parse error: ") + std::string(err.description()));
    return out;
  }

  constexpr std::string_view kRootKeys[] = {"general", "present", "modules"};
  warn_unknown_keys(root, "", kRootKeys, out.warnings);

  if (auto* gen = root["general"].as_table()) {
    constexpr std::string_view kGenKeys[] = {"offline_acknowledged", "mod_enabled", "log_level"};
    warn_unknown_keys(*gen, "general.", kGenKeys, out.warnings);

    if (auto v = (*gen)["offline_acknowledged"].value<bool>()) {
      out.config.general.offline_acknowledged = *v;
    } else if (gen->contains("offline_acknowledged")) {
      out.warnings.push_back("general.offline_acknowledged must be boolean; using default");
    }

    if (auto v = (*gen)["mod_enabled"].value<bool>()) {
      out.config.general.mod_enabled = *v;
    } else if (gen->contains("mod_enabled")) {
      out.warnings.push_back("general.mod_enabled must be boolean; using default");
    }

    if (auto v = (*gen)["log_level"].value<std::string>()) {
      if (*v == "debug" || *v == "info" || *v == "warn" || *v == "error") {
        out.config.general.log_level = *v;
      } else {
        out.warnings.push_back("general.log_level invalid ('" + *v + "'); using 'info'");
      }
    } else if (gen->contains("log_level")) {
      out.warnings.push_back("general.log_level must be string; using default");
    }
  }

  if (auto* pres = root["present"].as_table()) {
    constexpr std::string_view kPresKeys[] = {"mode", "max_fps"};
    warn_unknown_keys(*pres, "present.", kPresKeys, out.warnings);

    if (auto v = (*pres)["mode"].value<std::string>()) {
      if (*v == "game" || *v == "flip") {
        out.config.present.mode = *v;
      } else {
        out.warnings.push_back("present.mode invalid ('" + *v + "'); using 'game'");
      }
    } else if (pres->contains("mode")) {
      out.warnings.push_back("present.mode must be string; using default");
    }

    if (auto v = (*pres)["max_fps"].value<int64_t>()) {
      if (*v < 0) {
        out.config.present.max_fps = 0;
        out.warnings.push_back("present.max_fps clamped to 0");
      } else if (*v > 360) {
        out.config.present.max_fps = 360;
        out.warnings.push_back("present.max_fps clamped to 360");
      } else {
        out.config.present.max_fps = static_cast<uint32_t>(*v);
      }
    } else if (pres->contains("max_fps")) {
      out.warnings.push_back("present.max_fps must be integer; using default");
    }
  }

  if (auto* mods = root["modules"].as_table()) {
    constexpr std::string_view kModKeys[] = {"frame_telemetry", "presentation"};
    warn_unknown_keys(*mods, "modules.", kModKeys, out.warnings);

    if (auto v = (*mods)["frame_telemetry"].value<bool>()) {
      out.config.modules.frame_telemetry = *v;
    } else if (mods->contains("frame_telemetry")) {
      out.warnings.push_back("modules.frame_telemetry must be boolean; using default");
    }

    if (auto v = (*mods)["presentation"].value<bool>()) {
      out.config.modules.presentation = *v;
    } else if (mods->contains("presentation")) {
      out.warnings.push_back("modules.presentation must be boolean; using default");
    }
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
