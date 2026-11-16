#include "probe/trace_types.h"

#include <algorithm>

namespace sf::probe {

std::string_view draw_api_name(DrawApi api) noexcept {
  switch (api) {
  case DrawApi::Draw:
    return "Draw";
  case DrawApi::DrawIndexed:
    return "DrawIndexed";
  case DrawApi::DrawInstanced:
    return "DrawInstanced";
  case DrawApi::DrawIndexedInstanced:
    return "DrawIndexedInstanced";
  case DrawApi::Dispatch:
    return "Dispatch";
  case DrawApi::ClearRtv:
    return "ClearRenderTargetView";
  case DrawApi::ClearDsv:
    return "ClearDepthStencilView";
  }
  return "Unknown";
}

std::string_view pass_tag_name(PassTag tag) noexcept {
  switch (tag) {
  case PassTag::Unknown:
    return "unknown";
  case PassTag::Shadow:
    return "shadow";
  case PassTag::GBuffer:
    return "gbuffer";
  case PassTag::Lighting:
    return "lighting";
  case PassTag::ParticlesAlpha:
    return "particles_alpha";
  case PassTag::PostProcess:
    return "postprocess";
  case PassTag::Ui:
    return "ui";
  case PassTag::Compute:
    return "compute";
  }
  return "unknown";
}

PassTag classify_pass(const PassSummary& pass) noexcept {
  if (pass.has_dispatch) {
    return PassTag::Compute;
  }
  if (pass.rtv_count == 0 && pass.rt_width > 0 && !pass.blend_enabled) {
    return PassTag::Shadow;
  }
  if (pass.rtv_count >= 2 && !pass.blend_enabled && pass.depth_write) {
    return PassTag::GBuffer;
  }
  if (pass.blend_enabled && !pass.depth_write) {
    return PassTag::ParticlesAlpha;
  }
  if (pass.blend_enabled && pass.depth_write) {
    return PassTag::Lighting;
  }
  if (pass.rtv_count == 1 && !pass.blend_enabled && !pass.depth_write && pass.draw_calls <= 16) {
    return PassTag::PostProcess;
  }
  return PassTag::Unknown;
}

size_t TraceRecorder::CallSiteKeyHash::operator()(const CallSiteKey& k) const noexcept {
  size_t h = std::hash<uint64_t>{}(k.caller_rva);
  h ^= std::hash<uint64_t>{}(k.ps_hash) + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U);
  const auto meta = static_cast<uint64_t>(k.api) | (k.blend_enabled ? (1ULL << 8U) : 0ULL) |
                    (k.depth_write ? (1ULL << 9U) : 0ULL);
  h ^= std::hash<uint64_t>{}(meta) + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U);
  return h;
}

bool TraceRecorder::is_draw_or_dispatch(DrawApi api) noexcept {
  return api != DrawApi::ClearRtv && api != DrawApi::ClearDsv;
}

TraceRecorder::TraceRecorder(Limits limits) : limits_(limits) {}

void TraceRecorder::begin_frame(uint64_t frame_idx) {
  current_frame_ = frame_idx;
  total_draw_calls_ = 0;
  total_elements_ = 0;
  call_sites_.clear();
  passes_.clear();
  pending_resources_.clear();
}

void TraceRecorder::record_draw(const DrawCallState& state) {
  const uint64_t instances = state.instance_count == 0 ? 1U : state.instance_count;
  const uint64_t elems = static_cast<uint64_t>(state.vertex_or_index_count) * instances;

  if (is_draw_or_dispatch(state.api)) {
    ++total_draw_calls_;
    total_elements_ += elems;

    const CallSiteKey key{
        .caller_rva = state.caller_rva,
        .ps_hash = state.ps_hash,
        .api = state.api,
        .blend_enabled = state.blend_enabled,
        .depth_write = state.depth_write,
    };
    if (auto it = call_sites_.find(key); it != call_sites_.end()) {
      ++it->second.draw_count;
      it->second.total_elements += elems;
    } else if (call_sites_.size() < limits_.max_call_sites) {
      call_sites_.emplace(key, CallSiteStats{
                                   .caller_rva = state.caller_rva,
                                   .ps_hash = state.ps_hash,
                                   .api = state.api,
                                   .blend_enabled = state.blend_enabled,
                                   .depth_write = state.depth_write,
                                   .draw_count = 1,
                                   .total_elements = elems,
                               });
    }
  }

  // A clear call or a switch in bound RTV/DSV signature begins a new GPU pass boundary.
  const bool force_boundary = !is_draw_or_dispatch(state.api);
  const bool need_new_pass =
      passes_.empty() || force_boundary || passes_.back().rtv_signature != state.rtv_signature ||
      passes_.back().rtv_count != state.rtv_count || passes_.back().rt_width != state.rt_width ||
      passes_.back().rt_height != state.rt_height;

  if (need_new_pass) {
    if (passes_.size() >= limits_.max_passes_per_frame) {
      return;
    }
    // Avoid creating redundant empty pass headers on consecutive ClearRtv+ClearDsv pairs.
    if (force_boundary && !passes_.empty() && passes_.back().draw_calls == 0 &&
        passes_.back().rtv_signature == state.rtv_signature) {
      return;
    }
    PassSummary next{};
    next.pass_id = static_cast<uint32_t>(passes_.size() + 1U);
    next.rtv_signature = state.rtv_signature;
    next.rt_width = state.rt_width;
    next.rt_height = state.rt_height;
    next.rtv_count = state.rtv_count;
    next.blend_enabled = state.blend_enabled;
    next.depth_write = state.depth_write;
    passes_.push_back(next);
  }

  if (is_draw_or_dispatch(state.api)) {
    auto& active = passes_.back();
    ++active.draw_calls;
    active.total_elements += elems;
    active.blend_enabled = active.blend_enabled || state.blend_enabled;
    active.depth_write = active.depth_write && state.depth_write;
    active.has_dispatch = active.has_dispatch || (state.api == DrawApi::Dispatch);
  }
}

bool TraceRecorder::record_resource(const ResourceSummary& res) {
  if (res.id_hash == 0 || seen_resources_.contains(res.id_hash)) {
    return false;
  }
  if (seen_resources_.size() >= limits_.max_tracked_resources) {
    return false;
  }
  seen_resources_.insert(res.id_hash);
  pending_resources_.push_back(res);
  return true;
}

FrameTraceSnapshot TraceRecorder::end_frame() {
  FrameTraceSnapshot snap{};
  snap.frame_index = current_frame_;
  snap.total_draw_calls = total_draw_calls_;
  snap.total_elements = total_elements_;
  snap.new_resources = std::move(pending_resources_);

  snap.passes.reserve(passes_.size());
  for (auto& pass : passes_) {
    if (pass.draw_calls == 0) {
      continue;
    }
    pass.tag = classify_pass(pass);
    snap.passes.push_back(pass);
  }

  snap.call_sites.reserve(call_sites_.size());
  for (const auto& [key, stats] : call_sites_) {
    (void)key;
    snap.call_sites.push_back(stats);
  }
  std::ranges::sort(snap.call_sites, [](const CallSiteStats& a, const CallSiteStats& b) {
    if (a.draw_count != b.draw_count) {
      return a.draw_count > b.draw_count;
    }
    if (a.total_elements != b.total_elements) {
      return a.total_elements > b.total_elements;
    }
    return a.caller_rva < b.caller_rva;
  });

  return snap;
}

void TraceRecorder::reset() {
  begin_frame(0);
  seen_resources_.clear();
}

} // namespace sf::probe
