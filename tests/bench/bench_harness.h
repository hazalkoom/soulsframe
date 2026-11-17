#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sf::bench {

/// Prevents the optimizing compiler from dead-code eliminating a computed value.
template <typename T> inline void do_not_optimize(const T& value) {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : "r,m"(value) : "memory");
#else
  volatile const T* sink = &value;
  (void)sink;
#endif
}

/// Inserts a full compiler memory barrier without runtime instructions.
inline void clobber_memory() {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : : "memory");
#endif
}

/// Definition of a single micro-benchmark case executed inside `soulsframe_benchmarks`.
struct BenchCase {
  std::string_view name;
  std::string_view category;
  uint64_t iterations = 1;
  uint64_t bytes_per_iter = 0;
  std::function<void(uint64_t)> fn;
};

/// Measured statistical summary of one micro-benchmark execution.
struct BenchResult {
  std::string name;
  std::string category;
  uint64_t iterations = 0;
  uint64_t samples = 0;
  double min_ns = 0.0;
  double median_ns = 0.0;
  double p95_ns = 0.0;
  double ops_per_sec = 0.0;
  double mb_per_sec = 0.0;
};

/// Returns the global mutable benchmark registry (populated via `BenchRegistrar`).
inline std::vector<BenchCase>& registry() {
  static std::vector<BenchCase> cases;
  return cases;
}

/// RAII static registration helper invoked before `main()`.
struct BenchRegistrar {
  BenchRegistrar(std::string_view name, std::string_view category, uint64_t iterations,
                 uint64_t bytes_per_iter, std::function<void(uint64_t)> fn) {
    registry().push_back(BenchCase{
        .name = name,
        .category = category,
        .iterations = iterations,
        .bytes_per_iter = bytes_per_iter,
        .fn = std::move(fn),
    });
  }
};

} // namespace sf::bench

#define SF_BENCH_CONCAT_IMPL(a, b) a##b
#define SF_BENCH_CONCAT(a, b) SF_BENCH_CONCAT_IMPL(a, b)

/// Registers a micro-benchmark function `void fn(uint64_t iters)` with the harness.
#define SF_REGISTER_BENCH(name, category, iterations, bytes_per_iter, fn)                          \
  static const ::sf::bench::BenchRegistrar SF_BENCH_CONCAT(sf_bench_reg_, __LINE__)(               \
      (name), (category), (iterations), (bytes_per_iter), (fn))
