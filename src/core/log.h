#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace sf::log {

/// Runtime log severity threshold controlled by `general.log_level`.
enum class Level : uint8_t { Debug = 0, Info = 1, Warn = 2, Error = 3 };

/// Parses a `general.log_level` string (`"debug"`, `"info"`, `"warn"`, `"error"`), defaulting
/// to `Level::Info` on unrecognized inputs.
Level parse_level(std::string_view name) noexcept;

/// Sets the minimum severity threshold emitted by `SF_LOG*()` calls.
void set_level(Level min_level) noexcept;

/// Returns the active minimum severity threshold.
Level get_level() noexcept;

/// Opens (or rotates) `soulsframe.log` inside the directory containing `module_handle`.
void init(void* module_handle);

/// Flushes and closes the log file handle cleanly on DLL unload.
void shutdown();

/// Formats and appends a single newline-terminated, flushed `Info`-level message thread-safely.
void write(const char* fmt, ...);

/// Formats and appends a newline-terminated, flushed message when `lvl >= get_level()`.
void write_level(Level lvl, const char* fmt, ...);

/// Returns the parent directory path of `module_handle` without trailing separator.
std::string module_dir(void* module_handle);

} // namespace sf::log

#define SF_LOG(...) ::sf::log::write(__VA_ARGS__)
#define SF_LOG_DEBUG(...) ::sf::log::write_level(::sf::log::Level::Debug, __VA_ARGS__)
#define SF_LOG_WARN(...) ::sf::log::write_level(::sf::log::Level::Warn, __VA_ARGS__)
#define SF_LOG_ERROR(...) ::sf::log::write_level(::sf::log::Level::Error, __VA_ARGS__)
