#include "probe/jsonl_writer.h"
#include "probe/trace_types.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

/// libFuzzer harness exercising TraceRecorder + JSONL serialization under arbitrary inputs.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view raw(reinterpret_cast<const char*>(data), size);
  const auto escaped = sf::probe::json_escape(raw);
  (void)escaped.size();

  sf::probe::TraceRecorder rec;
  rec.begin_frame(1);

  for (size_t i = 0; i + 4 <= size && i < 64; i += 4) {
    sf::probe::DrawState st{};
    st.api = static_cast<sf::probe::DrawApi>(data[i] % 7);
    st.vertex_or_index_count = static_cast<uint32_t>(data[i + 1]) * 3U;
    st.caller_rva = static_cast<uint64_t>(data[i + 2]) << 8U;
    st.rtv_signature = data[i + 3];
    st.rtv_count = static_cast<uint8_t>(data[i + 3] % 4U);
    st.rt_width = (st.rtv_count > 0) ? 1280 : 0;
    st.rt_height = (st.rtv_count > 0) ? 720 : 0;
    st.blend_enabled = (data[i] & 0x10U) != 0;
    st.depth_write = (data[i] & 0x20U) == 0;
    rec.record_draw(st);
  }

  const auto snap = rec.end_frame();
  const auto lines = sf::probe::serialize_frame_snapshot(snap, 16.6);
  (void)lines.size();
  return 0;
}
