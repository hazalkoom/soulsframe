#include "core/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace sf::log {

namespace {
FILE* g_file = nullptr;
std::mutex g_mu;
} // namespace

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
  std::lock_guard lk(g_mu);
  if (!g_file) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(g_file, fmt, ap);
  va_end(ap);
  std::fputc('\n', g_file);
  std::fflush(g_file);
}

} // namespace sf::log
