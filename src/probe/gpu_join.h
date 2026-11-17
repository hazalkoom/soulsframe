#pragma once

#include "probe/trace_types.h"
#include "timing/gpu_timing.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace sf::probe {

/// A finished frame ready to be serialized to JSONL, optionally enriched with GPU timings.
struct ReadyFrame {
  FrameTraceSnapshot snap;
  double cpu_frame_ms = 0.0;
};

/// Holds per-frame trace snapshots until the matching GPU timestamp results arrive.
/// Why: D3D11 query results become available 1-3 frames after submission and reading them
/// earlier would stall the GPU pipeline. Instead of blocking, we delay serialization of each
/// frame by a few frames, attach `gpu_ms` when the result lands, and emit frames in order.
/// Pure C++ (no D3D11) so the delaying/ordering rules are unit-tested on Linux.
class GpuFrameJoiner {
public:
  /// `max_hold_frames` bounds the delay (the probe uses 8, matching the 8 GPU query slots): frames
  /// older than this are emitted without GPU data (query lost, driver too slow, or timing
  /// disabled). Clamped to >= 1.
  explicit GpuFrameJoiner(size_t max_hold_frames = 4);

  /// Queues a completed frame snapshot (CPU side) awaiting GPU timing.
  void push(FrameTraceSnapshot snap, double cpu_frame_ms);

  /// Attaches a resolved GPU timing to the held frame with `frame_index`. Ignored when that
  /// frame is unknown (already emitted) or `timing.valid` is false (frame then times out).
  void on_gpu_result(uint64_t frame_index, const timing::GpuFrameTiming& timing);

  /// Pops frames in submission order. A frame is released when its GPU result arrived, when
  /// more than `max_hold_frames` newer frames are queued behind it, or when `flush_all`.
  std::vector<ReadyFrame> take_ready(bool flush_all = false);

  /// Number of frames still waiting for GPU data.
  size_t held_count() const { return held_.size(); }

private:
  struct Held {
    ReadyFrame frame;
    bool resolved = false;
  };

  size_t max_hold_ = 4;
  std::deque<Held> held_;
};

} // namespace sf::probe
