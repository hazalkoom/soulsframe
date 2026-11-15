#pragma once

#include "core/module.h"

#include <memory>
#include <string>
#include <vector>

namespace sf {

/// Diagnostic record capturing why each registered module is active or inactive.
struct ModuleStatus {
  std::string name;
  bool active = false;
  std::string detail;
};

/// Owns registered `Module` instances and isolates initialization failures per module.
class ModuleRegistry {
public:
  /// Adds a module to the registry (null pointers are ignored).
  void register_module(std::unique_ptr<Module> mod);

  /// Initializes enabled modules whose safety preconditions (such as verified exe hash) pass.
  /// Exceptions or `false` returns disable only the failing module so the game stays runnable.
  void init_all(const GameProfile& profile, const Config& cfg);

  /// Dispatches per-frame updates to all currently active modules.
  /// Exceptions thrown by a module disable only that module and mark its status inactive.
  void on_present(PresentCtx& ctx);

  /// Shuts down active modules in reverse initialization order, isolating any exceptions
  /// so remaining modules still clean up cleanly.
  void shutdown_all();

  /// Returns the status ledger populated by the most recent `init_all()` call.
  const std::vector<ModuleStatus>& statuses() const { return statuses_; }

  /// Returns the number of modules currently marked active.
  size_t active_count() const;

private:
  struct Entry {
    std::unique_ptr<Module> mod;
    bool active = false;
  };
  std::vector<Entry> entries_;
  std::vector<ModuleStatus> statuses_;
};

} // namespace sf
