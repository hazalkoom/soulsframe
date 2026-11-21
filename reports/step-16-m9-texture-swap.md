# Step 16 — M9 texture-pack swap & offline bake

## What was built
- **Pure core `src/tex/texture_pack.{h,cpp}`** (Linux-tested, guard-listed): DXGI format
  size helpers, `compute_texture_key()` (XXH64 of top mip seeded by width/height/format),
  hardened `parse_dds()` (DXT1/3/5, ATI1/2, RGBA32, DX10; rejects cubes, arrays, truncation,
  >16384 px, >15 mips), `parse_manifest()` (`<16hex> = rel/path.dds`), path-traversal guard,
  and `TextureSwapPolicy` VRAM budget.
- **D3D11 `TextureModule`** (`src/gfx/d3d11/texture_module.cpp`): MinHook detour on
  `ID3D11Device::CreateTexture2D` (slot 5). Only immutable/default, SRV-only, non-array,
  non-MSAA textures with initial data are hashed. Matches load the DDS and upload its full mip
  chain; sRGB twin formats are preserved. Every failure (missing file, bad DDS, budget,
  `CreateTexture2D` error) falls back to the original texture.
- **Config**: `[modules] textures = false`, `[textures] pack_dir`, `max_pack_mb` [16, 4096],
  `dump_keys`.
- **Offline tool `tools/py/bake_textures.py`**: `build <pack>` validates DDS files named by key
  and writes `manifest.txt`; `keys <log>` lists keys logged with `dump_keys = true`.
  (Python instead of the originally planned C# — no extra toolchain needed.)

## Why
Lets modders ship lower-VRAM (BC7/BC1) or higher-quality textures without touching game
archives; content keys make packs independent of file paths in the game data.

## Verification
- `check-guards.sh` pass; host tests 2/2 (7 new texture-pack test cases).
- MinGW win64 release build; imports checked.

## Follow-ups
- libFuzzer target for `parse_dds`/`parse_manifest`.
- Wine smoke scenario with a testbed texture + pack.
