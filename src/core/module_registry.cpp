#include "core/module_registry.h"

#include "games/ds3/profile.h"

#include <algorithm>

namespace sf {

void ModuleRegistry::register_module(std::unique_ptr<Module> mod) {
  if (!mod) {
    return;
  }
  entries_.push_back(Entry{.mod = std::move(mod), .active = false});
}

void ModuleRegistry::init_all(const GameProfile& profile, const Config& cfg) {
  statuses_.clear();
  statuses_.reserve(entries_.size());

  for (auto& entry : entries_) {
    const std::string mod_name = entry.mod->name();

    if (!entry.mod->is_enabled(cfg)) {
      entry.active = false;
      statuses_.push_back({.name = mod_name, .active = false, .detail = "disabled by config"});
      continue;
    }

    if (entry.mod->requires_engine_hooks() && !profile.engine_hooks_allowed) {
      entry.active = false;
      statuses_.push_back({.name = mod_name,
                           .active = false,
                           .detail = "refused: unknown exe hash (engine hooks disabled)"});
      continue;
    }

    bool ok = false;
    try {
      ok = entry.mod->init(profile, cfg);
    } catch (...) {
      ok = false;
    }

    entry.active = ok;
    statuses_.push_back({.name = mod_name, .active = ok, .detail = ok ? "active" : "init failed"});
  }
}

void ModuleRegistry::on_present(PresentCtx& ctx) {
  for (size_t i = 0; i < entries_.size(); ++i) {
    auto& entry = entries_[i];
    if (!entry.active) {
      continue;
    }
    // Isolate per-frame faults so a single misbehaving module cannot crash Present().
    try {
      entry.mod->on_present(ctx);
    } catch (...) {
      entry.active = false;
      if (i < statuses_.size()) {
        statuses_[i].active = false;
        statuses_[i].detail = "disabled after exception in on_present";
      }
    }
  }
}

void ModuleRegistry::shutdown_all() {
  for (size_t i = entries_.size(); i > 0; --i) {
    const size_t idx = i - 1;
    auto& entry = entries_[idx];
    if (!entry.active) {
      continue;
    }
    try {
      entry.mod->shutdown();
    } catch (...) {
      if (idx < statuses_.size()) {
        statuses_[idx].detail = "exception during shutdown";
      }
    }
    entry.active = false;
    if (idx < statuses_.size()) {
      statuses_[idx].active = false;
    }
  }
}

size_t ModuleRegistry::active_count() const {
  return static_cast<size_t>(
      std::ranges::count_if(entries_, [](const Entry& entry) { return entry.active; }));
}

} // namespace sf
