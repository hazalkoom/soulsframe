#include "gfx/d3d11/texture_module.h"

#include "core/log.h"
#include "tex/texture_pack.h"

#include <MinHook.h>
#include <d3d11.h>
#include <windows.h>

#include <atomic>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <mutex>
#include <span>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sf::gfx {

namespace {

constexpr size_t kSlotCreateTexture2D = 5;
constexpr size_t kMaxDumpedKeys = 4096;
constexpr uint64_t kMaxDdsFileBytes = 256ULL * 1024ULL * 1024ULL;

using CreateTexture2DFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*,
                                                      const D3D11_SUBRESOURCE_DATA*,
                                                      ID3D11Texture2D**);

CreateTexture2DFn g_orig_create = nullptr;
void* g_target = nullptr;
bool g_owns_mh_init = false;

std::atomic<bool> g_active{false};
std::mutex g_mu; // Guards everything below; CreateTexture2D may be called from loader threads.
std::string g_pack_root;
bool g_dump_keys = false;
sf::tex::Manifest g_manifest;
sf::tex::TextureSwapPolicy g_policy;
std::unordered_set<uint64_t> g_dumped;

/// Reads a whole file, refusing anything larger than `kMaxDdsFileBytes`.
bool read_file(const std::string& path, std::vector<std::byte>& out) {
  std::ifstream ifs(path, std::ios::binary | std::ios::ate);
  if (!ifs.is_open()) {
    return false;
  }
  const std::streamoff size = ifs.tellg();
  if (size <= 0 || static_cast<uint64_t>(size) > kMaxDdsFileBytes) {
    return false;
  }
  out.resize(static_cast<size_t>(size));
  ifs.seekg(0);
  return static_cast<bool>(ifs.read(reinterpret_cast<char*>(out.data()), size));
}

/// Eligibility filter. Why so strict: render targets, staging, dynamic and array textures are
/// created per-frame or written by the GPU, so hashing them wastes CPU and swapping is unsafe.
bool eligible(const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* init) {
  if (desc == nullptr || init == nullptr || init->pSysMem == nullptr) {
    return false;
  }
  if (desc->ArraySize != 1U || desc->SampleDesc.Count != 1U || desc->MiscFlags != 0U) {
    return false;
  }
  if (desc->Usage != D3D11_USAGE_IMMUTABLE && desc->Usage != D3D11_USAGE_DEFAULT) {
    return false;
  }
  if ((desc->BindFlags & ~static_cast<UINT>(D3D11_BIND_SHADER_RESOURCE)) != 0U ||
      desc->CPUAccessFlags != 0U) {
    return false;
  }
  return sf::tex::is_supported_format(static_cast<uint32_t>(desc->Format));
}

HRESULT STDMETHODCALLTYPE hooked_create_texture2d(ID3D11Device* dev,
                                                  const D3D11_TEXTURE2D_DESC* desc,
                                                  const D3D11_SUBRESOURCE_DATA* init,
                                                  ID3D11Texture2D** out) {
  if (!g_active.load(std::memory_order_acquire) || !eligible(desc, init)) {
    return g_orig_create(dev, desc, init, out);
  }

  const auto fmt = static_cast<uint32_t>(desc->Format);
  const uint64_t top_bytes =
      static_cast<uint64_t>(init->SysMemPitch) * sf::tex::mip_row_count(fmt, desc->Height);
  if (init->SysMemPitch < sf::tex::mip_row_pitch(fmt, desc->Width) || top_bytes == 0U) {
    return g_orig_create(dev, desc, init, out);
  }
  const sf::tex::TextureDesc td{.width = desc->Width,
                                .height = desc->Height,
                                .mip_levels = desc->MipLevels,
                                .array_size = desc->ArraySize,
                                .format = fmt};
  const uint64_t key = sf::tex::compute_texture_key(
      td, {static_cast<const std::byte*>(init->pSysMem), static_cast<size_t>(top_bytes)});

  std::string rel;
  {
    std::lock_guard lk(g_mu);
    g_policy.note_observed();
    if (g_dump_keys && g_dumped.size() < kMaxDumpedKeys && g_dumped.insert(key).second) {
      SF_LOG("TextureModule key %s %ux%u fmt=%u mips=%u", sf::tex::key_hex(key).c_str(),
             desc->Width, desc->Height, fmt, desc->MipLevels);
    }
    const auto it = g_manifest.entries.find(key);
    if (it == g_manifest.entries.end()) {
      return g_orig_create(dev, desc, init, out);
    }
    rel = it->second;
  }

  std::vector<std::byte> file;
  const auto dds =
      read_file(g_pack_root + "\\" + rel, file) ? sf::tex::parse_dds(file) : sf::tex::DdsInfo{};
  if (!dds.ok) {
    std::lock_guard lk(g_mu);
    g_policy.note_invalid();
    SF_LOG("TextureModule: %s rejected (%s)", rel.c_str(),
           dds.error.empty() ? "unreadable" : dds.error.c_str());
    return g_orig_create(dev, desc, init, out);
  }
  {
    std::lock_guard lk(g_mu);
    if (!g_policy.try_reserve(dds.data_bytes)) {
      return g_orig_create(dev, desc, init, out);
    }
  }

  D3D11_TEXTURE2D_DESC nd = *desc;
  nd.Width = dds.width;
  nd.Height = dds.height;
  nd.MipLevels = dds.mip_levels;
  nd.Format = static_cast<DXGI_FORMAT>(dds.format);
  // Legacy DDS headers cannot express sRGB, so keep the game's sRGB twin when the block layout
  // matches; otherwise colours would shift because the shader samples with different decoding.
  const bool srgb_twin = (fmt == static_cast<uint32_t>(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) &&
                          dds.format == static_cast<uint32_t>(DXGI_FORMAT_B8G8R8A8_UNORM)) ||
                         ((fmt == static_cast<uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) ||
                           fmt == static_cast<uint32_t>(DXGI_FORMAT_BC1_UNORM_SRGB) ||
                           fmt == static_cast<uint32_t>(DXGI_FORMAT_BC2_UNORM_SRGB) ||
                           fmt == static_cast<uint32_t>(DXGI_FORMAT_BC3_UNORM_SRGB) ||
                           fmt == static_cast<uint32_t>(DXGI_FORMAT_BC7_UNORM_SRGB)) &&
                          dds.format + 1U == fmt);
  if (srgb_twin) {
    nd.Format = desc->Format;
  }

  std::vector<D3D11_SUBRESOURCE_DATA> subs(dds.mip_levels);
  size_t off = dds.data_offset;
  uint32_t w = dds.width;
  uint32_t h = dds.height;
  for (auto& s : subs) {
    const uint64_t pitch = sf::tex::mip_row_pitch(dds.format, w);
    s.pSysMem = file.data() + off;
    s.SysMemPitch = static_cast<UINT>(pitch);
    s.SysMemSlicePitch = 0;
    off += static_cast<size_t>(pitch * sf::tex::mip_row_count(dds.format, h));
    w = w > 1U ? w / 2U : 1U;
    h = h > 1U ? h / 2U : 1U;
  }

  const HRESULT hr = g_orig_create(dev, &nd, subs.data(), out);
  if (SUCCEEDED(hr)) {
    return hr;
  }
  SF_LOG("TextureModule: replacement %s failed (hr=0x%08lx); using original", rel.c_str(),
         static_cast<unsigned long>(hr));
  return g_orig_create(dev, desc, init, out);
}

bool install_hook() {
  ID3D11Device* dev = nullptr;
  D3D_FEATURE_LEVEL fl{};
  const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                       D3D11_SDK_VERSION, &dev, &fl, nullptr);
  if (FAILED(hr) || dev == nullptr) {
    SF_LOG("TextureModule: dummy D3D11CreateDevice failed (hr=0x%08lx)",
           static_cast<unsigned long>(hr));
    return false;
  }
  g_target = (*reinterpret_cast<void***>(dev))[kSlotCreateTexture2D];
  dev->Release();

  const MH_STATUS mh = MH_Initialize();
  if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED) {
    return false;
  }
  g_owns_mh_init = (mh == MH_OK);
  if (MH_CreateHook(g_target, reinterpret_cast<void*>(&hooked_create_texture2d),
                    reinterpret_cast<void**>(&g_orig_create)) != MH_OK ||
      MH_EnableHook(g_target) != MH_OK) {
    SF_LOG("TextureModule: failed to hook CreateTexture2D");
    MH_RemoveHook(g_target);
    if (g_owns_mh_init) {
      MH_Uninitialize();
      g_owns_mh_init = false;
    }
    g_target = nullptr;
    return false;
  }
  return true;
}

} // namespace

struct TextureModule::Impl {
  std::string base_dir;
  bool active = false;
};

TextureModule::TextureModule(std::string base_dir) : impl_(std::make_unique<Impl>()) {
  impl_->base_dir = std::move(base_dir);
}

TextureModule::~TextureModule() = default;

bool TextureModule::init(const sf::GameProfile& profile, const sf::Config& cfg) {
  (void)profile;
  const std::string root = impl_->base_dir + "\\" + cfg.textures.pack_dir;
  std::ifstream ifs(root + "\\manifest.txt");
  std::string text;
  if (ifs.is_open()) {
    std::ostringstream ss;
    ss << ifs.rdbuf();
    text = ss.str();
  } else if (!cfg.textures.dump_keys) {
    SF_LOG("TextureModule: no manifest at %s\\manifest.txt and dump_keys=false", root.c_str());
    return false;
  }

  {
    std::lock_guard lk(g_mu);
    g_pack_root = root;
    g_dump_keys = cfg.textures.dump_keys;
    g_manifest = sf::tex::parse_manifest(text);
    g_policy = sf::tex::TextureSwapPolicy(static_cast<uint64_t>(cfg.textures.max_pack_mb) << 20U);
    g_dumped.clear();
    for (const auto& w : g_manifest.warnings) {
      SF_LOG("TextureModule: %s", w.c_str());
    }
  }

  if (!install_hook()) {
    return false;
  }
  g_active.store(true, std::memory_order_release);
  impl_->active = true;
  SF_LOG("TextureModule active (entries=%zu, budget=%u MiB, dump_keys=%s)",
         g_manifest.entries.size(), cfg.textures.max_pack_mb,
         cfg.textures.dump_keys ? "true" : "false");
  return true;
}

void TextureModule::shutdown() {
  if (!impl_->active) {
    return;
  }
  impl_->active = false;
  g_active.store(false, std::memory_order_release);
  if (g_target != nullptr) {
    MH_DisableHook(g_target);
    MH_RemoveHook(g_target);
    g_target = nullptr;
  }
  if (g_owns_mh_init) {
    MH_Uninitialize();
    g_owns_mh_init = false;
  }
  std::lock_guard lk(g_mu);
  const auto& st = g_policy.stats();
  SF_LOG("TextureModule shutdown (observed=%llu, replaced=%llu, over_budget=%llu, invalid=%llu, "
         "bytes=%llu)",
         static_cast<unsigned long long>(st.observed), static_cast<unsigned long long>(st.replaced),
         static_cast<unsigned long long>(st.over_budget),
         static_cast<unsigned long long>(st.invalid),
         static_cast<unsigned long long>(st.bytes_loaded));
}

} // namespace sf::gfx
