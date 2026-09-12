#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "doctest.h"
#include "history.hpp"
#include "key_input.hpp"
#include "program_mode.hpp"

namespace fs = std::filesystem;

using fm::History;
using fm::KeyInput;
using fm::ProgramMode;
using fm::RingCategory;

namespace {

// Real call-count tracking (2026-08-20 addition, docs/HISTORY.md:9023)
// for NullDisplay::leaveForExternalProgram()/reenterFromExternalProgram()
// below -- file-scope (not members of the anonymous local struct
// itself) so a test can read/reset them via leaveExternalProgramCount()/
// reenterExternalProgramCount() without needing a NullDisplay-typed
// reference (Fixture::display() only ever returns the base fm::Display&).
// Persist across the whole test binary's run (the static NullDisplay
// instance is a single process-wide singleton) -- harmless, since only
// the ONE test that cares about these counts reads them, and it resets
// both to 0 itself before running.
int &leaveExternalProgramCount() {
  static int count = 0;
  return count;
}
int &reenterExternalProgramCount() {
  static int count = 0;
  return count;
}

// KeyInput's raw-mode methods safely no-op when it was never open()ed
// against a real tty (see key_input.cpp), so these tests can run
// headlessly -- ChildProcess::runInteractive() itself genuinely forks
// and execs real commands, which is what actually matters here.
struct Fixture {
  KeyInput keyInput{display()};
  History history;
  ProgramMode mode{keyInput, display(), history};

  static fm::Display &display() {
    static struct NullDisplay : fm::Display {
      void write(const std::string &) override {}
      void putChar(int) override {}
      void flush() override {}
      void moveCursor(int, int) override {}
      void setVisibleCursor(int, int) override {}
      void hideVisibleCursor() override {}
      void cursorLeft() override {}
      void saveCursor() override {}
      void restoreCursor() override {}
      void clearScreen() override {}
      void clearToEndOfLine() override {}
      void scrollForward() override {}
      void scrollReverse() override {}
      void setAttribute(Attribute) override {}
      std::string attributeEscape(Attribute) const override { return ""; }
      int rows() const override { return 24; }
      int columns() const override { return 80; }
      void setSize(int, int) override {}
      std::string keySequence(Key) const override { return ""; }
      bool outputEnabled() const override { return true; }
      void setOutputEnabled(bool) override {}
      bool smoothScrollEnabled() const override { return false; }
      void setSmoothScrollEnabled(bool) override {}
      bool modeLineVisible() const override { return true; }
      void setModeLineVisible(bool) override {}
      bool hasNativeScrollbar() const override { return false; }
      void open() override {}
      void close() override {}
      void leaveForExternalProgram() override { leaveExternalProgramCount()++; }
      void reenterFromExternalProgram() override { reenterExternalProgramCount()++; }
      void renderScrolled(const std::vector<ScrolledLine> &, int, int, const std::vector<ModeLineSegment> &) override {}
    } instance;
    return instance;
  }
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "run: executes a real shell command and returns its exit status") {
  CHECK(mode.run("exit 0") == 0);
  CHECK(mode.run("exit 3") == 3);
}

TEST_CASE_FIXTURE(Fixture, "run: an empty command is a safe no-op, not run as an empty shell script") {
  CHECK(mode.run("") == -1);
}

TEST_CASE_FIXTURE(Fixture, "run: the command actually executes with real side effects") {
  char tmpl[] = "/tmp/programmode_testXXXXXX";
  int fd = mkstemp(tmpl);
  REQUIRE(fd >= 0);
  close(fd);
  std::string path(tmpl);

  std::string command = "echo hello > '" + path + "'";
  CHECK(mode.run(command) == 0);

  std::ifstream in(path);
  std::string content;
  std::getline(in, content);
  CHECK(content == "hello");
  fs::remove(path);
}

TEST_CASE_FIXTURE(Fixture, "run: records the command into History's ShellCommand ring") {
  mode.run("exit 0");
  const auto &entries = history.recentEntries(RingCategory::ShellCommand);
  REQUIRE_FALSE(entries.empty());
  CHECK(entries.back() == "exit 0");
}

TEST_CASE_FIXTURE(Fixture, "run: an empty command is not recorded into history") {
  mode.run("");
  CHECK(history.recentEntries(RingCategory::ShellCommand).empty());
}

// 2026-08-17 addition: Application::executeProgram()'s own `cd '<dir>'
// && ` prefix (see its own comment) is an internal implementation
// detail, not something the user typed -- recording it into history
// would corrupt ^J^R's command recall and the ^J^P PROGRAM list with a
// string that doesn't start with a recognized program name if
// resubmitted. run()'s SECOND argument lets a caller record something
// different from what actually executed.
TEST_CASE_FIXTURE(Fixture, "run: records recordedCommand (not command) into history, when given") {
  CHECK(mode.run("cd /tmp && echo hi", "echo hi") == 0);
  const auto &entries = history.recentEntries(RingCategory::ShellCommand);
  REQUIRE_FALSE(entries.empty());
  CHECK(entries.back() == "echo hi");
}

TEST_CASE_FIXTURE(Fixture, "run: falls back to recording command itself when recordedCommand is omitted") {
  mode.run("exit 0");
  const auto &entries = history.recentEntries(RingCategory::ShellCommand);
  REQUIRE_FALSE(entries.empty());
  CHECK(entries.back() == "exit 0");
}

// setForegroundLauncher() (GUI-mode addition): when a launcher is
// wired and returns true, run() must trust it handled the WHOLE
// foreground handoff itself and skip ChildProcess::runInteractive()
// entirely -- proven here via a real side effect (the file the shell
// command would have written) that only exists if runInteractive()
// actually ran, not by mocking anything internal.
TEST_CASE_FIXTURE(Fixture, "run: a launcher that returns true skips ChildProcess::runInteractive() entirely") {
  char tmpl[] = "/tmp/programmode_launcher_testXXXXXX";
  int fd = mkstemp(tmpl);
  REQUIRE(fd >= 0);
  close(fd);
  std::string path(tmpl);
  fs::remove(path);  // exists only if the real shell command actually ran

  std::string receivedCommand;
  mode.setForegroundLauncher([&](const std::string &command) {
    receivedCommand = command;
    return true;
  });

  std::string command = "echo hello > '" + path + "'";
  CHECK(mode.run(command) == 0);
  CHECK(receivedCommand == command);
  CHECK_FALSE(fs::exists(path));  // the real shell command never ran
}

TEST_CASE_FIXTURE(Fixture, "run: a launcher that returns false falls back to ChildProcess::runInteractive()") {
  char tmpl[] = "/tmp/programmode_launcher_fallback_testXXXXXX";
  int fd = mkstemp(tmpl);
  REQUIRE(fd >= 0);
  close(fd);
  std::string path(tmpl);
  fs::remove(path);

  bool launcherCalled = false;
  mode.setForegroundLauncher([&](const std::string &) {
    launcherCalled = true;
    return false;
  });

  std::string command = "echo hello > '" + path + "'";
  CHECK(mode.run(command) == 0);
  CHECK(launcherCalled);
  REQUIRE(fs::exists(path));  // fallback path really executed the command
  fs::remove(path);
}

TEST_CASE_FIXTURE(Fixture, "run: history is still recorded when a launcher handles the command") {
  mode.setForegroundLauncher([](const std::string &) { return true; });
  mode.run("less somefile", "less somefile");
  const auto &entries = history.recentEntries(RingCategory::ShellCommand);
  REQUIRE_FALSE(entries.empty());
  CHECK(entries.back() == "less somefile");
}

// 2026-08-20 addition, docs/HISTORY.md:9023: a real user question after
// diagnosing this session's own SIGTERM/raw-mode work -- "-cでも起きる
// んだが、^Jp'vi CLAUDE.md'[return]は問題無い。lessとviの違いって何？"
// (why does the FIRST `less` launched from PROGRAM mode render broken
// while `vi` never does). Root-caused by reading legacy vt.c's own
// vt_init(-2)/(2): leaving raw mode for an external program ALSO
// resets the scroll region to the full screen, re-constraining it on
// return -- this port's own leaveRawMode()/reenterRawMode() pairing
// only ported the termios half, dropping the scroll-region half
// entirely (TerminalDisplay's own applyScrollRegion() constrains the
// scroll region once at startup and never revisits it around an
// external program). Display::leaveForExternalProgram()/
// reenterFromExternalProgram() are the fix; this confirms run() calls
// them in the correct order around the real child process (not just
// that TerminalDisplay's OWN escape-sequence output looks right in
// isolation -- that's PTY-verified separately, this is the wiring).
TEST_CASE_FIXTURE(Fixture, "run: calls Display::leaveForExternalProgram() before, and "
                           "reenterFromExternalProgram() after, running the real child process") {
  leaveExternalProgramCount() = 0;
  reenterExternalProgramCount() = 0;
  CHECK(mode.run("exit 0") == 0);
  CHECK(leaveExternalProgramCount() == 1);
  CHECK(reenterExternalProgramCount() == 1);
}

TEST_CASE_FIXTURE(Fixture, "run: a launcher that handles the command itself skips both "
                           "leaveForExternalProgram()/reenterFromExternalProgram() -- it owns the whole "
                           "foreground handoff, same as it already skips ChildProcess::runInteractive()") {
  leaveExternalProgramCount() = 0;
  reenterExternalProgramCount() = 0;
  mode.setForegroundLauncher([](const std::string &) { return true; });
  CHECK(mode.run("less somefile") == 0);
  CHECK(leaveExternalProgramCount() == 0);
  CHECK(reenterExternalProgramCount() == 0);
}
