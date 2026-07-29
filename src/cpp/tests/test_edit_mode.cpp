#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "configuration.hpp"
#include "doctest.h"
#include "edit_mode.hpp"
#include "key_input.hpp"
#include "recording_display.hpp"

namespace fs = std::filesystem;

using fm::Configuration;
using fm::EditMode;
using fm::KeyInput;
using fm::test::RecordingDisplay;

namespace {

fs::path makeTempFile() {
  char tmpl[] = "/tmp/editmode_testXXXXXX";
  int fd = mkstemp(tmpl);
  REQUIRE(fd >= 0);
  close(fd);
  return fs::path(tmpl);
}

struct Fixture {
  RecordingDisplay display{24, 80};
  KeyInput keyInput{display};
  Configuration configuration;
  EditMode mode{keyInput, configuration};
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "edit: an EDITOR that doesn't touch the file reports unchanged") {
  fs::path path = makeTempFile();
  { std::ofstream(path) << "original"; }
  REQUIRE(configuration.loadRcText("EDITOR\n true\n", true));  // `true`: exits 0, touches nothing

  bool changed = mode.edit(path.string(), 1);
  CHECK_FALSE(changed);

  std::ifstream in(path);
  std::string content;
  std::getline(in, content);
  CHECK(content == "original");
  fs::remove(path);
}

TEST_CASE_FIXTURE(Fixture, "edit: an EDITOR that rewrites the file reports changed") {
  fs::path path = makeTempFile();
  { std::ofstream(path) << "original"; }
  // A real editor stand-in: editorArgv() appends "+<lineNumber>" then
  // `path` after this configured argv, so inside `sh -c '...'`, $0 is
  // "+1" and $1 is the real target path -- reference $1 to rewrite it.
  // The `sleep 1` is deliberate, not incidental: some filesystems
  // (observed on this project's own Ubuntu Docker CI, likely an
  // overlayfs mount) report last_write_time() at only whole-second
  // resolution, and the fork/exec/echo sequence otherwise completes
  // fast enough that the "before" and "after" reads can land in the
  // same reported second despite the file genuinely having changed --
  // exactly the coarse-stat-resolution blind spot EditMode's own
  // class comment already documents switching away from raw stat()
  // to avoid; the 1-second floor here guarantees this specific test
  // clears it regardless of the underlying filesystem's granularity.
  std::string rc = "EDITOR\n sh -c 'sleep 1; echo edited > $1'\n";
  REQUIRE(configuration.loadRcText(rc, true));

  bool changed = mode.edit(path.string(), 1);
  CHECK(changed);

  std::ifstream in(path);
  std::string content;
  std::getline(in, content);
  CHECK(content == "edited");
  fs::remove(path);
}

TEST_CASE_FIXTURE(Fixture, "edit: a nonexistent file fails cleanly") {
  CHECK_FALSE(mode.edit("/tmp/editmode_does_not_exist_xyz.txt", 1));
}

TEST_CASE_FIXTURE(Fixture, "edit: no EDITOR configured behaves as a no-op (reports unchanged)") {
  // With no EDITOR directive loaded, Configuration::editorArgv()
  // still returns a non-empty argv (just "+1"/path with no real
  // program prepended), so this isn't caught by an empty-argv check --
  // execvp() fails inside the child (silently, matching
  // ChildProcess::runInteractive()'s own contract of not
  // distinguishing "exec failed" from "ran and exited"), and since
  // the file is consequently untouched, edit() reports "unchanged"
  // rather than a distinct "couldn't launch" signal. This mirrors
  // do_edit()'s own equally coarse 0-vs--M_clear return value, which
  // never separately reported a launch failure either.
  fs::path path = makeTempFile();
  CHECK_FALSE(mode.edit(path.string(), 1));
  fs::remove(path);
}
