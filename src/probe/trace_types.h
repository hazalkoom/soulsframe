#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sf::probe {

/// D3D11 submission or clear primitive recorded by the M2 draw-call tracer.
enum class DrawApi : uint8_t {
  Draw,
  DrawIndexed,
  DrawInstanced,
  DrawIndexedInstanced,
  Dispatch,
  ClearRtv,
  ClearDsv,
};

/// Returns the canonical JSONL string identifier for a `DrawApi` enumerator.
std::string_view draw_api_name(DrawApi api) noexcept;

/// Heuristic GPU pass category inferred from active render targets and pipeline state.
enum class PassTag : uint8_t {
  Unknown,
  Shadow,
  GBuffer,
  Lighting,
  ParticlesAlpha,
  PostProcess,
  Ui,
  Compute,
};

/// Returns the canonical lowercase string identifier for a `PassTag`.
std::string_view pass_tag_name(PassTag tag) noexcept;

/// Pure-C++ snapshot of pipeline state at a single draw, clear, or dispatch call.
/// Deliberately holds no COM or Win32 types so trace aggregation stays Linux-testable.
struct DrawCallState {
  DrawApi api = DrawApi::DrawIndexed;
  uint32_t vertex_or_index_count = 0;
  uint32_t instance_count = 1;
  uint64_t caller_rva = 0;
  uint64_t ps_hash = 0;
  uint64_t vs_hash = 0;
  uint64_t rtv_signature = 0; ///< Digest of bound RTVs + DSV identifying pass boundaries.
  uint16_t rt_width = 0;
  uint16_t rt_height = 0;
  uint8_t rtv_count = 0;
  bool blend_enabled = false;
  bool depth_write = true;
};

/// Composite hash key identifying a distinct engine call site + pipeline configuration.
struct CallSiteKey {
  uint64_t caller_rva = 0;
  uint64_t ps_hash = 0;
  DrawApi api = DrawApi::DrawIndexed;
  bool blend_enabled = false;
  bool depth_write = true;

  bool operator==(const CallSiteKey&) const = default;
};

/// Per-frame aggregated draw statistics for one `CallSiteKey`.
struct CallSiteStats {
  uint64_t caller_rva = 0;
  uint64_t ps_hash = 0;
  DrawApi api = DrawApi::DrawIndexed;
  bool blend_enabled = false;
  bool depth_write = true;
  uint32_t draw_count = 0;
  uint64_t total_elements = 0;
};

/// Contiguous sequence of draws sharing an active render-target configuration.
struct PassSummary {
  uint32_t pass_id = 0;
  PassTag tag = PassTag::Unknown;
  uint32_t draw_calls = 0;
  uint64_t total_elements = 0;
  uint64_t rtv_signature = 0;
  uint16_t rt_width = 0;
  uint16_t rt_height = 0;
  uint8_t rtv_count = 0;
  bool blend_enabled = false;
  bool depth_write = true;
  bool has_dispatch = false;
  double gpu_ms = -1.0; ///< Measured GPU time for this pass; negative means "not measured".
};

/// Metadata describing an observed GPU resource (render target, texture, or buffer).
struct ResourceSummary {
  std::string kind = "Texture2D";
  uint64_t id_hash = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t format_or_stride = 0;
  uint32_t bind_flags = 0;
  uint32_t mip_levels = 1;
  uint32_t sample_count = 1;
  bool is_render_target = false;
};

/// Self-contained per-frame output produced by `TraceRecorder::end_frame()`.
struct FrameTraceSnapshot {
  uint64_t frame_index = 0;
  uint32_t total_draw_calls = 0;
  uint64_t total_elements = 0;
  std::vector<PassSummary> passes;
  std::vector<CallSiteStats> call_sites;
  std::vector<ResourceSummary> new_resources;
  double gpu_frame_ms = -1.0; ///< Measured GPU time for the whole frame; negative = unmeasured.
};

/// Classifies a completed pass using render-target count, blend, depth-write, and dispatch cues.
PassTag classify_pass(const PassSummary& pass) noexcept;

/// Aggregates draw call sites, detects render-target pass boundaries, and deduplicates
/// discovered GPU resources across frames with deterministic memory bounds.
class TraceRecorder {
public:
  /// Upper bounds preventing runaway heap usage on complex frames.
  struct Limits {
    size_t max_call_sites = 512;
    size_t max_passes_per_frame = 256;
    size_t max_tracked_resources = 2048;
  };

  TraceRecorder() = default;
  explicit TraceRecorder(Limits limits);

  /// Clears previous-frame transient vectors and prepares recording for `frame_idx`.
  void begin_frame(uint64_t frame_idx);

  /// Records a single draw, clear, or dispatch event into the active frame.
  void record_draw(const DrawCallState& state);

  /// Registers a resource once per session; returns true when newly seen.
  bool record_resource(const ResourceSummary& res);

  /// Finalizes pass classification and sorted call-site summaries for the current frame.
  FrameTraceSnapshot end_frame();

  /// Resets both per-frame state and the session-wide seen-resource registry.
  void reset();

private:
  struct CallSiteKeyHash {
    size_t operator()(const CallSiteKey& k) const noexcept;
  };

  static bool is_draw_or_dispatch(DrawApi api) noexcept;

  Limits limits_{};
  uint64_t current_frame_ = 0;
  uint32_t total_draw_calls_ = 0;
  uint64_t total_elements_ = 0;

  std::unordered_map<CallSiteKey, CallSiteStats, CallSiteKeyHash> call_sites_;
  std::vector<PassSummary> passes_;
  std::unordered_set<uint64_t> seen_resources_;
  std::vector<ResourceSummary> pending_resources_;
};

} // namespace sf::probe
