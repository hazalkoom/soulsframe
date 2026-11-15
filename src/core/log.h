#pragma once

#include <string>

namespace sf::log {

/// Opens (or rotates) `soulsframe.log` inside the directory containing `module_handle`.
void init(void* module_handle);

/// Flushes and closes the log file handle cleanly on DLL unload.
void shutdown();

/// Formats and appends a single newline-terminated, flushed message thread-safely.
void write(const char* fmt, ...);

/// Returns the parent directory path of `module_handle` without trailing separator.
std::string module_dir(void* module_handle);

} // namespace sf::log

#define SF_LOG(...) ::sf::log::write(__VA_ARGS__)
