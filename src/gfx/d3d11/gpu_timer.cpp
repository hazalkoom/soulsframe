#include "gfx/d3d11/gpu_timer.h"

#include "core/log.h"

#include <d3d11.h>

#include <array>
#include <cstddef>

namespace sf::gfx {

namespace {

/// In-flight frames tracked at once; D3D11 typically delivers results within 1-3 frames.
constexpr size_t kSlotCount = 8;
/// Timestamp queries per slot: up to 128 pass starts plus one end-of-frame stamp.
constexpr size_t kMaxStamps = 129;

/// Owns the queries of one in-flight frame.
struct Slot {
  ID3D11Query* disjoint = nullptr;
  std::array<ID3D11Query*, kMaxStamps> stamps{};
  size_t stamps_used = 0;

  void release_all() {
    if (disjoint) {
      disjoint->Release();
      disjoint = nullptr;
    }
    for (auto& q : stamps) {
      if (q) {
        q->Release();
        q = nullptr;
      }
    }
    stamps_used = 0;
  }
};

} // namespace

struct GpuTimer::Impl {
  std::array<Slot, kSlotCount> slots{};
  timing::GpuSlotRing ring{kSlotCount};
  bool init_attempted = false;
  bool ready = false;
  bool frame_open = false;
  size_t active_slot = 0;
  uint32_t stamped_passes = 0;

  /// Creates every query object against the game's real device (called lazily).
  bool create_queries(ID3D11DeviceContext* ctx) {
    init_attempted = true;
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) {
      return false;
    }

    D3D11_QUERY_DESC disjoint_desc{};
    disjoint_desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    D3D11_QUERY_DESC stamp_desc{};
    stamp_desc.Query = D3D11_QUERY_TIMESTAMP;

    bool ok = true;
    for (auto& slot : slots) {
      if (FAILED(dev->CreateQuery(&disjoint_desc, &slot.disjoint))) {
        ok = false;
        break;
      }
      for (auto& q : slot.stamps) {
        if (FAILED(dev->CreateQuery(&stamp_desc, &q))) {
          ok = false;
          break;
        }
      }
      if (!ok) {
        break;
      }
    }
    dev->Release();

    if (!ok) {
      for (auto& slot : slots) {
        slot.release_all();
      }
      SF_LOG("GpuTimer: D3D11 timestamp queries unavailable; gpu_ms disabled");
      return false;
    }
    SF_LOG("GpuTimer: timestamp queries ready (%zu slots x %zu stamps)", kSlotCount, kMaxStamps);
    return true;
  }

  /// Claims a slot (dropping the oldest unresolved frame if the ring is full) and opens it.
  bool open_frame(ID3D11DeviceContext* ctx) {
    auto slot_idx = ring.begin_frame();
    if (!slot_idx) {
      // GPU is far behind: sacrifice the oldest in-flight frame rather than stall.
      if (const auto oldest = ring.oldest_pending()) {
        ring.release(*oldest);
        slot_idx = ring.begin_frame();
      }
    }
    if (!slot_idx) {
      return false;
    }
    active_slot = *slot_idx;
    Slot& slot = slots[active_slot];
    slot.stamps_used = 0;
    ctx->Begin(slot.disjoint);
    frame_open = true;
    return true;
  }
};

GpuTimer::GpuTimer() : impl_(new Impl) {}

GpuTimer::~GpuTimer() {
  shutdown();
  delete impl_;
}

bool GpuTimer::available() const {
  return impl_->ready;
}

void GpuTimer::on_pass_count(void* ctx_ptr, uint32_t pass_count) {
  auto* ctx = static_cast<ID3D11DeviceContext*>(ctx_ptr);
  if (!ctx) {
    return;
  }
  if (!impl_->init_attempted) {
    impl_->ready = impl_->create_queries(ctx);
  }
  if (!impl_->ready) {
    return;
  }
  while (impl_->stamped_passes < pass_count) {
    if (!impl_->frame_open && !impl_->open_frame(ctx)) {
      return;
    }
    Slot& slot = impl_->slots[impl_->active_slot];
    // Keep one stamp in reserve for end-of-frame so total time is always measurable.
    if (slot.stamps_used + 1 >= kMaxStamps) {
      return;
    }
    ctx->End(slot.stamps[slot.stamps_used++]);
    ++impl_->stamped_passes;
  }
}

void GpuTimer::end_frame(void* ctx_ptr, uint64_t frame_index) {
  auto* ctx = static_cast<ID3D11DeviceContext*>(ctx_ptr);
  impl_->stamped_passes = 0;
  if (!ctx || !impl_->ready || !impl_->frame_open) {
    return;
  }
  Slot& slot = impl_->slots[impl_->active_slot];
  ctx->End(slot.stamps[slot.stamps_used++]);
  ctx->End(slot.disjoint);
  impl_->ring.submit_frame(impl_->active_slot, frame_index);
  impl_->frame_open = false;
}

std::vector<GpuFrameResult> GpuTimer::poll(void* ctx_ptr) {
  std::vector<GpuFrameResult> out;
  auto* ctx = static_cast<ID3D11DeviceContext*>(ctx_ptr);
  if (!ctx || !impl_->ready) {
    return out;
  }

  while (const auto pending = impl_->ring.oldest_pending()) {
    Slot& slot = impl_->slots[*pending];
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
    const HRESULT hr =
        ctx->GetData(slot.disjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (hr == S_FALSE) {
      break; // not ready yet; try again next frame without blocking
    }
    const uint64_t frame_index = impl_->ring.frame_of(*pending);
    if (hr != S_OK) {
      impl_->ring.release(*pending); // device error: drop this frame, keep going
      continue;
    }

    std::vector<uint64_t> ticks;
    ticks.reserve(slot.stamps_used);
    bool all_ready = true;
    for (size_t i = 0; i < slot.stamps_used; ++i) {
      UINT64 value = 0;
      if (ctx->GetData(slot.stamps[i], &value, sizeof(value), D3D11_ASYNC_GETDATA_DONOTFLUSH) !=
          S_OK) {
        all_ready = false;
        break;
      }
      ticks.push_back(value);
    }
    if (!all_ready) {
      break;
    }

    out.push_back(GpuFrameResult{
        .frame_index = frame_index,
        .timing = timing::resolve_gpu_frame(ticks, disjoint.Frequency, disjoint.Disjoint != FALSE),
    });
    impl_->ring.release(*pending);
  }
  return out;
}

void GpuTimer::shutdown() {
  for (auto& slot : impl_->slots) {
    slot.release_all();
  }
  impl_->ready = false;
  impl_->frame_open = false;
  impl_->stamped_passes = 0;
}

} // namespace sf::gfx
