#pragma once

#include "core/module.h"
#include "probe/gpu_join.h"
#include "probe/trace_types.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace sf::probe {

/// Probe-only `sf::Module` that captures D3D11 immediate-context draw calls,
/// pass boundaries, and GPU resources and writes them asynchronously to JSONL.
class ProbeModule final : public sf::Module {
public:
  ProbeModule(std::string output_dir, std::string exe_sha256);
  ~ProbeModule() override;

  const char* name() const override { return "probe_tracer"; }
  bool is_enabled(const sf::Config& cfg) const override;

  /// Opens the configured JSONL output path, spawns the writer thread, and hooks D3D11 context.
  bool init(const sf::GameProfile& profile, const sf::Config& cfg) override;

  /// Closes the current traced frame, queues JSONL lines for async flush, and starts the next.
  void on_present(sf::PresentCtx& ctx) override;

  /// Unhooks D3D11 context draws, drains pending JSONL lines, and closes the writer thread.
  void shutdown() override;

private:
  static void on_draw_trampoline(const DrawCallState& state, void* user);
  static void on_resource_trampoline(const ResourceSummary& resource, void* user);
  static uint32_t pass_count_trampoline(void* user);

  void record_draw_event(const DrawCallState& state);
  void record_resource_event(const ResourceSummary& resource);
  void enqueue_lines(std::vector<std::string> lines, bool finalize_after);
  void stop_writer(bool wait_without_join);
  void writer_loop();

  std::string output_dir_;
  std::string exe_sha256_;
  std::string output_path_;

  uint32_t max_capture_frames_ = 120;
  uint32_t captured_frames_ = 0;
  std::atomic<bool> capture_stopped_{false};
  std::atomic<bool> writer_done_{false};

  std::mutex trace_mu_;
  TraceRecorder recorder_;

  /// Delays frame serialization until GPU timestamps resolve (render thread only).
  GpuFrameJoiner joiner_{8};
  bool gpu_timing_ = false;

  std::ofstream out_stream_;
  std::mutex queue_mu_;
  std::condition_variable queue_cv_;
  std::deque<std::string> pending_lines_;
  bool writer_stop_ = false;
  std::thread writer_thread_;
};

} // namespace sf::probe
