#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path repo_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

} // namespace

TEST_CASE("build-pgo script exists and exposes all required stages") {
  const auto script = repo_root() / "scripts" / "build-pgo.sh";
  REQUIRE(std::filesystem::is_regular_file(script));

  std::ifstream input(script);
  REQUIRE(input.is_open());
  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

  CHECK(text.find("Stage 1/4") != std::string::npos);
  CHECK(text.find("Stage 2/4") != std::string::npos);
  CHECK(text.find("Stage 3/4") != std::string::npos);
  CHECK(text.find("Stage 4/4") != std::string::npos);
  CHECK(text.find("-fprofile-generate") != std::string::npos);
  CHECK(text.find("-fprofile-use") != std::string::npos);
  CHECK(text.find("--iterations") != std::string::npos);
  CHECK(text.find("STANDARD_JSON") != std::string::npos);
  CHECK(text.find("PGO_JSON") != std::string::npos);
}

TEST_CASE("build-pgo script rejects invalid fixed iteration counts") {
  const auto script = repo_root() / "scripts" / "build-pgo.sh";
  const std::string command = "bash \"" + script.string() + "\" 0 >/dev/null 2>&1";
  const int status = std::system(command.c_str());
  CHECK(status != 0);
}
