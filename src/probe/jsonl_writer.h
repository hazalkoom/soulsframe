#pragma once

#include "probe/trace_types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sf::probe {

/// Session-level metadata written as line 1 (`type = "session_header"`) of JSONL captures.
struct SessionHeaderInfo {
  std::string mod_version = "0.2.0";
  std::string game = "ds3";
  std::string exe_sha256;
  std::string build_mode = "probe";
};

/// Escapes control characters, backslashes, and quotes for safe JSON string emission.
std::string json_escape(std::string_view text);

/// Serializes a `session_header` JSONL line (without trailing newline).
std::string format_session_header(const SessionHeaderInfo& info);

/// Serializes a per-frame summary (`type = "frame"`) JSONL line. A non-negative `gpu_frame_ms`
/// adds a trailing `"gpu_ms"` field (M3 GPU timestamp queries); negative omits it.
std::string format_frame_event(uint64_t frame_idx, double cpu_frame_ms, uint32_t draw_calls,
                               uint64_t total_elements, double gpu_frame_ms = -1.0);

/// Serializes one `gpu_pass` boundary summary JSONL line.
std::string format_pass_event(uint64_t frame_idx, const PassSummary& pass);

/// Serializes one aggregated `draw_site` JSONL line.
std::string format_draw_site_event(uint64_t frame_idx, const CallSiteStats& site);

/// Serializes one newly discovered GPU `resource` JSONL line.
std::string format_resource_event(uint64_t frame_idx, const ResourceSummary& res);

/// Converts an entire `FrameTraceSnapshot` into ordered JSONL lines (resources, frame,
/// passes, then call sites).
std::vector<std::string> serialize_frame_snapshot(const FrameTraceSnapshot& snap,
                                                  double cpu_frame_ms);

} // namespace sf::probe
