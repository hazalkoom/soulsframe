#include "probe/probe_module.h"

#include "core/config.h"
#include "core/log.h"
#include "games/ds3/profile.h"
#include "gfx/d3d11/context_hook.h"
#include "probe/jsonl_writer.h"

#include <chrono>
#include <filesystem>
#include <iterator>
#include <utility>
#include <vector>

namespace sf::probe {

ProbeModule::ProbeModule(std::string output_dir, std::string exe_sha256)
    : output_dir_(std::move(output_dir)), exe_sha256_(std::move(exe_sha256)) {}

ProbeModule::~ProbeModule() {
  shutdown();
}

bool ProbeModule::is_enabled(const sf::Config& cfg) const {
  return cfg.modules.probe_tracer;
}

bool ProbeModule::init(const sf::GameProfile& profile, const sf::Config& cfg) {
  max_capture_frames_ = cfg.probe.capture_frames;
  captured_frames_ = 0;
  capture_stopped_.store(false, std::memory_order_release);

  const std::filesystem::path out_path = std::filesystem::path(output_dir_) / cfg.probe.output_file;
  output_path_ = out_path.string();

  out_stream_.open(out_path, std::ios::out | std::ios::trunc);
  if (!out_stream_.is_open()) {
    SF_LOG("ProbeModule: failed to open capture file '%s'", output_path_.c_str());
    return false;
  }

  {
    std::lock_guard lk(trace_mu_);
    recorder_.reset();
    recorder_.begin_frame(1);
  }

  SessionHeaderInfo hdr;
  hdr.mod_version = "0.2.0";
  hdr.game = profile.id;
  hdr.exe_sha256 = exe_sha256_;
  hdr.build_mode = "probe";

  {
    std::lock_guard lk(queue_mu_);
    pending_lines_.clear();
    writer_stop_ = false;
    writer_done_.store(false, std::memory_order_release);
    pending_lines_.push_back(format_session_header(hdr));
  }
  writer_thread_ = std::thread(&ProbeModule::writer_loop, this);

  gfx::ContextHookCallbacks cbs;
  cbs.on_draw = &ProbeModule::on_draw_trampoline;
  cbs.on_resource = &ProbeModule::on_resource_trampoline;
  cbs.pass_count = &ProbeModule::pass_count_trampoline;
  cbs.trace_resources = cfg.probe.trace_resources;
  cbs.gpu_timing = cfg.probe.gpu_timing;
  cbs.user = this;
  gpu_timing_ = cfg.probe.gpu_timing;
  joiner_ = GpuFrameJoiner(8);

  if (!gfx::install_context_hooks(cbs)) {
    SF_LOG("ProbeModule: D3D11 context hook install failed");
    shutdown();
    return false;
  }

  SF_LOG("ProbeModule active: capturing up to %u frames -> %s", max_capture_frames_,
         output_path_.c_str());
  return true;
}

void ProbeModule::on_present(sf::PresentCtx& ctx) {
  if (capture_stopped_.load(std::memory_order_acquire)) {
    return;
  }

  FrameTraceSnapshot snap;
  bool hit_limit = false;
  uint32_t count_after = 0;

  {
    std::lock_guard lk(trace_mu_);
    snap = recorder_.end_frame();
    snap.frame_index = ctx.frame_index;
    ++captured_frames_;
    count_after = captured_frames_;
    if (captured_frames_ >= max_capture_frames_) {
      hit_limit = true;
      capture_stopped_.store(true, std::memory_order_release);
    } else {
      recorder_.begin_frame(ctx.frame_index + 1);
    }
  }

  std::vector<std::string> lines;
  if (gpu_timing_) {
    // Close the GPU-timed frame, then attach any results that finished (older frames).
    for (const auto& result : gfx::gpu_timing_present(ctx.swap_chain, ctx.frame_index)) {
      joiner_.on_gpu_result(result.frame_index, result.timing);
    }
    joiner_.push(std::move(snap), ctx.delta_ms);
    for (const auto& ready : joiner_.take_ready(hit_limit)) {
      auto frame_lines = serialize_frame_snapshot(ready.snap, ready.cpu_frame_ms);
      lines.insert(lines.end(), std::make_move_iterator(frame_lines.begin()),
                   std::make_move_iterator(frame_lines.end()));
    }
  } else {
    lines = serialize_frame_snapshot(snap, ctx.delta_ms);
  }
  enqueue_lines(std::move(lines), hit_limit);

  if (hit_limit) {
    // Joining from Present (outside DllMain loader lock) finalizes the capture cleanly.
    stop_writer(false);
    SF_LOG("ProbeModule reached capture_frames limit (%u); trace finalized -> %s", count_after,
           output_path_.c_str());
  }
}

void ProbeModule::shutdown() {
  capture_stopped_.store(true, std::memory_order_release);
  gfx::remove_context_hooks();
  // shutdown() runs during DLL_PROCESS_DETACH under the OS loader lock: wait for the
  // writer stream flush via writer_done_ and detach instead of calling thread::join().
  stop_writer(true);
}

void ProbeModule::on_draw_trampoline(const DrawCallState& state, void* user) {
  if (auto* self = static_cast<ProbeModule*>(user)) {
    self->record_draw_event(state);
  }
}

uint32_t ProbeModule::pass_count_trampoline(void* user) {
  auto* self = static_cast<ProbeModule*>(user);
  if (!self) {
    return 0;
  }
  std::lock_guard lk(self->trace_mu_);
  return self->recorder_.pass_count();
}

void ProbeModule::on_resource_trampoline(const ResourceSummary& resource, void* user) {
  if (auto* self = static_cast<ProbeModule*>(user)) {
    self->record_resource_event(resource);
  }
}

void ProbeModule::record_draw_event(const DrawCallState& state) {
  if (capture_stopped_.load(std::memory_order_acquire)) {
    return;
  }
  std::lock_guard lk(trace_mu_);
  recorder_.record_draw(state);
}

void ProbeModule::record_resource_event(const ResourceSummary& resource) {
  if (capture_stopped_.load(std::memory_order_acquire)) {
    return;
  }
  std::lock_guard lk(trace_mu_);
  recorder_.record_resource(resource);
}

void ProbeModule::enqueue_lines(std::vector<std::string> lines, bool finalize_after) {
  {
    std::lock_guard lk(queue_mu_);
    for (auto& line : lines) {
      pending_lines_.push_back(std::move(line));
    }
    if (finalize_after) {
      writer_stop_ = true;
    }
  }
  queue_cv_.notify_one();
}

void ProbeModule::stop_writer(bool wait_without_join) {
  if (!writer_thread_.joinable()) {
    if (out_stream_.is_open()) {
      out_stream_.flush();
      out_stream_.close();
    }
    return;
  }

  {
    std::lock_guard lk(queue_mu_);
    writer_stop_ = true;
  }
  queue_cv_.notify_all();

  if (wait_without_join) {
    for (int i = 0; i < 250 && !writer_done_.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    writer_thread_.detach();
    return;
  }

  writer_thread_.join();
}

void ProbeModule::writer_loop() {
  std::deque<std::string> batch;
  for (;;) {
    {
      std::unique_lock lk(queue_mu_);
      queue_cv_.wait(lk, [&]() { return writer_stop_ || !pending_lines_.empty(); });
      if (pending_lines_.empty() && writer_stop_) {
        break;
      }
      batch.swap(pending_lines_);
    }

    for (const auto& line : batch) {
      out_stream_ << line << '\n';
    }
    out_stream_.flush();
    batch.clear();
  }

  if (out_stream_.is_open()) {
    out_stream_.flush();
    out_stream_.close();
  }
  writer_done_.store(true, std::memory_order_release);
}

} // namespace sf::probe
