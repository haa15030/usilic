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

// KeyInput's raw-mode methods safely no-op when it was never open()ed
// against a real tty (see key_input.cpp), so these tests can run
// headlessly -- ChildProcess::runInteractive() itself genuinely forks
// and execs real commands, which is what actually matters here.
struct Fixture {
  KeyInput keyInput{display()};
  History history;
  ProgramMode mode{keyInput, history};

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
      void open() override {}
      void close() override {}
      void renderScrolled(const std::vector<std::string> &, int, int, const std::string &) override {}
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
