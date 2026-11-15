#include <doctest/doctest.h>

#include "core/config.h"
#include "core/module_registry.h"
#include "games/ds3/profile.h"

namespace {

class StubModule final : public sf::Module {
public:
  StubModule(const char* name, bool enabled, bool needs_engine, bool init_ok, int* present_counter,
             bool* shutdown_flag)
      : name_(name), enabled_(enabled), needs_engine_(needs_engine), init_ok_(init_ok),
        present_counter_(present_counter), shutdown_flag_(shutdown_flag) {}

  const char* name() const override { return name_; }
  bool requires_engine_hooks() const override { return needs_engine_; }
  bool is_enabled(const sf::Config&) const override { return enabled_; }
  bool init(const sf::GameProfile&, const sf::Config&) override { return init_ok_; }
  void on_present(sf::PresentCtx&) override {
    if (present_counter_)
      ++(*present_counter_);
  }
  void shutdown() override {
    if (shutdown_flag_)
      *shutdown_flag_ = true;
  }

private:
  const char* name_;
  bool enabled_;
  bool needs_engine_;
  bool init_ok_;
  int* present_counter_;
  bool* shutdown_flag_;
};

} // namespace

TEST_CASE("module registry isolates failures, honors kill switches and unknown exe hashes") {
  sf::ModuleRegistry reg;
  int p_ok = 0, p_fail = 0, p_disabled = 0, p_engine = 0;
  bool s_ok = false, s_fail = false;

  reg.register_module(std::make_unique<StubModule>("ok_mod", true, false, true, &p_ok, &s_ok));
  reg.register_module(
      std::make_unique<StubModule>("fail_mod", true, false, false, &p_fail, &s_fail));
  reg.register_module(
      std::make_unique<StubModule>("off_mod", false, false, true, &p_disabled, nullptr));
  reg.register_module(
      std::make_unique<StubModule>("engine_mod", true, true, true, &p_engine, nullptr));

  auto profile = sf::games::ds3::resolve_profile("unknown_hash");
  const auto cfg = sf::Config::defaults();

  reg.init_all(profile, cfg);
  CHECK(reg.active_count() == 1);
  REQUIRE(reg.statuses().size() == 4);
  CHECK(reg.statuses()[0].active);
  CHECK_FALSE(reg.statuses()[1].active);
  CHECK_FALSE(reg.statuses()[2].active);
  CHECK_FALSE(reg.statuses()[3].active);

  sf::PresentCtx ctx{1, 16.6, nullptr};
  reg.on_present(ctx);
  CHECK(p_ok == 1);
  CHECK(p_fail == 0);
  CHECK(p_disabled == 0);
  CHECK(p_engine == 0);

  reg.shutdown_all();
  CHECK(s_ok);
  CHECK_FALSE(s_fail);
  CHECK(reg.active_count() == 0);
}
