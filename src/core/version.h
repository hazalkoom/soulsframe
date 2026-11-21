#pragma once

#include <cstdint>

namespace sf {

/// Semantic major version component.
inline constexpr uint32_t kVersionMajor = 0U;

/// Semantic minor version component.
inline constexpr uint32_t kVersionMinor = 10U;

/// Semantic patch version component.
inline constexpr uint32_t kVersionPatch = 0U;

/// Human-readable SoulsFrame release version string (matches CMake project version).
inline constexpr const char* kVersionString = "0.10.0";

/// Product display name used in logs and CLI banners.
inline constexpr const char* kProductName = "SoulsFrame";

} // namespace sf
