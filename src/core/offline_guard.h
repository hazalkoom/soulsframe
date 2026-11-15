#pragma once

#include <string>
#include <vector>

namespace sf {

/// Inputs inspected before allowing SoulsFrame to install runtime hooks.
struct OfflineCheckInput {
  /// Value of `general.offline_acknowledged` from `soulsframe.toml`.
  bool config_offline_acknowledged = false;

  /// Command-line arguments passed to the host process.
  std::vector<std::string> command_line_args;

  /// True if runtime detection observes an active online session marker.
  bool force_online_detected = false;
};

/// Result of evaluating the offline-only safety policy.
struct OfflineVerdict {
  /// True only if offline usage is explicitly acknowledged and no online markers exist.
  bool allowed = false;

  /// Human-readable explanation logged when initialization is allowed or blocked.
  std::string reason;
};

/// Evaluates whether the mod is permitted to activate hooks.
/// Refusing hooks when unacknowledged protects users from accidental anti-cheat/ban triggers.
OfflineVerdict evaluate_offline_guard(const OfflineCheckInput& input);

} // namespace sf
