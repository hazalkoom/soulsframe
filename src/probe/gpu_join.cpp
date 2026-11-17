#include "probe/gpu_join.h"

#include <algorithm>
#include <utility>

namespace sf::probe {

GpuFrameJoiner::GpuFrameJoiner(size_t max_hold_frames)
    : max_hold_(std::max<size_t>(1, max_hold_frames)) {}

void GpuFrameJoiner::push(FrameTraceSnapshot snap, double cpu_frame_ms) {
  held_.push_back(Held{.frame = ReadyFrame{.snap = std::move(snap), .cpu_frame_ms = cpu_frame_ms},
                       .resolved = false});
}

void GpuFrameJoiner::on_gpu_result(uint64_t frame_index, const timing::GpuFrameTiming& timing) {
  if (!timing.valid) {
    return;
  }
  const auto it = std::ranges::find_if(
      held_, [&](const Held& h) { return h.frame.snap.frame_index == frame_index; });
  if (it == held_.end()) {
    return;
  }
  auto& snap = it->frame.snap;
  snap.gpu_frame_ms = timing.total_ms;
  // Segments map 1:1 to passes only when the recorder did not cap/merge passes differently
  // from the GPU stamps; otherwise keep just the frame total rather than mislabel passes.
  if (timing.segment_ms.size() == snap.passes.size()) {
    for (size_t i = 0; i < snap.passes.size(); ++i) {
      snap.passes[i].gpu_ms = timing.segment_ms[i];
    }
  }
  it->resolved = true;
}

std::vector<ReadyFrame> GpuFrameJoiner::take_ready(bool flush_all) {
  std::vector<ReadyFrame> out;
  while (!held_.empty()) {
    const bool overdue = held_.size() > max_hold_;
    if (!flush_all && !held_.front().resolved && !overdue) {
      break;
    }
    out.push_back(std::move(held_.front().frame));
    held_.pop_front();
  }
  return out;
}

} // namespace sf::probe
