#include "timing/fps_unlock_module.h"

#include "core/log.h"

namespace sf::timing {

struct FpsUnlockModule::Impl {
  FixedStepStepper stepper;
  FpsSafetyVerdict verdict{};
  Config::FpsUnlock cfg{};
  bool active = false;
};

FpsUnlockModule::FpsUnlockModule() : impl_(std::make_unique<Impl>()) {}

FpsUnlockModule::~FpsUnlockModule() = default;

bool FpsUnlockModule::init(const sf::GameProfile& profile, const sf::Config& cfg) {
  impl_->cfg = cfg.fps_unlock;
  impl_->verdict = evaluate_fps_unlock_safety(profile, cfg);
  if (!impl_->verdict.module_enabled) {
    return false;
  }

  impl_->stepper.configure(cfg.fps_unlock);
  impl_->active = true;

  SF_LOG("FpsUnlockModule active (strategy=%s, target_fps=%u, sim_tick_hz=%u, "
         "max_substeps=%u, dry_run=%s)",
         impl_->cfg.strategy.c_str(), impl_->cfg.target_fps, impl_->cfg.sim_tick_hz,
         impl_->cfg.max_substeps, impl_->verdict.dry_run_only ? "true" : "false");
  SF_LOG("FpsUnlockModule policy: %s", impl_->verdict.reason.c_str());
  return true;
}

void FpsUnlockModule::on_present(sf::PresentCtx& ctx) {
  if (!impl_->active) {
    return;
  }
  impl_->stepper.advance(ctx.delta_ms);
}

void FpsUnlockModule::shutdown() {
  if (!impl_->active) {
    return;
  }
  impl_->active = false;
  const auto& st = impl_->stepper.stats();
  SF_LOG("FpsUnlockModule shutdown (frames=%llu, sim_ticks=%llu, interp_frames=%llu, "
         "multi_tick=%llu, spiral_clamps=%llu)",
         static_cast<unsigned long long>(st.render_frames),
         static_cast<unsigned long long>(st.total_sim_ticks),
         static_cast<unsigned long long>(st.interpolated_frames),
         static_cast<unsigned long long>(st.multi_tick_frames),
         static_cast<unsigned long long>(st.spiral_clamps));
}

} // namespace sf::timing
