#include "core/log.h"

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace sf::log {

namespace {
FILE* g_file = nullptr;
std::mutex g_mu;
std::atomic<uint8_t> g_min_level{static_cast<uint8_t>(Level::Info)};

const char* level_tag(Level lvl) noexcept {
  switch (lvl) {
  case Level::Debug:
    return "[DEBUG] ";
  case Level::Info:
    return "[INFO] ";
  case Level::Warn:
    return "[WARN] ";
  case Level::Error:
    return "[ERROR] ";
  }
  return "[INFO] ";
}

void vwrite_level(Level lvl, const char* fmt, va_list ap) {
  if (static_cast<uint8_t>(lvl) < g_min_level.load(std::memory_order_relaxed)) {
    return;
  }
  std::lock_guard lk(g_mu);
  if (!g_file) {
    return;
  }
  std::fputs(level_tag(lvl), g_file);
  std::vfprintf(g_file, fmt, ap);
  std::fputc('\n', g_file);
  std::fflush(g_file);
}

} // namespace

Level parse_level(std::string_view name) noexcept {
  if (name == "debug") {
    return Level::Debug;
  }
  if (name == "warn") {
    return Level::Warn;
  }
  if (name == "error") {
    return Level::Error;
  }
  return Level::Info;
}

void set_level(Level min_level) noexcept {
  g_min_level.store(static_cast<uint8_t>(min_level), std::memory_order_relaxed);
}

Level get_level() noexcept {
  return static_cast<Level>(g_min_level.load(std::memory_order_relaxed));
}

std::string module_dir(void* module_handle) {
  char path[MAX_PATH]{};
  const DWORD len = GetModuleFileNameA(static_cast<HMODULE>(module_handle), path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return ".";
  }
  char* slash = std::strrchr(path, '\\');
  if (!slash) {
    slash = std::strrchr(path, '/');
  }
  if (slash) {
    *slash = '\0';
  }
  return std::string(path);
}

void init(void* module_handle) {
  std::lock_guard lk(g_mu);
  if (g_file) {
    std::fclose(g_file);
    g_file = nullptr;
  }
  g_min_level.store(static_cast<uint8_t>(Level::Info), std::memory_order_relaxed);
  std::string log_path = module_dir(module_handle) + "\\soulsframe.log";
  g_file = std::fopen(log_path.c_str(), "w");
}

void shutdown() {
  std::lock_guard lk(g_mu);
  if (g_file) {
    std::fflush(g_file);
    std::fclose(g_file);
    g_file = nullptr;
  }
}

void write(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vwrite_level(Level::Info, fmt, ap);
  va_end(ap);
}

void write_level(Level lvl, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vwrite_level(lvl, fmt, ap);
  va_end(ap);
}

} // namespace sf::log
