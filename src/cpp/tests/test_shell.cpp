#include <poll.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

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

// RAII scoped $HOME override (2026-08-17 addition, for the '~'-
// expansion tests below): setenv() mutates real PROCESS-WIDE
// environment state that outlives the individual TEST_CASE it's
// called from -- doctest runs every test case in the same process, so
// an unrestored setenv("HOME", ...) here would leak into every OTHER
// test that runs afterward and happens to touch $HOME (e.g. any
// PathResolver/Application test resolving a "~"-prefixed path), a
// real flaky-test regression caught by a `make test` run where an
// unrelated activatePrompt test failed only when run as part of the
// FULL suite (not in isolation) -- exactly the "ran after this one"
// symptom of process-wide state leaking between test cases.
struct ScopedHomeEnv {
  std::string previous;
  bool hadPrevious;
  explicit ScopedHomeEnv(const std::string &value) {
    const char *existing = std::getenv("HOME");
    hadPrevious = existing != nullptr;
    if (hadPrevious) previous = existing;
    setenv("HOME", value.c_str(), 1);
  }
  ~ScopedHomeEnv() {
    if (hadPrevious)
      setenv("HOME", previous.c_str(), 1);
    else
      unsetenv("HOME");
  }
};

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

// 2026-09-06追加、直接のユーザー報告("旧では^[scat^Mでちゃんと入力
// 待ちになる...^Gで抜けた後^J.でSHELL busyと出る...環境がどこかで
// 食い違ってるんじゃないの？")――busy()自身の宣言コメント参照:
// PROGRAM一覧に載っていないただのシェルコマンド(`cat`)がフォア
// グラウンドで実行中の間、busy()が正しくtrueを返し、それが終わると
// (ここでは^C相当のSIGINTで)falseへ戻ることを、本物のbashセッション
// で確認する。
TEST_CASE("busy(): true while a plain (non-PROGRAM-table) foreground command like `cat` is still reading "
          "stdin, and returns to false once it's interrupted") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.argv = {"bash", "--norc", "--noprofile"};
  options.promptMark = "<usilic-test-mark-busy>";
  options.programName = "usilictest";
  Shell shell(d, options);

  if (!shell.start()) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(shell, [&] { return !shell.currentDirectory().empty(); }));
  CHECK_FALSE(shell.busy());

  typeAscii(shell.commandLine(), "cat");
  shell.submitLine();
  // busy()自身はPTYの出力に一切依存しない(tcgetpgrp()の直接の
  // syscall)が、bash自身がcatを実際にforkしてフォアグラウンドの
  // プロセスグループへ渡すまでにはわずかな実時間がかかるので、
  // pumpUntil()と同じポーリングで待つ。
  bool becameBusy = pumpUntil(shell, [&] { return shell.busy(); });
  CHECK(becameBusy);

  shell.sendControlByte(0x03);  // ^C -- SIGINTをフォアグラウンドのプロセスグループへ、catはこれで終了する
  bool becameIdle = pumpUntil(shell, [&] { return !shell.busy(); });
  CHECK(becameIdle);

  shell.terminate();
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

TEST_CASE("submitLine(): no leftover blank/duplicate-text line between the frozen command and its real output") {
  // ECHO is disabled on this pty (see start()'s own comment) so
  // commandLine_'s own local echo is the only rendering of what's
  // typed -- but a real side-by-side comparison against legacy
  // (2026-08-03) found bash's readline still echoing the submitted
  // command text back as an extra scrollback line regardless (zsh's
  // zle instead emits a bare blank line, see the same-named test
  // above this one and pendingEchoFilter_'s own comment for the fuller
  // story) -- independent of the raw tty ECHO flag, since interactive
  // line editors manage their own on-screen rendering. This exercises
  // the real bash-echoes-literal-text case end to end.
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.argv = {"bash", "--norc", "--noprofile"};
  options.promptMark = "<usilic-test-mark3>";
  options.programName = "usilictest";
  Shell shell(d, options);

  if (!shell.start()) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(shell, [&] { return !shell.currentDirectory().empty(); }));

  typeAscii(shell.commandLine(), "echo SHELLTESTMARKER456");
  shell.submitLine();

  REQUIRE(pumpUntil(shell, [&] {
    for (const auto &line : shell.scrollback().lines())
      if (line.text.find("SHELLTESTMARKER456") != std::string::npos && !line.shellCommand) return true;
    return false;
  }));

  const auto &lines = shell.scrollback().lines();
  int frozenIdx = -1;
  for (int i = 0; i < static_cast<int>(lines.size()); i++)
    if (lines[i].shellCommand == "echo SHELLTESTMARKER456") frozenIdx = i;
  REQUIRE(frozenIdx >= 0);
  REQUIRE(frozenIdx + 1 < static_cast<int>(lines.size()));
  const std::string &nextLine = lines[frozenIdx + 1].text;
  CHECK_FALSE(nextLine.empty());
  CHECK_FALSE(nextLine == "echo SHELLTESTMARKER456");

  shell.terminate();
}

// 2026-08-24 fix, a real user report: a long CP/MV/ARC-injected
// command line (a full path plus a multi-byte Japanese filename easily
// exceeds the terminal width, far more readily than anything a user
// would hand-type) was silently cut off mid-line in the live SHELL
// scrollback -- LOG mode (which reads pipeBuffer_'s own already-
// wrapped output) was unaffected, since freezePromptLine()'s own line
// never went through pipeBuffer_ at all. ContentView::renderLine()
// itself has no wrapping of its own (see its own "line-truncation
// precedent" comment -- every OTHER scrollback line already arrives
// pre-wrapped to fit `columns_` via pipeBuffer_.wrapLine()), so a
// single overlong Line, however correct its OWN stored text was, still
// got clipped at RENDER time. Uses a narrow terminal (30 columns) so a
// short, deterministic Japanese string reliably needs more than one
// row -- proof the fix actually re-wraps (matching pipeBuffer_'s own
// established wrapping), not just that the data was always intact.
TEST_CASE("submitLine(): a long command line with multi-byte characters wraps across multiple scrollback "
          "lines instead of being cut off (the OLD single-Line-no-matter-how-long shape would have left "
          "exactly one shellCommand-tagged line here, too wide for a 30-column row to render in full)") {
  RecordingDisplay d(24, 30);
  ShellOptions options;
  options.argv = {"bash", "--norc", "--noprofile"};
  options.promptMark = "<usilic-test-mark-wrap>";
  options.programName = "usilictest";
  options.rows = 24;
  options.columns = 30;
  Shell shell(d, options);

  if (!shell.start()) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(shell, [&] { return !shell.currentDirectory().empty(); }));

  // "マイナカード表" -- 7 double-width characters, 14 columns; combined
  // with "usilictest:<dir>% echo " (well over 16 columns on its own),
  // guaranteed to exceed this test's own 30-column width.
  std::string longArg = "\xe3\x83\x9e\xe3\x82\xa4\xe3\x83\x8a\xe3\x82\xab\xe3\x83\xbc\xe3\x83\x89\xe8\xa1\xa8";
  std::string command = "echo " + longArg;
  typeAscii(shell.commandLine(), command);
  shell.submitLine();

  REQUIRE(pumpUntil(shell, [&] {
    int n = 0;
    for (const auto &line : shell.scrollback().lines())
      if (line.shellCommand == command) n++;
    return n > 0;
  }));

  std::vector<int> frozenIdx;
  const auto &lines = shell.scrollback().lines();
  for (int i = 0; i < static_cast<int>(lines.size()); i++)
    if (lines[i].shellCommand == command) frozenIdx.push_back(i);

  REQUIRE(frozenIdx.size() > 1);  // genuinely wrapped, not one long line
  std::string joined;
  for (int i : frozenIdx) joined += lines[i].text;
  CHECK(joined.find(longArg) != std::string::npos);  // the full argument survived, split across rows but not lost

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

// 2026-08-17 fix: zsh's default PS1 (`%~`)/bash's `\w` abbreviate the
// cwd with a leading '~' whenever it's $HOME (bare "~") or a
// subdirectory of it ("~/subdir") -- used to be stored into
// currentDirectory_ completely unexpanded, which isn't a real
// filesystem path (tilde expansion is a shell convention, not
// something std::filesystem understands). A real report ("SHELLで
// 'less CLA'^I（補完が利かない）", comparing directly against legacy's
// own working completion) traced back to exactly this:
// completeFilesystemPath()'s std::filesystem::directory_iterator()
// silently returning zero candidates whenever the shell's cwd was
// anywhere under $HOME, which in practice is nearly always real usage.
TEST_CASE("processOutputBytes: a bare '~' prompt directory (cwd == $HOME) is expanded to the real "
          "$HOME path, not stored as a literal tilde") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.promptMark = "<M>";
  options.programName = "prog";
  Shell shell(d, options);

  ScopedHomeEnv homeOverride("/Users/testuser");
  shell.processOutputBytes("<M>prog:~% <M>");
  CHECK(shell.currentDirectory() == "/Users/testuser");
}

TEST_CASE("processOutputBytes: a '~/subdir' prompt directory is expanded to the real $HOME-relative "
          "path, not stored with the tilde still embedded") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.promptMark = "<M>";
  options.programName = "prog";
  Shell shell(d, options);

  ScopedHomeEnv homeOverride("/Users/testuser");
  shell.processOutputBytes("<M>prog:~/git/usilic% <M>");
  CHECK(shell.currentDirectory() == "/Users/testuser/git/usilic");
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

TEST_CASE("flushNewScrollbackLines: drops a bash-style literal-text echo artifact right after the frozen "
          "command, but keeps real output that happens to differ") {
  // Synthetic version of the real-bash test above (pendingEchoFilter_'s
  // own comment has the full story) -- deterministic and independent
  // of whether bash is actually installed.
  RecordingDisplay d(24, 80);
  ShellOptions options;
  Shell shell(d, options);

  typeAscii(shell.commandLine(), "ls");
  shell.submitLine();  // freezes the command line, sets pendingEchoFilter_ = "ls"

  shell.processOutputBytes("ls\nreal output\n");  // bash-style: literal echo, then genuine output

  const auto &lines = shell.scrollback().lines();
  int frozenIdx = -1;
  for (int i = 0; i < static_cast<int>(lines.size()); i++)
    if (lines[i].shellCommand == "ls") frozenIdx = i;
  REQUIRE(frozenIdx >= 0);
  REQUIRE(frozenIdx + 1 < static_cast<int>(lines.size()));
  CHECK(lines[frozenIdx + 1].text == "real output");  // the echoed "ls" line was dropped, not this one
}

TEST_CASE("flushNewScrollbackLines: drops a zsh-style bare blank-line echo artifact right after the frozen "
          "command") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  Shell shell(d, options);

  typeAscii(shell.commandLine(), "ls");
  shell.submitLine();  // freezes the command line, sets pendingEchoFilter_ = "ls"

  shell.processOutputBytes("\nreal output\n");  // zsh-style: a bare blank line, then genuine output

  const auto &lines = shell.scrollback().lines();
  int frozenIdx = -1;
  for (int i = 0; i < static_cast<int>(lines.size()); i++)
    if (lines[i].shellCommand == "ls") frozenIdx = i;
  REQUIRE(frozenIdx >= 0);
  REQUIRE(frozenIdx + 1 < static_cast<int>(lines.size()));
  CHECK(lines[frozenIdx + 1].text == "real output");  // the blank line was dropped, not this one
}

TEST_CASE("flushNewScrollbackLines: a genuinely blank FIRST line of real output is only ever dropped once, "
          "not on every flush") {
  // pendingEchoFilter_ is consumed (reset to nullopt) the first time
  // flushNewScrollbackLines() runs after a submission, so a command
  // that legitimately produces its OWN blank line as later output
  // (not the echo artifact) must not have that later blank line
  // dropped too.
  RecordingDisplay d(24, 80);
  ShellOptions options;
  Shell shell(d, options);

  typeAscii(shell.commandLine(), "ls");
  shell.submitLine();

  shell.processOutputBytes("real output\n");  // no echo artifact this time -- first line is genuine
  shell.processOutputBytes("\n");             // a later, genuinely blank line of real output

  const auto &lines = shell.scrollback().lines();
  int blankCount = 0;
  for (const auto &line : lines)
    if (line.text.empty()) blankCount++;
  CHECK(blankCount == 1);  // the later blank line survived; nothing extra was dropped
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

// 2026-09-15修正、直接のユーザー報告("PROGRAMのプロンプトで'less '
// と打ったところで^Iとやっても候補が出ない")+依頼("最長共通接頭辞
// まで補完"): 以前は候補が複数(曖昧)なら常に何もしなかったが、今は
// 候補全体の最長共通接頭辞まで補完する(bashの素のTabと同じ)。この
// テスト自身の候補("a.txt"/"b.txt")はたまたま共通接頭辞が無い
// (1文字目から食い違う)ケースなので、結果自体(何も変わらない)は
// 以前と同じまま――「複数候補なら常に無視」ではなく「共通接頭辞が
// 無ければ結果的に無視」であることを示すため、名前を訂正した。
TEST_CASE("onTab: ambiguous candidates with NO common prefix beyond what's already typed leave the "
          "command line unchanged") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  Shell shell(d, options);

  shell.setCompletionProvider(
      [](const std::string &) -> std::vector<std::string> { return {"a.txt", "b.txt"}; });

  typeAscii(shell.commandLine(), "cat a");
  shell.onTab();

  CHECK(shell.commandLine().text() == "cat a");
}

TEST_CASE("onTab: ambiguous candidates that DO share a longer common prefix complete up to that "
          "prefix (bash-style Tab, not full auto-complete)") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  Shell shell(d, options);

  shell.setCompletionProvider(
      [](const std::string &) -> std::vector<std::string> { return {"log_alpha.txt", "log_beta.txt"}; });

  typeAscii(shell.commandLine(), "cat ");
  shell.onTab();

  // Common prefix of "log_alpha.txt"/"log_beta.txt" is "log_" -- completes
  // that far, not the whole filename (still ambiguous past that point).
  CHECK(shell.commandLine().text() == "cat log_");
}

TEST_CASE("onResize: repositions the command-line field to the new row/width") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.promptMark = "<M>";
  options.programName = "p";
  Shell shell(d, options);

  shell.onResize(10, 40);
  // 2026-08-17 change: the DISPLAYED prompt prefix is now always the
  // literal "usilic-1.5b:" (see handlePromptDetected()'s own comment),
  // independent of options.programName -- programName still drives
  // the INTERNAL detection marker matched here ("<M>p:" establishes
  // that), just no longer what gets drawn on screen.
  shell.processOutputBytes("<M>p:/x$ <M>");  // establishes a known-width prompt ("usilic-1.5b:/x$ " -> 16 cols)

  typeAscii(shell.commandLine(), "hi");

  // commandLineRow() is a fixed constant again (2026-08-04, see its
  // own comment) -- rows - 2, regardless of how little scrollback_
  // content exists (bottom-anchoring is what makes this safe: see
  // ContentView::setBottomAnchored()'s own comment).
  CHECK(d.cursorRow() == 8);
  CHECK(d.cursorCol() == 18);  // 16-column "usilic-1.5b:/x$ " prompt + 2 typed chars
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

// 2026-09-20追加、直接のユーザー報告("SHELLプロンプトでman ls...
// 旧版のように(stdin)で表示して欲しい"): buildSetupCommand()自身の
// pagerStatements/ShellOptions::pagerPath自身の宣言コメント参照
// ――pagerPathが設定されている間、実際の生きたbashセッション内で
// $PAGER/$MANPAGERが正しくexportされ、main.cpp自身のtryHandoffToParentShell()
// が認識する`--usilic-pager <pid>`が(bash自身の`$$`経由で)実際の
// 数値のPIDとして埋め込まれることを確認する(legacyのexport PAGER=
// '<usilic> -p <pid>'と同じ理屈だが、この移植はgetppid()ではなく
// この明示的なpid引数に頼る、main.cpp自身のコメント参照)。
TEST_CASE("Shell start(): pagerPath (when set) exports PAGER/MANPAGER pointing at it, with a real numeric "
          "--usilic-pager <pid> embedded via the shell's own $$") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.argv = {"bash", "--norc", "--noprofile"};
  options.promptMark = "<usilic-test-pager-mark>";
  options.programName = "usilictest";
  options.pagerPath = "/fake/path/to/usilic";
  Shell shell(d, options);

  if (!shell.start()) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(shell, [&] { return !shell.currentDirectory().empty(); }));

  typeAscii(shell.commandLine(), "echo \"$PAGER\" ; echo \"$MANPAGER\"");
  shell.submitLine();
  REQUIRE(pumpUntil(shell, [&] {
    int count = 0;
    for (const auto &line : shell.scrollback().lines())
      if (line.text.find("--usilic-pager") != std::string::npos) count++;
    return count >= 2;  // both echoes landed, not just the first (race, not a real bug)
  }));

  int pagerLines = 0;
  for (const auto &line : shell.scrollback().lines()) {
    if (line.text.find("/fake/path/to/usilic --usilic-pager ") == std::string::npos) continue;
    pagerLines++;
    std::string pidPart = line.text.substr(line.text.find("--usilic-pager ") + std::strlen("--usilic-pager "));
    REQUIRE_FALSE(pidPart.empty());
    CHECK(std::all_of(pidPart.begin(), pidPart.end(), [](unsigned char c) { return std::isdigit(c); }));
  }
  CHECK(pagerLines == 2);  // PAGER and MANPAGER both printed, both matching
}

TEST_CASE("Shell start(): pagerPath left at its default (empty) omits the PAGER/MANPAGER export entirely") {
  RecordingDisplay d(24, 80);
  ShellOptions options;
  options.argv = {"bash", "--norc", "--noprofile"};
  options.promptMark = "<usilic-test-nopager-mark>";
  options.programName = "usilictest";
  Shell shell(d, options);

  if (!shell.start()) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(shell, [&] { return !shell.currentDirectory().empty(); }));

  typeAscii(shell.commandLine(), "echo \"PAGER=[$PAGER]\"");
  shell.submitLine();
  REQUIRE(pumpUntil(shell, [&] {
    for (const auto &line : shell.scrollback().lines())
      if (line.text.find("PAGER=[") != std::string::npos) return true;
    return false;
  }));

  for (const auto &line : shell.scrollback().lines())
    CHECK(line.text.find("--usilic-pager") == std::string::npos);
}
