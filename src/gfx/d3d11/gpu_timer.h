#pragma once

#include "timing/gpu_timing.h"

#include <cstdint>
#include <vector>

namespace sf::gfx {

/// One resolved GPU timing for a previously submitted frame.
struct GpuFrameResult {
  uint64_t frame_index = 0;
  timing::GpuFrameTiming timing;
};

/// Places a GPU timestamp at the start of each traced pass and at end of frame, using D3D11
/// `TIMESTAMP` / `TIMESTAMP_DISJOINT` queries (probe builds only).
///
/// Why this shape:
///  - Results are polled with `D3D11_ASYNC_GETDATA_DONOTFLUSH` and never waited on, so the
///    measurement does not stall the GPU pipeline or distort the numbers it reports.
///  - A small ring of query slots lets several frames be in flight at once.
///  - Everything is touched only from the render thread that owns the immediate context.
///
/// All functions are no-ops that return quickly when the driver lacks query support, so a
/// failure degrades to "no gpu_ms in the capture" instead of crashing the game.
///
/// `ctx` is an opaque `ID3D11DeviceContext*`; kept `void*` so this header needs no D3D11.
class GpuTimer {
public:
  GpuTimer();
  ~GpuTimer();
  GpuTimer(const GpuTimer&) = delete;
  GpuTimer& operator=(const GpuTimer&) = delete;

  /// Stamps a timestamp for every pass opened since the last call (`pass_count` is the
  /// recorder's running pass total for this frame). Lazily creates queries on first use.
  void on_pass_count(void* ctx, uint32_t pass_count);

  /// Closes the frame: final timestamp, ends the disjoint query, and queues it for polling.
  void end_frame(void* ctx, uint64_t frame_index);

  /// Collects every in-flight frame whose GPU results are ready (non-blocking, FIFO).
  std::vector<GpuFrameResult> poll(void* ctx);

  /// Releases all D3D11 query objects (safe to call repeatedly).
  void shutdown();

  /// True once queries were created successfully and are in use.
  bool available() const;

private:
  struct Impl;
  Impl* impl_;
};

} // namespace sf::gfx
