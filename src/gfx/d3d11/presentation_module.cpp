#include "gfx/d3d11/presentation_module.h"

#include "core/log.h"
#include "core/runtime_controls.h"
#include "timing/present_policy.h"

#include <MinHook.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <mmsystem.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>

namespace sf::gfx {

namespace {

// IDXGIFactory vtable layout:
//   0..2 IUnknown, 3..6 IDXGIObject, 7 EnumAdapters, 8 MakeWindowAssociation,
//   9 GetWindowAssociation, 10 CreateSwapChain, 11 CreateSoftwareAdapter.
constexpr size_t kSlotCreateSwapChain = 10;

using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*,
                                                      DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);

CreateSwapChainFn g_orig_create_swap_chain = nullptr;
void* g_create_sc_target = nullptr;
bool g_factory_hook_installed = false;
bool g_owns_mh_init = false;

std::mutex g_policy_mu;
sf::Config g_active_cfg = sf::Config::defaults();

std::atomic<bool> g_flip_active{false};
std::atomic<bool> g_tearing_enabled{false};
std::atomic<bool> g_swapchain_windowed{true};
std::atomic<uint64_t> g_upgrade_attempts{0};
std::atomic<uint64_t> g_upgrade_succeeded{0};
std::atomic<uint64_t> g_upgrade_fallbacks{0};

thread_local bool t_in_create_sc = false;

/// Win32 high-resolution clock backend for `sf::timing::FramePacer`.
class WinPacerClock final : public sf::timing::IPacerClock {
public:
  WinPacerClock() {
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    freq_ = (freq.QuadPart > 0) ? static_cast<uint64_t>(freq.QuadPart) : 1'000'000ULL;
  }

  uint64_t now_ns() const override {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const uint64_t ticks = static_cast<uint64_t>(counter.QuadPart);
    const uint64_t whole_sec = ticks / freq_;
    const uint64_t rem_ticks = ticks % freq_;
    return (whole_sec * 1'000'000'000ULL) + ((rem_ticks * 1'000'000'000ULL) / freq_);
  }

  void sleep_ms(uint32_t ms) override { Sleep(static_cast<DWORD>(ms)); }

  void yield_spin() override {
    if (!SwitchToThread()) {
      YieldProcessor();
    }
  }

private:
  uint64_t freq_ = 1'000'000ULL;
};

sf::timing::SwapFormatClass classify_format(DXGI_FORMAT fmt) noexcept {
  switch (fmt) {
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
  case DXGI_FORMAT_R10G10B10A2_UNORM:
    return sf::timing::SwapFormatClass::FlipCompatible;
  default:
    return sf::timing::SwapFormatClass::Unsupported;
  }
}

sf::timing::SwapEffectKind classify_swap_effect(DXGI_SWAP_EFFECT effect) noexcept {
  switch (effect) {
  case DXGI_SWAP_EFFECT_DISCARD:
  case DXGI_SWAP_EFFECT_SEQUENTIAL:
    return sf::timing::SwapEffectKind::BitBlt;
  case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
  case DXGI_SWAP_EFFECT_FLIP_DISCARD:
    return sf::timing::SwapEffectKind::FlipModel;
  default:
    return sf::timing::SwapEffectKind::Other;
  }
}

bool query_tearing_support(IDXGIFactory* factory) noexcept {
  if (factory == nullptr) {
    return false;
  }
  IDXGIFactory5* factory5 = nullptr;
  if (FAILED(
          factory->QueryInterface(__uuidof(IDXGIFactory5), reinterpret_cast<void**>(&factory5))) ||
      factory5 == nullptr) {
    return false;
  }
  BOOL allow_tearing = FALSE;
  const HRESULT hr = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                   &allow_tearing, sizeof(allow_tearing));
  factory5->Release();
  return SUCCEEDED(hr) && allow_tearing == TRUE;
}

HRESULT STDMETHODCALLTYPE hooked_create_swap_chain(IDXGIFactory* factory, IUnknown* device,
                                                   DXGI_SWAP_CHAIN_DESC* desc,
                                                   IDXGISwapChain** swap_chain) {
  if (t_in_create_sc || desc == nullptr || g_orig_create_swap_chain == nullptr) {
    return g_orig_create_swap_chain(factory, device, desc, swap_chain);
  }

  t_in_create_sc = true;

  sf::Config cfg{};
  {
    std::lock_guard lk(g_policy_mu);
    cfg = g_active_cfg;
  }

  sf::timing::SwapChainRequest req{};
  req.buffer_count = static_cast<uint32_t>(desc->BufferCount);
  req.sample_count = static_cast<uint32_t>(desc->SampleDesc.Count);
  req.windowed = (desc->Windowed != FALSE);
  req.swap_effect = classify_swap_effect(desc->SwapEffect);
  req.format = classify_format(desc->BufferDesc.Format);
  req.flags = static_cast<uint32_t>(desc->Flags);
  req.tearing_supported = query_tearing_support(factory);

  const auto decision = sf::timing::evaluate_flip_upgrade(cfg, req);
  g_swapchain_windowed.store(req.windowed, std::memory_order_relaxed);

  HRESULT hr = E_FAIL;
  if (decision.upgrade) {
    ++g_upgrade_attempts;
    DXGI_SWAP_CHAIN_DESC upgraded = *desc;
    upgraded.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    upgraded.BufferCount = static_cast<UINT>(decision.new_buffer_count);
    upgraded.Flags = static_cast<UINT>(decision.new_flags);

    hr = g_orig_create_swap_chain(factory, device, &upgraded, swap_chain);
    if (SUCCEEDED(hr)) {
      ++g_upgrade_succeeded;
      g_flip_active.store(true, std::memory_order_relaxed);
      g_tearing_enabled.store(decision.tearing_enabled, std::memory_order_relaxed);
      SF_LOG("PresentationModule: CreateSwapChain %s", decision.reason.c_str());
    } else {
      ++g_upgrade_fallbacks;
      g_flip_active.store(false, std::memory_order_relaxed);
      g_tearing_enabled.store(false, std::memory_order_relaxed);
      SF_LOG("PresentationModule: FLIP_DISCARD rejected by DXGI (hr=0x%08lx); falling back to "
             "original swap-chain descriptor",
             static_cast<unsigned long>(hr));
      hr = g_orig_create_swap_chain(factory, device, desc, swap_chain);
    }
  } else {
    g_flip_active.store(req.swap_effect == sf::timing::SwapEffectKind::FlipModel,
                        std::memory_order_relaxed);
    g_tearing_enabled.store(decision.tearing_enabled, std::memory_order_relaxed);
    SF_LOG("PresentationModule: CreateSwapChain unchanged (%s)", decision.reason.c_str());
    hr = g_orig_create_swap_chain(factory, device, desc, swap_chain);
  }

  t_in_create_sc = false;
  return hr;
}

bool install_factory_hook() {
  if (g_factory_hook_installed) {
    return true;
  }

  IDXGIFactory1* factory = nullptr;
  const HRESULT hr =
      CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
  if (FAILED(hr) || factory == nullptr) {
    SF_LOG("PresentationModule: CreateDXGIFactory1 failed (hr=0x%08lx)",
           static_cast<unsigned long>(hr));
    return false;
  }

  void** vtable = *reinterpret_cast<void***>(factory);
  g_create_sc_target = vtable[kSlotCreateSwapChain];
  factory->Release();

  const MH_STATUS mh = MH_Initialize();
  if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED) {
    SF_LOG("PresentationModule: MH_Initialize failed (%d)", static_cast<int>(mh));
    g_create_sc_target = nullptr;
    return false;
  }
  g_owns_mh_init = (mh == MH_OK);

  if (MH_CreateHook(g_create_sc_target, reinterpret_cast<void*>(&hooked_create_swap_chain),
                    reinterpret_cast<void**>(&g_orig_create_swap_chain)) != MH_OK) {
    SF_LOG("PresentationModule: MH_CreateHook(CreateSwapChain) failed");
    if (g_owns_mh_init) {
      MH_Uninitialize();
      g_owns_mh_init = false;
    }
    g_create_sc_target = nullptr;
    return false;
  }

  if (MH_EnableHook(g_create_sc_target) != MH_OK) {
    SF_LOG("PresentationModule: MH_EnableHook(CreateSwapChain) failed");
    MH_RemoveHook(g_create_sc_target);
    if (g_owns_mh_init) {
      MH_Uninitialize();
      g_owns_mh_init = false;
    }
    g_create_sc_target = nullptr;
    return false;
  }

  g_factory_hook_installed = true;
  SF_LOG("PresentationModule: IDXGIFactory::CreateSwapChain hooked at %p", g_create_sc_target);
  return true;
}

void remove_factory_hook() {
  if (!g_factory_hook_installed) {
    return;
  }
  if (g_create_sc_target != nullptr) {
    MH_DisableHook(g_create_sc_target);
    MH_RemoveHook(g_create_sc_target);
    g_create_sc_target = nullptr;
  }
  if (g_owns_mh_init) {
    MH_Uninitialize();
    g_owns_mh_init = false;
  }
  g_orig_create_swap_chain = nullptr;
  g_factory_hook_installed = false;
}

} // namespace

struct PresentationModule::Impl {
  WinPacerClock clock;
  sf::timing::FramePacer pacer;
  bool timer_period_active = false;
  bool flip_hook_active = false;
  bool active = false;
};

PresentationModule::PresentationModule() : impl_(std::make_unique<Impl>()) {}

PresentationModule::~PresentationModule() = default;

bool PresentationModule::init(const sf::GameProfile&, const sf::Config& cfg) {
  {
    std::lock_guard lk(g_policy_mu);
    g_active_cfg = cfg;
  }

  g_flip_active.store(false, std::memory_order_relaxed);
  g_tearing_enabled.store(false, std::memory_order_relaxed);
  g_upgrade_attempts.store(0, std::memory_order_relaxed);
  g_upgrade_succeeded.store(0, std::memory_order_relaxed);
  g_upgrade_fallbacks.store(0, std::memory_order_relaxed);

  impl_->pacer.set_target_fps(cfg.present.max_fps);
  if (impl_->pacer.enabled()) {
    if (timeBeginPeriod(1) == TIMERR_NOERROR) {
      impl_->timer_period_active = true;
    }
  }

  if (cfg.present.mode == "flip") {
    impl_->flip_hook_active = install_factory_hook();
    if (!impl_->flip_hook_active) {
      SF_LOG("PresentationModule: continuing without CreateSwapChain flip hook");
    }
  }

  impl_->active = true;
  SF_LOG("PresentationModule active (mode=%s, max_fps=%u, flip_hook=%s)", cfg.present.mode.c_str(),
         cfg.present.max_fps, impl_->flip_hook_active ? "on" : "off");
  return true;
}

void PresentationModule::on_present(sf::PresentCtx& ctx) {
  if (!impl_->active) {
    return;
  }

  if (impl_->pacer.enabled() && sf::runtime_controls().should_run_frame_cap()) {
    impl_->pacer.pace(impl_->clock);
  }

  const auto adj = sf::timing::resolve_present_call(
      ctx.sync_interval, ctx.present_flags, g_flip_active.load(std::memory_order_relaxed),
      g_tearing_enabled.load(std::memory_order_relaxed),
      g_swapchain_windowed.load(std::memory_order_relaxed));
  ctx.sync_interval = adj.sync_interval;
  ctx.present_flags = adj.flags;
}

void PresentationModule::shutdown() {
  if (!impl_->active) {
    return;
  }
  impl_->active = false;

  if (impl_->flip_hook_active) {
    remove_factory_hook();
    impl_->flip_hook_active = false;
  }

  if (impl_->timer_period_active) {
    timeEndPeriod(1);
    impl_->timer_period_active = false;
  }

  const auto& st = impl_->pacer.stats();
  const double avg_wait_ms =
      (st.paced_frames > 0U)
          ? (static_cast<double>(st.total_wait_ns) / static_cast<double>(st.paced_frames) / 1e6)
          : 0.0;
  SF_LOG("PresentationModule shutdown (paced=%llu, missed=%llu, avg_wait=%.3f ms, "
         "flip_attempts=%llu, flip_ok=%llu, flip_fallbacks=%llu)",
         static_cast<unsigned long long>(st.paced_frames),
         static_cast<unsigned long long>(st.missed_deadlines), avg_wait_ms,
         static_cast<unsigned long long>(g_upgrade_attempts.load()),
         static_cast<unsigned long long>(g_upgrade_succeeded.load()),
         static_cast<unsigned long long>(g_upgrade_fallbacks.load()));
}

} // namespace sf::gfx
