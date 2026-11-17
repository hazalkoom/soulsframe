#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace sf::timing {

/// Converts two GPU timestamp counters into milliseconds.
/// Why: D3D11 timestamps are raw ticks that are only meaningful with the frequency from a
/// non-disjoint `D3D11_QUERY_TIMESTAMP_DISJOINT` query. Returns a negative value (-1.0) when the
/// measurement is unusable (disjoint, zero frequency, or end < begin) so callers can omit it
/// instead of reporting garbage.
double gpu_ticks_to_ms(uint64_t begin_ticks, uint64_t end_ticks, uint64_t frequency_hz,
                       bool disjoint) noexcept;

/// Per-frame GPU timing resolved from boundary timestamps.
struct GpuFrameTiming {
  bool valid = false;             ///< False when any input made the frame unusable.
  double total_ms = 0.0;          ///< Time from the first to the last boundary stamp.
  std::vector<double> segment_ms; ///< Segment i spans boundary stamps[i]..stamps[i+1].
};

/// Resolves N+1 boundary timestamps into N per-pass segments plus a frame total.
/// Why: the probe stamps once at the start of every pass and once at end of frame, so pass k
/// (1-based) maps to segment k-1. Needs at least 2 stamps and monotonic non-decreasing ticks.
GpuFrameTiming resolve_gpu_frame(std::span<const uint64_t> stamps, uint64_t frequency_hz,
                                 bool disjoint);

/// Fixed-size ring of in-flight GPU query slots (pure bookkeeping, no D3D11 types).
/// Why: GPU query results arrive 1-3 frames after submission. Reading them immediately would
/// stall the pipeline, so each frame records into its own slot and is resolved later in FIFO
/// order. The D3D11 layer owns one query set per slot index.
class GpuSlotRing {
public:
  /// Creates a ring with `capacity` slots (clamped to >= 1).
  explicit GpuSlotRing(size_t capacity);

  /// Claims a free slot for recording; nullopt when every slot is still in flight.
  std::optional<size_t> begin_frame();

  /// Marks a recording slot as submitted (awaiting GPU results) for `frame_index`.
  void submit_frame(size_t slot, uint64_t frame_index);

  /// Oldest submitted-but-unresolved slot, if any.
  std::optional<size_t> oldest_pending() const;

  /// Frame index stored by `submit_frame` for `slot` (0 for unknown slots).
  uint64_t frame_of(size_t slot) const;

  /// Returns `slot` to the free pool (after resolving or abandoning it).
  void release(size_t slot);

  /// Total slot count.
  size_t capacity() const { return states_.size(); }

  /// Number of submitted slots awaiting resolution.
  size_t pending_count() const { return pending_.size(); }

private:
  enum class State : uint8_t { Free, Recording, Pending };
  std::vector<State> states_;
  std::vector<uint64_t> frames_;
  std::deque<size_t> pending_;
};

} // namespace sf::timing
