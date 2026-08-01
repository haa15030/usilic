#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "configuration.hpp"
#include "cp_mv_rm_mode.hpp"
#include "doctest.h"
#include "key_input.hpp"
#include "recording_display.hpp"

namespace fs = std::filesystem;

using fm::Configuration;
using fm::CpMvRmMode;
using fm::KeyInput;
using fm::WriteCheck;
using fm::test::RecordingDisplay;

namespace {

fs::path makeTempDir() {
  char tmpl[] = "/tmp/cpmvrm_testXXXXXX";
  char *result = mkdtemp(tmpl);
  REQUIRE(result != nullptr);
  return fs::path(result);
}

// KeyInput's raw-mode methods safely no-op when it was never open()ed
// against a real tty (see test_program_mode.cpp's own fixture, the
// established precedent for this), so these tests can run headlessly
// even though CpMvRmMode's *Interactive() methods call them.
struct Fixture {
  fs::path root;
  Configuration configuration;
  RecordingDisplay display{24, 80};
  KeyInput keyInput{display};
  CpMvRmMode mode{configuration, keyInput};

  Fixture() : root(makeTempDir()) {}
  ~Fixture() { fs::remove_all(root); }

  std::string writeFile(const std::string &name, const std::string &content) {
    std::string path = (root / name).string();
    std::ofstream(path) << content;
    return path;
  }
};

// isWritable()'s root bypass (ported from is_write()'s own
// `if (!getuid()) return 1;`) means every isWritable() call returns
// true unconditionally when the test process itself runs as root --
// which it genuinely does under this project's own Ubuntu Docker CI
// check (root is the default user in that image). That's correct,
// intentional behavior being exercised, not a bug to work around --
// these tests skip themselves in that case rather than asserting
// something false about a real, deliberate code path.
bool runningAsRoot() { return getuid() == 0; }

}  // namespace

TEST_CASE_FIXTURE(Fixture, "copyTo: copies a real file, leaving the original in place") {
  std::string src = writeFile("a.txt", "hello");
  fs::create_directory(root / "dest");
  std::string destDir = (root / "dest").string() + "/";

  auto result = mode.copyTo({src}, destDir);
  CHECK(result.ok);
  CHECK(fs::exists(src));
  CHECK(fs::exists(root / "dest" / "a.txt"));

  std::ifstream in(root / "dest" / "a.txt");
  std::string content;
  std::getline(in, content);
  CHECK(content == "hello");
}

TEST_CASE_FIXTURE(Fixture, "moveTo: moves a real file, removing the original") {
  std::string src = writeFile("b.txt", "world");
  fs::create_directory(root / "dest");
  std::string destDir = (root / "dest").string() + "/";

  auto result = mode.moveTo({src}, destDir);
  CHECK(result.ok);
  CHECK_FALSE(fs::exists(src));
  CHECK(fs::exists(root / "dest" / "b.txt"));
}

TEST_CASE_FIXTURE(Fixture, "remove: deletes real files") {
  // Configuration's default RM role includes -i (interactive
  // confirmation, matching config.h's compiled-in default): with no
  // real terminal attached, ChildProcess::run()'s piped stdin reads
  // EOF, which rm -i treats as "no" and skips the delete -- entirely
  // expected given -i's whole point, and exactly the interactive-
  // confirmation case CpMvRmMode's own class comment already says is
  // out of scope for run() (vs. runInteractive()). Use a
  // non-interactive RM override so this test exercises remove()'s
  // own mechanics instead.
  REQUIRE(configuration.loadRcText("RM\n rm -vr\n", true));

  std::string a = writeFile("c.txt", "x");
  std::string b = writeFile("d.txt", "y");

  auto result = mode.remove({a, b});
  CHECK(result.ok);
  CHECK_FALSE(fs::exists(a));
  CHECK_FALSE(fs::exists(b));
}

TEST_CASE_FIXTURE(Fixture, "copyTo/moveTo/remove: an empty source list is a safe no-op") {
  CHECK_FALSE(mode.copyTo({}, root.string() + "/").ok);
  CHECK_FALSE(mode.moveTo({}, root.string() + "/").ok);
  CHECK_FALSE(mode.remove({}).ok);
}

// *Interactive() variants (K-51/K-53/K-55, "with SHELL window"): same
// underlying cp/mv/rm invocation, just via ChildProcess::runInteractive()
// instead of run() -- these tests verify the real filesystem effect is
// identical, not the terminal-attachment mechanics themselves (already
// covered by ExecMode's own tests, which this shares its shape with).
TEST_CASE_FIXTURE(Fixture, "copyToInteractive: copies a real file via runInteractive(), same effect as copyTo") {
  std::string src = writeFile("ai.txt", "hello");
  fs::create_directory(root / "destI");
  std::string destDir = (root / "destI").string() + "/";

  CHECK(mode.copyToInteractive({src}, destDir) == 0);
  CHECK(fs::exists(src));
  CHECK(fs::exists(root / "destI" / "ai.txt"));
}

TEST_CASE_FIXTURE(Fixture, "moveToInteractive: moves a real file via runInteractive(), same effect as moveTo") {
  std::string src = writeFile("bi.txt", "world");
  fs::create_directory(root / "destI");
  std::string destDir = (root / "destI").string() + "/";

  CHECK(mode.moveToInteractive({src}, destDir) == 0);
  CHECK_FALSE(fs::exists(src));
  CHECK(fs::exists(root / "destI" / "bi.txt"));
}

TEST_CASE_FIXTURE(Fixture, "removeInteractive: deletes real files via runInteractive(), same effect as remove") {
  REQUIRE(configuration.loadRcText("RM\n rm -vr\n", true));  // avoid -i, see remove()'s own test comment

  std::string a = writeFile("ci.txt", "x");
  std::string b = writeFile("di.txt", "y");

  CHECK(mode.removeInteractive({a, b}) == 0);
  CHECK_FALSE(fs::exists(a));
  CHECK_FALSE(fs::exists(b));
}

TEST_CASE_FIXTURE(Fixture, "copyToInteractive/moveToInteractive/removeInteractive: empty source list is a safe no-op") {
  CHECK(mode.copyToInteractive({}, root.string() + "/") == -1);
  CHECK(mode.moveToInteractive({}, root.string() + "/") == -1);
  CHECK(mode.removeInteractive({}) == -1);
}

TEST_CASE_FIXTURE(Fixture, "isWritable: an existing file owned by the current user is writable") {
  std::string path = writeFile("owned.txt", "x");
  CHECK(CpMvRmMode::isWritable(path));
}

TEST_CASE_FIXTURE(Fixture, "isWritable: Plain rejects a nonexistent path") {
  bool result = CpMvRmMode::isWritable(root.string() + "/does-not-exist.txt", WriteCheck::Plain);
  CHECK(result == runningAsRoot());
}

TEST_CASE_FIXTURE(Fixture, "isWritable: MissingIsWritable accepts a nonexistent path") {
  CHECK(CpMvRmMode::isWritable(root.string() + "/does-not-exist.txt", WriteCheck::MissingIsWritable));
}

TEST_CASE_FIXTURE(Fixture, "isWritable: MustBeDirectory rejects a plain file") {
  std::string path = writeFile("notadir.txt", "x");
  bool result = CpMvRmMode::isWritable(path, WriteCheck::MustBeDirectory);
  CHECK(result == runningAsRoot());
}

TEST_CASE_FIXTURE(Fixture, "isWritable: MustBeDirectory accepts a real directory") {
  fs::create_directory(root / "realdir");
  CHECK(CpMvRmMode::isWritable((root / "realdir").string(), WriteCheck::MustBeDirectory));
}

TEST_CASE_FIXTURE(Fixture, "isWritable: MustBeDirectory rejects a nonexistent path") {
  bool result = CpMvRmMode::isWritable(root.string() + "/no-such-dir", WriteCheck::MustBeDirectory);
  CHECK(result == runningAsRoot());
}
