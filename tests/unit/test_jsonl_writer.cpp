#include "probe/jsonl_writer.h"

#include <doctest/doctest.h>

TEST_CASE("json_escape escapes control chars, quotes, and backslashes") {
  using sf::probe::json_escape;
  CHECK(json_escape(R"(hello "world"\path)") == R"(hello \"world\"\\path)");
  CHECK(json_escape("a\nb\rc\td\be\ff\x01") == R"(a\nb\rc\td\be\ff\u0001)");
}

TEST_CASE("JSONL event formatters produce valid structured lines") {
  using namespace sf::probe;

  const auto hdr = format_session_header(SessionHeaderInfo{
      .mod_version = "0.2.0",
      .game = "ds3",
      .exe_sha256 = "deadbeef",
      .build_mode = "probe",
  });
  CHECK(hdr.find(R"("type":"session_header")") != std::string::npos);
  CHECK(hdr.find(R"("exe_sha256":"deadbeef")") != std::string::npos);

  TraceRecorder rec;
  rec.begin_frame(7);
  rec.record_resource(ResourceSummary{
      .kind = "Texture2D",
      .id_hash = 0xfeedfaceULL,
      .width = 1920,
      .height = 1080,
      .format_or_stride = 28,
      .bind_flags = 40,
      .mip_levels = 1,
      .sample_count = 4,
      .is_render_target = true,
  });
  rec.record_draw(DrawCallState{
      .api = DrawApi::DrawIndexed,
      .vertex_or_index_count = 150,
      .instance_count = 2,
      .caller_rva = 0x1a2b3c,
      .ps_hash = 0x8f31c09eULL,
      .rtv_signature = 0x11,
      .rt_width = 1920,
      .rt_height = 1080,
      .rtv_count = 1,
      .blend_enabled = true,
      .depth_write = false,
  });

  const auto snap = rec.end_frame();
  const auto lines = serialize_frame_snapshot(snap, 16.65);
  REQUIRE(lines.size() == 4);
  CHECK(lines[0].find(R"("type":"resource")") != std::string::npos);
  CHECK(lines[0].find(R"("id":"0xfeedface")") != std::string::npos);
  CHECK(lines[1].find(R"("type":"frame","idx":7)") != std::string::npos);
  CHECK(lines[2].find(R"("type":"gpu_pass")") != std::string::npos);
  CHECK(lines[2].find(R"("tag":"particles_alpha")") != std::string::npos);
  CHECK(lines[3].find(R"("type":"draw_site")") != std::string::npos);
  CHECK(lines[3].find(R"("rva":"0x1a2b3c")") != std::string::npos);
}
