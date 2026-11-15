#include "probe/jsonl_writer.h"

#include <array>
#include <cstdio>

namespace sf::probe {

namespace {

/// Formats a 64-bit unsigned integer as lowercase `0x...` hex text.
std::string format_hex64(uint64_t value) {
  std::array<char, 32> buf{};
  std::snprintf(buf.data(), buf.size(), "0x%llx", static_cast<unsigned long long>(value));
  return {buf.data()};
}

} // namespace

std::string json_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8U);
  for (const unsigned char ch : text) {
    switch (ch) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (ch < 0x20U) {
        std::array<char, 8> esc{};
        std::snprintf(esc.data(), esc.size(), "\\u%04x", static_cast<unsigned>(ch));
        out += esc.data();
      } else {
        out.push_back(static_cast<char>(ch));
      }
      break;
    }
  }
  return out;
}

std::string format_session_header(const SessionHeaderInfo& info) {
  std::string out;
  out.reserve(192);
  out += R"({"type":"session_header","schema":1,"mod_version":")";
  out += json_escape(info.mod_version);
  out += R"(","game":")";
  out += json_escape(info.game);
  out += R"(","exe_sha256":")";
  out += json_escape(info.exe_sha256);
  out += R"(","build_mode":")";
  out += json_escape(info.build_mode);
  out += R"("})";
  return out;
}

std::string format_frame_event(uint64_t frame_idx, double cpu_frame_ms, uint32_t draw_calls,
                               uint64_t total_elements) {
  std::array<char, 160> buf{};
  std::snprintf(
      buf.data(), buf.size(),
      R"({"type":"frame","idx":%llu,"cpu_frame_ms":%.4f,"draw_calls":%u,"elements":%llu})",
      static_cast<unsigned long long>(frame_idx), cpu_frame_ms, draw_calls,
      static_cast<unsigned long long>(total_elements));
  return {buf.data()};
}

std::string format_pass_event(uint64_t frame_idx, const PassSummary& pass) {
  const auto tag = pass_tag_name(pass.tag);
  std::array<char, 256> buf{};
  std::snprintf(
      buf.data(), buf.size(),
      R"({"type":"gpu_pass","frame_idx":%llu,"pass_id":%u,"tag":"%.*s","draws":%u,"elements":%llu,"rt_width":%u,"rt_height":%u,"rtv_count":%u,"blend":%s,"depth_write":%s})",
      static_cast<unsigned long long>(frame_idx), pass.pass_id, static_cast<int>(tag.size()),
      tag.data(), pass.draw_calls, static_cast<unsigned long long>(pass.total_elements),
      static_cast<unsigned>(pass.rt_width), static_cast<unsigned>(pass.rt_height),
      static_cast<unsigned>(pass.rtv_count), pass.blend_enabled ? "true" : "false",
      pass.depth_write ? "true" : "false");
  return {buf.data()};
}

std::string format_draw_site_event(uint64_t frame_idx, const CallSiteStats& site) {
  const auto api = draw_api_name(site.api);
  const auto rva = format_hex64(site.caller_rva);
  const auto ps = format_hex64(site.ps_hash);
  std::array<char, 256> buf{};
  std::snprintf(
      buf.data(), buf.size(),
      R"({"type":"draw_site","frame_idx":%llu,"rva":"%s","api":"%.*s","count":%u,"elements":%llu,"blend_enabled":%s,"depth_write":%s,"ps_hash":"%s"})",
      static_cast<unsigned long long>(frame_idx), rva.c_str(), static_cast<int>(api.size()),
      api.data(), site.draw_count, static_cast<unsigned long long>(site.total_elements),
      site.blend_enabled ? "true" : "false", site.depth_write ? "true" : "false", ps.c_str());
  return {buf.data()};
}

std::string format_resource_event(uint64_t frame_idx, const ResourceSummary& res) {
  const auto id_hex = format_hex64(res.id_hash);
  const auto kind = json_escape(res.kind);
  std::array<char, 256> buf{};
  std::snprintf(
      buf.data(), buf.size(),
      R"({"type":"resource","frame_idx":%llu,"id":"%s","kind":"%s","width":%u,"height":%u,"fmt":%u,"bind_flags":%u,"mips":%u,"samples":%u,"rt":%s})",
      static_cast<unsigned long long>(frame_idx), id_hex.c_str(), kind.c_str(), res.width,
      res.height, res.format_or_stride, res.bind_flags, res.mip_levels, res.sample_count,
      res.is_render_target ? "true" : "false");
  return {buf.data()};
}

std::vector<std::string> serialize_frame_snapshot(const FrameTraceSnapshot& snap,
                                                  double cpu_frame_ms) {
  std::vector<std::string> lines;
  lines.reserve(1U + snap.new_resources.size() + snap.passes.size() + snap.call_sites.size());

  for (const auto& res : snap.new_resources) {
    lines.push_back(format_resource_event(snap.frame_index, res));
  }
  lines.push_back(format_frame_event(snap.frame_index, cpu_frame_ms, snap.total_draw_calls,
                                     snap.total_elements));
  for (const auto& pass : snap.passes) {
    lines.push_back(format_pass_event(snap.frame_index, pass));
  }
  for (const auto& site : snap.call_sites) {
    lines.push_back(format_draw_site_event(snap.frame_index, site));
  }
  return lines;
}

} // namespace sf::probe
