#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "doctest.h"
#include "path_resolver.hpp"

namespace fs = std::filesystem;

using fm::PathResolver;

namespace {

fs::path makeTempDir() {
  char tmpl[] = "/tmp/pathresolver_testXXXXXX";
  char *result = mkdtemp(tmpl);
  REQUIRE(result != nullptr);
  return fs::path(result);
}

struct Fixture {
  fs::path root;
  PathResolver resolver{root.string()};

  Fixture() : root(makeTempDir()) {
    fs::create_directory(root / "subdir");
    { std::ofstream(root / "file.txt") << "hi"; }
    resolver = PathResolver(root.string());
  }
  ~Fixture() { fs::remove_all(root); }
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "resolve: classifies a real directory, adding a trailing slash") {
  auto result = resolver.resolve((root / "subdir").string());
  CHECK(result.isDirectory);
  CHECK(result.exists);
  CHECK(result.path.back() == '/');
}

TEST_CASE_FIXTURE(Fixture, "resolve: classifies a real file, without adding a trailing slash") {
  auto result = resolver.resolve((root / "file.txt").string());
  CHECK_FALSE(result.isDirectory);
  CHECK(result.exists);
  CHECK(result.path.back() != '/');
}

TEST_CASE_FIXTURE(Fixture, "resolve: a nonexistent path is reported as not existing") {
  auto result = resolver.resolve((root / "nope.txt").string());
  CHECK_FALSE(result.exists);
  CHECK_FALSE(result.isDirectory);
}

TEST_CASE_FIXTURE(Fixture, "resolve: expands a bare '~' to the home directory") {
  auto result = resolver.resolve("~");
  bool matches = (result.path == root.string() + "/") || (result.path == root.string());
  CHECK(matches);
}

TEST_CASE_FIXTURE(Fixture, "resolve: expands '~/...' to home-relative") {
  auto result = resolver.resolve("~/subdir");
  CHECK(result.isDirectory);
  CHECK(result.path == (root / "subdir").string() + "/");
}

TEST_CASE_FIXTURE(Fixture, "resolve: collapses './' and '../' segments") {
  std::string messy = root.string() + "/subdir/../subdir/./";
  auto result = resolver.resolve(messy);
  CHECK(result.isDirectory);
  CHECK(result.path == (root / "subdir").string() + "/");
}

TEST_CASE_FIXTURE(Fixture, "resolve: trims trailing whitespace before resolving") {
  auto result = resolver.resolve((root / "file.txt").string() + "  \t");
  CHECK(result.exists);
  CHECK_FALSE(result.isDirectory);
}

TEST_CASE_FIXTURE(Fixture, "resolve: an empty path resolves to nothing, safely") {
  auto result = resolver.resolve("");
  CHECK_FALSE(result.exists);
  CHECK(result.path.empty());
}

TEST_CASE_FIXTURE(Fixture, "resolve: a relative path joins onto baseDirectory when given (2026-08-01 user report)") {
  auto result = resolver.resolve("file.txt", root.string());
  CHECK(result.exists);
  CHECK_FALSE(result.isDirectory);
  CHECK(result.path == (root / "file.txt").string());
}

TEST_CASE_FIXTURE(Fixture, "resolve: without baseDirectory, a relative path is NOT joined (original behavior preserved)") {
  auto result = resolver.resolve("file.txt");
  CHECK_FALSE(result.exists);  // "file.txt" alone doesn't exist relative to the test process's own cwd
}

TEST_CASE_FIXTURE(Fixture, "resolve: an absolute path ignores baseDirectory entirely") {
  auto result = resolver.resolve((root / "subdir").string(), "/some/unrelated/base");
  CHECK(result.isDirectory);
  CHECK(result.path == (root / "subdir").string() + "/");
}

TEST_CASE_FIXTURE(Fixture, "resolve: a ~-prefixed path ignores baseDirectory entirely") {
  auto result = resolver.resolve("~/subdir", "/some/unrelated/base");
  CHECK(result.isDirectory);
  CHECK(result.path == (root / "subdir").string() + "/");
}

TEST_CASE_FIXTURE(Fixture, "resolve: baseDirectory without a trailing slash is still joined correctly") {
  auto result = resolver.resolve("subdir", root.string());  // root has no trailing slash
  CHECK(result.isDirectory);
  CHECK(result.path == (root / "subdir").string() + "/");
}
