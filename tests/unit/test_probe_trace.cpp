#include "probe/trace_types.h"

#include <doctest/doctest.h>

TEST_CASE("draw_api_name and pass_tag_name map all enumerators") {
  using namespace sf::probe;
  CHECK(draw_api_name(DrawApi::Draw) == "Draw");
  CHECK(draw_api_name(DrawApi::DrawIndexed) == "DrawIndexed");
  CHECK(draw_api_name(DrawApi::DrawInstanced) == "DrawInstanced");
  CHECK(draw_api_name(DrawApi::DrawIndexedInstanced) == "DrawIndexedInstanced");
  CHECK(draw_api_name(DrawApi::Dispatch) == "Dispatch");
  CHECK(draw_api_name(DrawApi::ClearRtv) == "ClearRenderTargetView");
  CHECK(draw_api_name(DrawApi::ClearDsv) == "ClearDepthStencilView");

  CHECK(pass_tag_name(PassTag::Unknown) == "unknown");
  CHECK(pass_tag_name(PassTag::Shadow) == "shadow");
  CHECK(pass_tag_name(PassTag::GBuffer) == "gbuffer");
  CHECK(pass_tag_name(PassTag::Lighting) == "lighting");
  CHECK(pass_tag_name(PassTag::ParticlesAlpha) == "particles_alpha");
  CHECK(pass_tag_name(PassTag::PostProcess) == "postprocess");
  CHECK(pass_tag_name(PassTag::Ui) == "ui");
  CHECK(pass_tag_name(PassTag::Compute) == "compute");
}

TEST_CASE("TraceRecorder aggregates call sites and detects GPU passes") {
  using namespace sf::probe;
  TraceRecorder rec;
  rec.begin_frame(42);

  // Pass 1: Shadow depth-only pass after DSV clear.
  rec.record_draw(DrawCallState{
      .api = DrawApi::ClearDsv,
      .rtv_signature = 0x1001,
      .rt_width = 2048,
      .rt_height = 2048,
      .rtv_count = 0,
  });
  rec.record_draw(DrawCallState{
      .api = DrawApi::DrawIndexed,
      .vertex_or_index_count = 3000,
      .instance_count = 2,
      .caller_rva = 0x12000,
      .ps_hash = 0x111,
      .rtv_signature = 0x1001,
      .rt_width = 2048,
      .rt_height = 2048,
      .rtv_count = 0,
      .blend_enabled = false,
      .depth_write = true,
  });

  // Pass 2: GBuffer (3 RTVs, opaque, depth write).
  rec.record_draw(DrawCallState{
      .api = DrawApi::DrawIndexed,
      .vertex_or_index_count = 1200,
      .caller_rva = 0x22000,
      .ps_hash = 0x222,
      .rtv_signature = 0x2002,
      .rt_width = 1920,
      .rt_height = 1080,
      .rtv_count = 3,
      .blend_enabled = false,
      .depth_write = true,
  });

  // Pass 3: Particles alpha pass (blended, depth-write off) from repeated call site.
  for (int i = 0; i < 4; ++i) {
    rec.record_draw(DrawCallState{
        .api = DrawApi::DrawIndexedInstanced,
        .vertex_or_index_count = 6,
        .instance_count = 50,
        .caller_rva = 0x33000,
        .ps_hash = 0x333,
        .rtv_signature = 0x3003,
        .rt_width = 1920,
        .rt_height = 1080,
        .rtv_count = 1,
        .blend_enabled = true,
        .depth_write = false,
    });
  }

  // Pass 4: Compute dispatch.
  rec.record_draw(DrawCallState{
      .api = DrawApi::Dispatch,
      .vertex_or_index_count = 120,
      .instance_count = 1,
      .caller_rva = 0x44000,
      .rtv_signature = 0x4004,
      .rtv_count = 0,
      .blend_enabled = false,
      .depth_write = false,
  });

  // Pass 5: PostProcess single full-screen draw.
  rec.record_draw(DrawCallState{
      .api = DrawApi::Draw,
      .vertex_or_index_count = 3,
      .caller_rva = 0x55000,
      .rtv_signature = 0x5005,
      .rt_width = 1920,
      .rt_height = 1080,
      .rtv_count = 1,
      .blend_enabled = false,
      .depth_write = false,
  });

  const auto snap = rec.end_frame();
  CHECK(snap.frame_index == 42);
  CHECK(snap.total_draw_calls == 8);
  CHECK(snap.total_elements == 6000ULL + 1200ULL + 1200ULL + 120ULL + 3ULL);
  REQUIRE(snap.passes.size() == 5);
  CHECK(snap.passes[0].tag == PassTag::Shadow);
  CHECK(snap.passes[1].tag == PassTag::GBuffer);
  CHECK(snap.passes[2].tag == PassTag::ParticlesAlpha);
  CHECK(snap.passes[2].draw_calls == 4);
  CHECK(snap.passes[3].tag == PassTag::Compute);
  CHECK(snap.passes[4].tag == PassTag::PostProcess);

  // Sorted descending by draw_count, so the 4x particle call site comes first.
  REQUIRE(snap.call_sites.size() == 5);
  CHECK(snap.call_sites[0].caller_rva == 0x33000);
  CHECK(snap.call_sites[0].draw_count == 4);
  CHECK(snap.call_sites[0].total_elements == 1200);
}

TEST_CASE("TraceRecorder enforces caps and deduplicates session resources") {
  using namespace sf::probe;
  TraceRecorder rec(TraceRecorder::Limits{
      .max_call_sites = 2,
      .max_passes_per_frame = 2,
      .max_tracked_resources = 2,
  });

  ResourceSummary rt1{
      .kind = "Texture2D",
      .id_hash = 0xABC1,
      .width = 1920,
      .height = 1080,
      .is_render_target = true,
  };
  ResourceSummary rt2{
      .kind = "Texture2D",
      .id_hash = 0xABC2,
      .width = 1024,
      .height = 1024,
      .is_render_target = true,
  };
  ResourceSummary rt3{
      .kind = "Texture2D",
      .id_hash = 0xABC3,
      .width = 512,
      .height = 512,
      .is_render_target = false,
  };

  rec.begin_frame(1);
  CHECK(rec.record_resource(rt1) == true);
  CHECK(rec.record_resource(rt1) == false); // already seen
  CHECK(rec.record_resource(ResourceSummary{.id_hash = 0}) == false);
  CHECK(rec.record_resource(rt2) == true);
  CHECK(rec.record_resource(rt3) == false); // cap reached

  for (uint64_t i = 0; i < 4; ++i) {
    rec.record_draw(DrawCallState{
        .api = DrawApi::Draw,
        .vertex_or_index_count = 3,
        .caller_rva = 0x100 + i,
        .rtv_signature = 0x900 + i,
        .rt_width = 320,
        .rt_height = 240,
        .rtv_count = 1,
        .blend_enabled = true,
        .depth_write = true,
    });
  }

  const auto snap1 = rec.end_frame();
  CHECK(snap1.new_resources.size() == 2);
  CHECK(snap1.call_sites.size() == 2);
  CHECK(snap1.passes.size() == 2);
  CHECK(snap1.passes[0].tag == PassTag::Lighting);

  // Next frame sees no duplicate resources until reset().
  rec.begin_frame(2);
  CHECK(rec.record_resource(rt1) == false);
  CHECK(rec.end_frame().new_resources.empty());

  rec.reset();
  rec.begin_frame(3);
  CHECK(rec.record_resource(rt1) == true);
}
