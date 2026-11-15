#include "core/module_registry.h"

#include "games/ds3/profile.h"

namespace sf {

void ModuleRegistry::register_module(std::unique_ptr<Module> mod) {
  if (!mod)
    return;
  entries_.push_back(Entry{std::move(mod), false});
}

void ModuleRegistry::init_all(const GameProfile& profile, const Config& cfg) {
  statuses_.clear();
  statuses_.reserve(entries_.size());

  for (auto& entry : entries_) {
    const std::string mod_name = entry.mod->name();

    if (!entry.mod->is_enabled(cfg)) {
      entry.active = false;
      statuses_.push_back({mod_name, false, "disabled by config"});
      continue;
    }

    if (entry.mod->requires_engine_hooks() && !profile.engine_hooks_allowed) {
      entry.active = false;
      statuses_.push_back({mod_name, false, "refused: unknown exe hash (engine hooks disabled)"});
      continue;
    }

    bool ok = false;
    try {
      ok = entry.mod->init(profile, cfg);
    } catch (...) {
      ok = false;
    }

    entry.active = ok;
    statuses_.push_back({mod_name, ok, ok ? "active" : "init failed"});
  }
}

void ModuleRegistry::on_present(PresentCtx& ctx) {
  for (auto& entry : entries_) {
    if (entry.active) {
      entry.mod->on_present(ctx);
    }
  }
}

void ModuleRegistry::shutdown_all() {
  for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
    if (it->active) {
      it->mod->shutdown();
      it->active = false;
    }
  }
}

size_t ModuleRegistry::active_count() const {
  size_t count = 0;
  for (const auto& entry : entries_) {
    if (entry.active) {
      ++count;
    }
  }
  return count;
}

} // namespace sf
