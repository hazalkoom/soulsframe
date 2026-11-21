#include <doctest/doctest.h>

#include "core/config.h"
#include "core/module_registry.h"
#include "games/ds3/profile.h"

#include <stdexcept>

namespace {

/// Configurable test double implementing `sf::Module` with optional throw injection.
class StubModule final : public sf::Module {
public:
  StubModule(const char* name, bool enabled, bool needs_engine, bool init_ok, int* present_counter,
             bool* shutdown_flag, bool throw_init = false, bool throw_present = false,
             bool throw_shutdown = false, int* resize_counter = nullptr, bool throw_resize = false)
      : name_(name), enabled_(enabled), needs_engine_(needs_engine), init_ok_(init_ok),
        present_counter_(present_counter), shutdown_flag_(shutdown_flag),
        resize_counter_(resize_counter), throw_init_(throw_init), throw_present_(throw_present),
        throw_shutdown_(throw_shutdown), throw_resize_(throw_resize) {}

  const char* name() const override { return name_; }
  bool requires_engine_hooks() const override { return needs_engine_; }
  bool is_enabled(const sf::Config&) const override { return enabled_; }
  bool init(const sf::GameProfile&, const sf::Config&) override {
    if (throw_init_) {
      throw std::runtime_error("injected init failure");
    }
    return init_ok_;
  }
  void on_present(sf::PresentCtx&) override {
    if (present_counter_) {
      ++(*present_counter_);
    }
    if (throw_present_) {
      throw std::runtime_error("injected on_present failure");
    }
  }
  void on_resize() override {
    if (resize_counter_) {
      ++(*resize_counter_);
    }
    if (throw_resize_) {
      throw std::runtime_error("injected on_resize failure");
    }
  }
  void shutdown() override {
    if (shutdown_flag_) {
      *shutdown_flag_ = true;
    }
    if (throw_shutdown_) {
      throw std::runtime_error("injected shutdown failure");
    }
  }

private:
  const char* name_;
  bool enabled_;
  bool needs_engine_;
  bool init_ok_;
  int* present_counter_;
  bool* shutdown_flag_;
  int* resize_counter_;
  bool throw_init_;
  bool throw_present_;
  bool throw_shutdown_;
  bool throw_resize_;
};

} // namespace

TEST_CASE("module registry isolates failures, honors kill switches and unknown exe hashes") {
  sf::ModuleRegistry reg;
  int p_ok = 0, p_fail = 0, p_disabled = 0, p_engine = 0;
  bool s_ok = false, s_fail = false;

  reg.register_module(nullptr); // Ignored safely.
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

TEST_CASE("module registry isolates exceptions in init, on_present, and shutdown") {
  sf::ModuleRegistry reg;
  int p_healthy = 0, p_throw_present = 0;
  bool s_healthy = false, s_throw_shutdown = false;

  reg.register_module(
      std::make_unique<StubModule>("healthy", true, false, true, &p_healthy, &s_healthy));
  reg.register_module(std::make_unique<StubModule>("throw_init", true, false, true, nullptr,
                                                   nullptr, true, false, false));
  reg.register_module(std::make_unique<StubModule>("throw_present", true, false, true,
                                                   &p_throw_present, nullptr, false, true, false));
  reg.register_module(std::make_unique<StubModule>("throw_shutdown", true, false, true, nullptr,
                                                   &s_throw_shutdown, false, false, true));

  const auto profile = sf::games::ds3::resolve_profile("");
  const auto cfg = sf::Config::defaults();
  reg.init_all(profile, cfg);

  REQUIRE(reg.statuses().size() == 4);
  CHECK(reg.statuses()[0].active);
  CHECK_FALSE(reg.statuses()[1].active); // throw_init disabled safely
  CHECK(reg.statuses()[2].active);
  CHECK(reg.statuses()[3].active);
  CHECK(reg.active_count() == 3);

  sf::PresentCtx ctx{1, 16.6, nullptr};
  reg.on_present(ctx);
  // Second frame: throw_present must already be disabled and not invoked again.
  ctx.frame_index = 2;
  reg.on_present(ctx);

  CHECK(p_healthy == 2);
  CHECK(p_throw_present == 1);
  CHECK_FALSE(reg.statuses()[2].active);
  CHECK(reg.active_count() == 2);

  // shutdown_all must catch throw_shutdown's exception and still shut down healthy module.
  reg.shutdown_all();
  CHECK(s_throw_shutdown);
  CHECK(s_healthy);
  CHECK(reg.active_count() == 0);
}

TEST_CASE("module registry dispatches on_resize and isolates exceptions") {
  sf::ModuleRegistry reg;
  int r_healthy = 0, r_throw = 0, r_off = 0;

  reg.register_module(std::make_unique<StubModule>("healthy", true, false, true, nullptr, nullptr,
                                                   false, false, false, &r_healthy, false));
  reg.register_module(std::make_unique<StubModule>("throw_resize", true, false, true, nullptr,
                                                   nullptr, false, false, false, &r_throw, true));
  reg.register_module(std::make_unique<StubModule>("off", false, false, true, nullptr, nullptr,
                                                   false, false, false, &r_off, false));

  const auto profile = sf::games::ds3::resolve_profile("");
  const auto cfg = sf::Config::defaults();
  reg.init_all(profile, cfg);
  REQUIRE(reg.active_count() == 2);

  reg.on_resize();
  reg.on_resize();

  CHECK(r_healthy == 2);
  CHECK(r_throw == 1);
  CHECK(r_off == 0);
  CHECK(reg.active_count() == 1);
  CHECK_FALSE(reg.statuses()[1].active);
  CHECK(reg.statuses()[1].detail == "disabled after exception in on_resize");
}
