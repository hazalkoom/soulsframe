#pragma once

#include "core/config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

/// Severity level for static/startup configuration diagnostics.
enum class DiagnosticSeverity : uint8_t { Info, Warning, Error };

/// Single configuration diagnostic finding emitted before hooks are installed.
struct ConfigDiagnostic {
  DiagnosticSeverity severity = DiagnosticSeverity::Info;
  std::string code;
  std::string message;
};

/// Build and runtime context supplied to `diagnose_config()`.
struct DiagnosticContext {
  bool is_probe_build = false;
  bool exe_hooks_allowed = false;
};

/// Returns uppercase ASCII label (`"INFO"`, `"WARN"`, or `"ERROR"`) for a diagnostic severity.
const char* severity_name(DiagnosticSeverity s) noexcept;

/// Validates cross-field interactions across `[general]`, `[present]`, `[modules]`, and module
/// sub-tables without requiring Win32 or D3D11 headers.
std::vector<ConfigDiagnostic> diagnose_config(const Config& cfg, const DiagnosticContext& ctx = {});

/// Formats a concise one-line configuration summary suitable for startup logs and `sftool`.
std::string format_config_summary(const Config& cfg);

} // namespace sf
