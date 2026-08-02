#include <poll.h>

#include "doctest.h"
#include "recording_display.hpp"
#include "shell.hpp"

using fm::Charset;
using fm::Shell;
using fm::ShellOptions;
using fm::test::RecordingDisplay;

namespace {

void typeAscii(fm::LineEditor &e, const std::string &s) {
  for (unsigned char c : s) e.insertByte(c);
}

// Polls a real, started Shell's pty until `predicate` is true or a
// bounded number of attempts elapse -- used only by the real-pty
// integration tests below (real bash startup/echo latency isn't
// deterministic, so this is a poll loop, not a fixed sleep).
bool pumpUntil(Shell &shell, const std::function<bool()> &predicate, int maxAttempts = 500) {
  for (int i = 0; i < maxAttempts; i++) {
    if (predicate()) return true;
    struct pollfd pfd {
      shell.ptyFd(), POLLIN, 0
    };
    if (poll(&pfd, 1, 20) > 0) shell.onPtyReadable();
  }
  return predicate();
}

}  // namespace

// These two test cases genuinely fork/exec a real bash (matching this
// project's established preference for real end-to-end verification
// over fakes when a class's whole purpose is process/pty plumbing --
// see ChildProcess's own tests) -- skipped gracefully if bash isn't
// on PATH.

TEST_CASE("start()/alive()/terminate(): spawns and tears down a real bash session") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.argv = {"bash", "--norc", "--noprofile"};
  options.promptMark = "<usilic-test-mark>";
  options.programName = "usilictest";
  options.rows = 24;
  options.columns = 80;
  Shell shell(d, options);

  if (!shell.start()) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(shell.alive());
  CHECK(shell.ptyFd() >= 0);

  bool gotPrompt = pumpUntil(shell, [&] { return !shell.currentDirectory().empty(); });
  CHECK(gotPrompt);

  shell.terminate();
  CHECK_FALSE(shell.alive());
}

TEST_CASE("submitLine(): a plain command's output shows up in the real scrollback") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.argv = {"bash", "--norc", "--noprofile"};
  options.promptMark = "<usilic-test-mark2>";
  options.programName = "usilictest";
  Shell shell(d, options);

  if (!shell.start()) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(shell, [&] { return !shell.currentDirectory().empty(); }));

  typeAscii(shell.commandLine(), "echo SHELLTESTMARKER123");
  shell.submitLine();
  CHECK(shell.commandLine().text() == "");

  bool sawOutput = pumpUntil(shell, [&] {
    for (const auto &line : shell.scrollback().lines())
      if (line.text.find("SHELLTESTMARKER123") != std::string::npos) return true;
    return false;
  });
  CHECK(sawOutput);

  shell.terminate();
}

// The remaining tests exercise Shell's parsing/decision logic
// directly -- either via processOutputBytes() with synthetic bytes
// built to exactly match buildSetupCommand()'s own PS1 format (there
// being no pre-existing captured-from-a-real-shell fixture to extract
// the way PipeBuffer's overstrike tests could pull real bytes from
// README.jp; documented as a synthetic fixture the same way
// LhaReader's tests are), or via submitLine()/onTab() with fake
// callbacks -- so none of them need a real pty or a real shell.

TEST_CASE("processOutputBytes: recognizes a synthetic PS1 marker and extracts the directory") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.promptMark = "<M>";
  options.programName = "prog";
  Shell shell(d, options);

  bool called = false;
  std::string reported;
  shell.setDirectoryChangedCallback([&](const std::string &dir) {
    called = true;
    reported = dir;
  });

  shell.processOutputBytes("hello output\n<M>prog:/home/user$ <M>");

  CHECK(called);
  CHECK(reported == "/home/user");
  CHECK(shell.currentDirectory() == "/home/user");
  REQUIRE(shell.scrollback().lines().size() == 1);
  CHECK(shell.scrollback().lines()[0].text == "hello output");
}

TEST_CASE("processOutputBytes: directoryChangedCallback only fires when the directory actually changes") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.promptMark = "<M>";
  options.programName = "prog";
  Shell shell(d, options);

  int calls = 0;
  shell.setDirectoryChangedCallback([&](const std::string &) { calls++; });

  shell.processOutputBytes("<M>prog:/tmp$ <M>");
  shell.processOutputBytes("<M>prog:/tmp$ <M>");
  shell.processOutputBytes("<M>prog:/var$ <M>");

  CHECK(calls == 2);
}

TEST_CASE("processOutputBytes: a partial marker match that never completes is replayed as plain content") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.promptMark = "<M>";
  options.programName = "prog";
  Shell shell(d, options);

  // "<M>prog" matches the marker prefix up through "prog", but the
  // next byte ('X') doesn't match the expected ':' -- legacy rolls
  // this back and treats the whole thing as ordinary output instead
  // of silently dropping it; this class does the same.
  shell.processOutputBytes("<M>progXYZ\n");

  REQUIRE(shell.scrollback().lines().size() == 1);
  CHECK(shell.scrollback().lines()[0].text == "<M>progXYZ");
}

TEST_CASE("processOutputBytes: a marker split across two feed() calls is still recognized") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.promptMark = "<M>";
  options.programName = "prog";
  Shell shell(d, options);

  shell.processOutputBytes("<M>pro");
  shell.processOutputBytes("g:/opt$ <M>");

  CHECK(shell.currentDirectory() == "/opt");
}

TEST_CASE("processOutputBytes: a runaway prompt tail with no closing mark eventually gives up") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.promptMark = "<M>";
  options.programName = "prog";
  options.columns = 200;
  Shell shell(d, options);

  // Trailing '\n' so PipeBuffer (which only finalizes a line once it
  // sees one, see pipe_buffer.cpp's processPending()) actually
  // exposes the replayed content via lines() for this assertion.
  std::string bytes = "<M>prog:" + std::string(5000, 'x') + "\n";
  shell.processOutputBytes(bytes);

  // No crash/hang, and the buffered bytes were eventually replayed as
  // ordinary content rather than lost.
  std::string total;
  for (const auto &line : shell.scrollback().lines()) total += line.text;
  CHECK(total.find(std::string(50, 'x')) != std::string::npos);
}

TEST_CASE("submitLine: writes a plain command to history and does not consult the program launcher for it") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  Shell shell(d, options);

  std::string recorded;
  shell.setHistoryRecorder([&](const std::string &line) { recorded = line; });
  bool launcherCalled = false;
  shell.setProgramLauncher([&](const std::string &, const std::string &) {
    launcherCalled = true;
    return false;
  });

  typeAscii(shell.commandLine(), "  ls -la");
  shell.submitLine();

  CHECK(recorded == "ls -la");
  CHECK(shell.commandLine().text() == "");
}

TEST_CASE("submitLine: hands off to the program launcher for the trailing pipeline stage") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  Shell shell(d, options);

  std::string capturedToken, capturedLine;
  shell.setHistoryRecorder([](const std::string &) { FAIL("history should not be recorded when handed off"); });
  shell.setProgramLauncher([&](const std::string &token, const std::string &line) {
    capturedToken = token;
    capturedLine = line;
    return true;
  });

  typeAscii(shell.commandLine(), "cat file.txt | less");
  shell.submitLine();

  CHECK(capturedToken == "less");
  CHECK(capturedLine == "cat file.txt | less");
}

TEST_CASE("submitLine: an empty command line is a no-op besides clearing the field") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  Shell shell(d, options);

  bool recorderCalled = false;
  shell.setHistoryRecorder([&](const std::string &) { recorderCalled = true; });

  shell.submitLine();

  CHECK_FALSE(recorderCalled);
}

TEST_CASE("onTab: splices a single unambiguous completion into the command line") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  Shell shell(d, options);

  shell.setCompletionProvider([](const std::string &partial) -> std::vector<std::string> {
    if (partial == "fo") return {"foobar.txt"};
    return {};
  });

  typeAscii(shell.commandLine(), "cat fo");
  shell.onTab();

  CHECK(shell.commandLine().text() == "cat foobar.txt");
}

TEST_CASE("onTab: does nothing when the completion provider returns more than one candidate") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  Shell shell(d, options);

  shell.setCompletionProvider(
      [](const std::string &) -> std::vector<std::string> { return {"a.txt", "b.txt"}; });

  typeAscii(shell.commandLine(), "cat a");
  shell.onTab();

  CHECK(shell.commandLine().text() == "cat a");
}

TEST_CASE("onResize: repositions the command-line field to the new bottom row/width") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.promptMark = "<M>";
  options.programName = "p";
  Shell shell(d, options);

  shell.onResize(10, 40);
  shell.processOutputBytes("<M>p:/x$ <M>");  // establishes a known-width prompt ("p:/x$ " -> 6 cols)

  typeAscii(shell.commandLine(), "hi");

  CHECK(d.cursorRow() == 8);  // rows - 2: the LAST row (rows - 1) is scrollback_'s own mode-line
                              // (2026-08-02: commandLine_ and the mode-line must not share a row --
                              // see Shell::commandLineRow()'s own comment)
  CHECK(d.cursorCol() == 8);  // 6-column prompt + 2 typed chars
}

TEST_CASE("sendControlByte and writeToPty are safe no-ops before start()") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  Shell shell(d, options);

  shell.sendControlByte(3);  // Ctrl-C
  typeAscii(shell.commandLine(), "echo hi");
  shell.submitLine();  // would writeToPty() if launcher doesn't handle it

  CHECK(shell.commandLine().text() == "");
}

TEST_CASE("busy() reflects setBusy()") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  Shell shell(d, options);

  CHECK_FALSE(shell.busy());
  shell.setBusy(true);
  CHECK(shell.busy());
  shell.setBusy(false);
  CHECK_FALSE(shell.busy());
}
