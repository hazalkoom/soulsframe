#include "bench_harness.h"

#include "probe/jsonl_writer.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

struct CliOptions {
  bool quick = false;
  uint64_t fixed_iterations = 0;
  std::string filter;
  std::string json_path;
};

CliOptions parse_cli(int argc, char** argv) {
  CliOptions opts;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--quick") {
      opts.quick = true;
    } else if (arg == "--filter" && i + 1 < argc) {
      opts.filter = argv[++i];
    } else if (arg == "--json" && i + 1 < argc) {
      opts.json_path = argv[++i];
    } else if (arg == "--iterations" && i + 1 < argc) {
      const std::string_view value(argv[++i]);
      uint64_t parsed = 0;
      const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
      if (ec == std::errc{} && ptr == value.data() + value.size() && parsed > 0) {
        opts.fixed_iterations = parsed;
      }
    }
  }
  return opts;
}

sf::bench::BenchResult run_case(const sf::bench::BenchCase& bc, bool quick,
                                uint64_t fixed_iterations) {
  using Clock = std::chrono::steady_clock;

  const uint64_t iters = fixed_iterations > 0
                             ? fixed_iterations
                             : (quick ? std::max<uint64_t>(1, bc.iterations / 100) : bc.iterations);
  const size_t warmup_runs = quick ? 1U : 2U;
  const size_t measured_runs = quick ? 2U : 5U;

  for (size_t w = 0; w < warmup_runs; ++w) {
    bc.fn(iters);
  }

  std::vector<double> ns_samples;
  ns_samples.reserve(measured_runs);

  for (size_t r = 0; r < measured_runs; ++r) {
    const auto t0 = Clock::now();
    bc.fn(iters);
    const auto t1 = Clock::now();
    const double total_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    ns_samples.push_back(total_ns / static_cast<double>(iters));
  }

  std::sort(ns_samples.begin(), ns_samples.end());
  const double min_ns = ns_samples.front();
  const double median_ns = ns_samples[ns_samples.size() / 2U];
  const size_t p95_idx =
      static_cast<size_t>(std::ceil(0.95 * static_cast<double>(ns_samples.size() - 1U)));
  const double p95_ns = ns_samples[p95_idx];
  const double ops_per_sec = median_ns > 0.0 ? (1e9 / median_ns) : 0.0;
  const double mb_per_sec =
      (bc.bytes_per_iter > 0 && median_ns > 0.0)
          ? (static_cast<double>(bc.bytes_per_iter) * ops_per_sec) / (1024.0 * 1024.0)
          : 0.0;

  return sf::bench::BenchResult{
      .name = std::string(bc.name),
      .category = std::string(bc.category),
      .iterations = iters,
      .samples = measured_runs,
      .min_ns = min_ns,
      .median_ns = median_ns,
      .p95_ns = p95_ns,
      .ops_per_sec = ops_per_sec,
      .mb_per_sec = mb_per_sec,
  };
}

void print_table(const std::vector<sf::bench::BenchResult>& results) {
  std::cout << std::left << std::setw(36) << "Benchmark" << std::setw(12) << "Category"
            << std::right << std::setw(12) << "Median(ns)" << std::setw(12) << "Min(ns)"
            << std::setw(12) << "P95(ns)" << std::setw(15) << "Throughput" << '\n';
  std::cout << std::string(99, '-') << '\n';

  std::cout << std::fixed;
  for (const auto& r : results) {
    std::ostringstream tp;
    tp << std::fixed;
    if (r.mb_per_sec > 0.0) {
      tp << std::setprecision(1) << r.mb_per_sec << " MB/s";
    } else if (r.ops_per_sec >= 1e6) {
      tp << std::setprecision(2) << (r.ops_per_sec / 1e6) << " Mops/s";
    } else if (r.ops_per_sec >= 1e3) {
      tp << std::setprecision(1) << (r.ops_per_sec / 1e3) << " Kops/s";
    } else {
      tp << std::setprecision(1) << r.ops_per_sec << " ops/s";
    }

    std::cout << std::left << std::setw(36) << r.name << std::setw(12) << r.category << std::right
              << std::setw(12) << std::setprecision(2) << r.median_ns << std::setw(12)
              << std::setprecision(2) << r.min_ns << std::setw(12) << std::setprecision(2)
              << r.p95_ns << std::setw(15) << tp.str() << '\n';
  }
}

bool write_json(const std::string& path, const std::vector<sf::bench::BenchResult>& results) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out << "{\n  \"benchmarks\": [\n";
  out << std::fixed << std::setprecision(3);
  for (size_t i = 0; i < results.size(); ++i) {
    const auto& r = results[i];
    out << "    {\"name\": \"" << sf::probe::json_escape(r.name) << "\", "
        << "\"category\": \"" << sf::probe::json_escape(r.category) << "\", "
        << "\"iterations\": " << r.iterations << ", "
        << "\"samples\": " << r.samples << ", "
        << "\"min_ns\": " << r.min_ns << ", "
        << "\"median_ns\": " << r.median_ns << ", "
        << "\"p95_ns\": " << r.p95_ns << ", "
        << "\"ops_per_sec\": " << r.ops_per_sec << ", "
        << "\"mb_per_sec\": " << r.mb_per_sec << '}';
    if (i + 1 < results.size()) {
      out << ',';
    }
    out << '\n';
  }
  out << "  ]\n}\n";
  return static_cast<bool>(out);
}

} // namespace

int main(int argc, char** argv) {
  const CliOptions opts = parse_cli(argc, argv);

  std::vector<sf::bench::BenchResult> results;
  for (const auto& bc : sf::bench::registry()) {
    if (!opts.filter.empty() && bc.name.find(opts.filter) == std::string_view::npos &&
        bc.category.find(opts.filter) == std::string_view::npos) {
      continue;
    }
    results.push_back(run_case(bc, opts.quick, opts.fixed_iterations));
  }

  print_table(results);

  if (!opts.json_path.empty() && !write_json(opts.json_path, results)) {
    std::cerr << "Failed to write benchmark JSON to: " << opts.json_path << '\n';
    return 1;
  }
  return 0;
}
