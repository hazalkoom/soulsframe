#pragma once

#include "core/config.h"
#include "core/module.h"

#include <memory>
#include <string>

namespace sf::gfx {

/// Milestone 9 texture-pack replacement module (`[modules] textures`, `[textures]`).
///
/// Detours `ID3D11Device::CreateTexture2D` (vtable slot 5). For eligible immutable/default 2D
/// textures with initial data it computes `sf::tex::compute_texture_key()` over the top mip and,
/// if the pack manifest lists that key, uploads the replacement DDS instead. Any failure falls
/// back to the game's original texture, so a broken pack can never crash or blank the game.
class TextureModule final : public sf::Module {
public:
  /// `base_dir` is the folder containing `dinput8.dll`; the pack lives in `base_dir/pack_dir`.
  explicit TextureModule(std::string base_dir);
  ~TextureModule() override;

  const char* name() const override { return "textures"; }
  bool is_enabled(const sf::Config& cfg) const override { return cfg.modules.textures; }
  bool init(const sf::GameProfile& profile, const sf::Config& cfg) override;
  void shutdown() override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace sf::gfx
