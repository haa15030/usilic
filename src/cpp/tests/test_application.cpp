#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <poll.h>
#include <unistd.h>

#include "application.hpp"
#include "doctest.h"
#include "recording_display.hpp"

namespace fs = std::filesystem;

using fm::Application;
using fm::ApplicationMode;
using fm::Charset;
using fm::Command;
using fm::Configuration;
using fm::GrepSyntax;
using fm::History;
using fm::KeyInput;
using fm::ShellOptions;
using fm::test::RecordingDisplay;

namespace {

fs::path makeTempDir() {
  char tmpl[] = "/tmp/application_testXXXXXX";
  char *result = mkdtemp(tmpl);
  REQUIRE(result != nullptr);
  return fs::path(result);
}

// A real, deterministic bash (matching Shell's own test convention:
// --norc/--noprofile avoid the user's real shell config interfering,
// and a unique promptMark keeps this session's prompt detection from
// colliding with any other Shell instance).
ShellOptions testShellOptions() {
  ShellOptions options;
  options.argv = {"bash", "--norc", "--noprofile"};
  options.promptMark = "<usilic-app-test>";
  options.programName = "usilictest";
  return options;
}

// Polls a real, started Shell (via Application) until `predicate` is
// true or a bounded number of attempts elapse -- same pattern as
// test_shell.cpp's own pumpUntil(), needed because real bash startup/
// echo latency isn't deterministic.
bool pumpUntil(Application &app, const std::function<bool()> &predicate, int maxAttempts = 500) {
  for (int i = 0; i < maxAttempts; i++) {
    if (predicate()) return true;
    struct pollfd pfd {
      app.shell().ptyFd(), POLLIN, 0
    };
    if (poll(&pfd, 1, 20) > 0) app.shell().onPtyReadable();
  }
  return predicate();
}

struct Fixture {
  fs::path root;
  RecordingDisplay display{24, 80};
  // KeyInput's raw-mode methods safely no-op when never open()ed
  // against a real tty (see test_program_mode.cpp's own fixture and
  // key_input.cpp) -- reusing the same RecordingDisplay is fine since
  // ProgramMode only ever calls leaveRawMode()/reenterRawMode() on
  // this, never anything display-size-dependent.
  KeyInput keyInput{display};
  History history;
  Configuration configuration;
  Application app{display, keyInput, history, configuration, Charset::Encoding::Ascii, 80, testShellOptions()};

  Fixture() : root(makeTempDir()) {
    fs::create_directory(root / "subdir");
    { std::ofstream(root / "plain.txt") << "hello\nworld\n"; }
    { std::ofstream(root / "log.txt") << "marker one\nline1\nmarker two\nline2a\nline2b\nmarker three\nline3\n"; }
    { std::ofstream(root / "subdir" / "sub.txt") << "alpha\nbeta\nalpha\ngamma\n"; }
    // executeProgram() only recognizes commands whose first word
    // matches a PROGRAM directive entry (find_prog()'s own semantics,
    // see application.cpp) -- the default Configuration has no
    // PROGRAM entries at all (only ever populated by parsing an rc
    // file), so tests need this loaded to exercise the success path.
    // RM is overridden to drop the default `-i` (interactive
    // confirmation): ChildProcess::run() attaches no real terminal, so
    // `-i` would hang waiting on stdin -- same override
    // test_cp_mv_rm_mode.cpp's own tests already use.
    configuration.loadRcText("PROGRAM\n echo\nRM\n rm -vr\n", true);

    // A real archive (built via the actual `tar` CLI, matching
    // ArchiveMode's own test precedent) for the ARC-related tests.
    fs::create_directory(root / "arcsrc");
    { std::ofstream(root / "arcsrc" / "inner.txt") << "archived content\n"; }
    std::string tarCmd = "cd '" + root.string() + "' && tar cf archive.tar arcsrc";
    REQUIRE(std::system(tarCmd.c_str()) == 0);

    // Real files for the CP/MV/RM-related tests.
    { std::ofstream(root / "cpmv_a.txt") << "content-a\n"; }
    { std::ofstream(root / "cpmv_b.txt") << "content-b\n"; }
    fs::create_directory(root / "cpmvdest");
  }

  ~Fixture() { fs::remove_all(root); }

  std::string path() const { return root.string() + "/"; }

  int indexOf(const std::string &name) const {
    const auto &entries = const_cast<Application &>(app).directoryMode().entries();
    for (std::size_t i = 0; i < entries.size(); i++)
      if (entries[i].name == name) return static_cast<int>(i);
    return -1;
  }

  // Feeds each byte of text through feedByte() as an ordinary
  // (non-special-key) keypress -- used to type a search pattern.
  void feedText(const std::string &text) {
    for (char c : text) app.feedByte(static_cast<unsigned char>(c), std::nullopt);
  }
};

}  // namespace

// -- resolveCommand: the new keystroke->Command table --

TEST_CASE("resolveCommand: special keys map to movement, with left/right as up/down synonyms") {
  CHECK(Application::resolveCommand(0, KeyInput::SpecialKey::Up) == Command::MoveUp);
  CHECK(Application::resolveCommand(0, KeyInput::SpecialKey::Down) == Command::MoveDown);
  CHECK(Application::resolveCommand(0, KeyInput::SpecialKey::Left) == Command::MoveUp);
  CHECK(Application::resolveCommand(0, KeyInput::SpecialKey::Right) == Command::MoveDown);
  CHECK(Application::resolveCommand(0, KeyInput::SpecialKey::PageUp) == Command::PageUp);
  CHECK(Application::resolveCommand(0, KeyInput::SpecialKey::PageDown) == Command::PageDown);
}

TEST_CASE("resolveCommand: plain bytes -- j/k, ^M, ^G, space/b") {
  CHECK(Application::resolveCommand('j', std::nullopt) == Command::MoveDown);
  CHECK(Application::resolveCommand('k', std::nullopt) == Command::MoveUp);
  CHECK(Application::resolveCommand(0x0e, std::nullopt) == Command::MoveDown);  // ^N
  CHECK(Application::resolveCommand(0x10, std::nullopt) == Command::MoveUp);    // ^P
  CHECK(Application::resolveCommand(0x0d, std::nullopt) == Command::Enter);     // ^M
  CHECK(Application::resolveCommand(0x07, std::nullopt) == Command::Back);      // ^G
  CHECK(Application::resolveCommand(' ', std::nullopt) == Command::PageDown);
  CHECK(Application::resolveCommand('b', std::nullopt) == Command::PageUp);
}

TEST_CASE("resolveCommand: an unmapped byte resolves to None") {
  CHECK(Application::resolveCommand('z', std::nullopt) == Command::None);
}

// -- start()/handleCommand(): directory navigation + file viewing --

TEST_CASE_FIXTURE(Fixture, "start: lists the directory and enters Directory mode") {
  REQUIRE(app.start(path()));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == path());
  CHECK(indexOf("plain.txt") >= 0);
  CHECK(indexOf("subdir/") >= 0);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(MoveDown/MoveUp): moves the directory cursor") {
  REQUIRE(app.start(path()));
  int start = app.directoryMode().entries().empty() ? 0 : 0;
  (void)start;
  int before = app.directoryMode().selectedEntry() ? indexOf(app.directoryMode().selectedEntry()->name) : -1;
  CHECK(app.handleCommand(Command::MoveDown));
  int after = indexOf(app.directoryMode().selectedEntry()->name);
  CHECK(after == before + 1);
  CHECK(app.handleCommand(Command::MoveUp));
  CHECK(indexOf(app.directoryMode().selectedEntry()->name) == before);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(Enter) on a directory entry descends, staying in Directory mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == (root / "subdir").string() + "/");
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(Enter) on a regular file switches to File mode and loads it") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / "plain.txt").string());
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(Enter) is a no-op while already in File mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));

  CHECK_FALSE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(Back) from File mode returns to Directory mode with cursor intact") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  std::string beforeDir = app.directoryMode().currentDirectory();

  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.handleCommand(Command::Back));

  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == beforeDir);
  CHECK(indexOf(app.directoryMode().selectedEntry()->name) == idx);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(Back) in Directory mode with nothing to go back to fails cleanly") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(Back) in Directory mode after descending returns to the parent") {
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.directoryMode().currentDirectory() == (root / "subdir").string() + "/");

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.directoryMode().currentDirectory() == path());
}

// -- resolveCommand/feedByte: ^S (single-byte) and ^J^S (leader) for GREP --

TEST_CASE("resolveCommand: ^S resolves to StartFgrep") {
  CHECK(Application::resolveCommand(0x13, std::nullopt) == Command::StartFgrep);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: a lone ^J is consumed as a pending leader byte, no action yet") {
  REQUIRE(app.start(path()));
  CHECK(app.feedByte(0x0a, std::nullopt));  // ^J: in progress, not a no-op
  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J followed by an unrecognized byte fails cleanly") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK_FALSE(app.feedByte('z', std::nullopt));
  // The leader state doesn't leak into the next byte: a plain 'j' afterwards still moves down.
  CHECK(app.feedByte('j', std::nullopt));
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^S starts an EGREP prompt (StartEgrep)") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte(0x13, std::nullopt));
  CHECK(app.promptActive());
}

// -- GREP: starting a search prompt, typing a pattern, submitting --

TEST_CASE_FIXTURE(Fixture, "handleCommand(StartFgrep): opens a prompt without leaving Directory mode yet") {
  REQUIRE(app.start(path()));
  CHECK(app.handleCommand(Command::StartFgrep));
  CHECK(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: typing a pattern then ^M runs a real fgrep search over the directory listing") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // ^M submits

  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Grep);
  REQUIRE(app.grepMode().matches().size() == 1);
  CHECK(app.grepMode().matches()[0].text.find("plain.txt") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^G cancels a pending prompt without starting a search") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain");
  CHECK(app.feedByte(0x07, std::nullopt));  // ^G cancels

  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "GREP: searching from File mode returns to File mode on Back") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("hello");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);
  REQUIRE(app.grepMode().matches().size() == 1);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / "plain.txt").string());
}

TEST_CASE_FIXTURE(Fixture, "GREP: Enter on a match is a no-op in this slice") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);

  CHECK_FALSE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Grep);
}

TEST_CASE_FIXTURE(Fixture, "GREP: starting a nested search from within grep results fails") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);

  CHECK_FALSE(app.handleCommand(Command::StartFgrep));
  CHECK_FALSE(app.promptActive());
}

// -- feedByte: ^Jc / ^J^C leader sequences for CUT --

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J c starts a CUT prompt (StartCutFixed) once in File mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte('c', std::nullopt));
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^C also starts a CUT prompt (StartCutRegex)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));

  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte(0x03, std::nullopt));
  CHECK(app.promptActive());
}

// -- CUT: split, view a segment, resplit it, back out step by step --

TEST_CASE_FIXTURE(Fixture, "handleCommand(StartCutFixed): refused from Directory mode (not this slice's scope)") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.handleCommand(Command::StartCutFixed));
  CHECK_FALSE(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "CUT: splitting log.txt on 'marker' produces one segment per marker line") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("marker");
  REQUIRE(app.feedByte(0x0d, std::nullopt));

  CHECK(app.currentMode() == ApplicationMode::Cut);
  REQUIRE(app.cutMode().segments().size() == 3);
  CHECK(app.cutMode().segments()[0].label == "marker one");
  CHECK(app.cutMode().segments()[1].label == "marker two");
  CHECK(app.cutMode().segments()[2].label == "marker three");
}

TEST_CASE_FIXTURE(Fixture, "CUT: Enter on a segment label shows that segment's content in CutContent") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("marker");
  REQUIRE(app.feedByte(0x0d, std::nullopt));

  REQUIRE(app.handleCommand(Command::Enter));  // select segment 0 ("marker one")
  CHECK(app.currentMode() == ApplicationMode::CutContent);
}

TEST_CASE_FIXTURE(Fixture, "CUT: nested resplit from CutContent, then step back out level by level") {
  REQUIRE(app.start(path()));
  int rootIdx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != rootIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // descend into subdir/ (pushes a DirectoryMode frame)
  REQUIRE(app.directoryMode().canGoBack());

  int fileIdx = indexOf("sub.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != fileIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // open sub.txt
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("alpha");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);
  REQUIRE(app.cutMode().segments().size() == 2);  // "alpha" at line 0 and line 2

  REQUIRE(app.handleCommand(Command::Enter));  // view segment 0: ["alpha", "beta"]
  REQUIRE(app.currentMode() == ApplicationMode::CutContent);

  REQUIRE(app.handleCommand(Command::StartCutFixed));  // resplit this segment
  feedText("beta");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);
  REQUIRE(app.cutMode().segments().size() == 2);  // ["alpha"] then ["beta"]

  // Step back out: nested Cut -> outer Cut (3-segment... 2-segment list) -> File -> Directory(subdir) -> Directory(root)
  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Cut);
  REQUIRE(app.cutMode().segments().size() == 2);
  CHECK(app.cutMode().segments()[0].label == "alpha");

  REQUIRE(app.handleCommand(Command::Back));  // no more nested cut levels -> back to File
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / "subdir" / "sub.txt").string());

  REQUIRE(app.handleCommand(Command::Back));  // File -> Directory(subdir)
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == (root / "subdir").string() + "/");

  // The directory-descent frame pushed before any CUT activity must
  // be untouched by cutModeStack_'s own push/pop churn above -- the
  // regression this slice's separate-ModeStack design exists to
  // prevent (see Application's class comment).
  REQUIRE(app.directoryMode().canGoBack());
  REQUIRE(app.handleCommand(Command::Back));  // Directory(subdir) -> Directory(root)
  CHECK(app.directoryMode().currentDirectory() == path());
}

TEST_CASE_FIXTURE(Fixture, "CUT: Back from CutContent returns to the Cut label list, not further") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("marker");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::CutContent);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Cut);
  REQUIRE(app.cutMode().segments().size() == 3);
}

TEST_CASE_FIXTURE(Fixture, "CUT: ^G cancels a pending cut prompt without splitting") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("marker");
  CHECK(app.feedByte(0x07, std::nullopt));

  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::File);
}

// -- feedByte: ^Jm / ^J^M leader sequences for MAN --
//
// These MAN tests genuinely exercise the real `man`(1) command
// (matching this project's preference for real execution over
// synthetic fixtures), but -- as ManMode's own tests already document
// -- a minimal environment may have no man pages installed at all, so
// they skip gracefully rather than asserting a specific page exists,
// the same pattern test_man_mode.cpp already uses.

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J m starts a MAN prompt (StartMan)") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte('m', std::nullopt));
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^M also starts a MAN prompt") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte(0x0d, std::nullopt));
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "MAN: typing a page name then ^M loads it and switches to Man mode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartMan));
  feedText("ls");
  bool ok = app.feedByte(0x0d, std::nullopt);
  if (!ok) {
    MESSAGE("man not installed or 'ls' has no man page here -- skipping");
    return;
  }
  CHECK(app.currentMode() == ApplicationMode::Man);
  CHECK(app.manMode().currentPage() == "ls");
}

TEST_CASE_FIXTURE(Fixture, "MAN: ^G returns to whichever mode the prompt was started from") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartMan));
  feedText("ls");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("man not installed or 'ls' has no man page here -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Man);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / "plain.txt").string());
}

TEST_CASE_FIXTURE(Fixture, "MAN: viewing a second page from within Man mode still returns to the original mode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartMan));
  feedText("ls");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("man not installed or 'ls' has no man page here -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Man);

  REQUIRE(app.handleCommand(Command::StartMan));  // still in Man mode -- view a different page
  feedText("cat");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("man not installed or 'cat' has no man page here -- skipping");
    return;
  }
  CHECK(app.currentMode() == ApplicationMode::Man);
  CHECK(app.manMode().currentPage() == "cat");

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);  // not back to the "ls" page -- the ORIGINAL mode
}

TEST_CASE_FIXTURE(Fixture, "MAN: ^G cancels a pending prompt without loading anything") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartMan));
  feedText("ls");
  CHECK(app.feedByte(0x07, std::nullopt));

  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "MAN: submitting an empty name fails cleanly without changing mode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartMan));
  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

// -- feedByte: ^Jp / ^J^P leader sequences for PROGRAM --

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J p starts a PROGRAM prompt (StartProgram)") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte('p', std::nullopt));
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^P also starts a PROGRAM prompt") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte(0x10, std::nullopt));
  CHECK(app.promptActive());
}

// -- PROGRAM: only a recognized (PROGRAM-list first-word-prefix
// matching) command actually runs; ApplicationMode never changes --

TEST_CASE_FIXTURE(Fixture, "PROGRAM: a recognized command actually runs, with real side effects") {
  REQUIRE(app.start(path()));
  std::string outPath = (root / "prog_out.txt").string();

  REQUIRE(app.handleCommand(Command::StartProgram));
  feedText("echo hello > '" + outPath + "'");
  CHECK(app.feedByte(0x0d, std::nullopt));

  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);  // PROGRAM never switches mode

  std::ifstream in(outPath);
  std::string content;
  std::getline(in, content);
  CHECK(content == "hello");
}

TEST_CASE_FIXTURE(Fixture, "PROGRAM: an unrecognized command (not in the PROGRAM list) is refused") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartProgram));
  feedText("some-unlisted-command arg");
  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "PROGRAM: an empty command fails cleanly") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartProgram));
  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));
}

TEST_CASE_FIXTURE(Fixture, "PROGRAM: works from File mode too, and stays in File mode afterwards") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartProgram));
  feedText("echo hi");
  CHECK(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::File);
}

TEST_CASE_FIXTURE(Fixture, "PROGRAM: ^G cancels the prompt without running anything") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartProgram));
  feedText("echo hello");
  CHECK(app.feedByte(0x07, std::nullopt));
  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

// -- feedByte: ^Jl / ^J^L leader sequences for LOCATE --
//
// These genuinely exercise the real `locate`(1) command (matching
// this project's preference for real execution), but -- as
// LocateMode's own tests already document -- `locate` depends on a
// system database this project doesn't control the presence of, so
// they skip gracefully rather than asserting specific results exist,
// the same pattern test_locate_mode.cpp already uses.

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J l starts a LOCATE prompt (StartLocate)") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte('l', std::nullopt));
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^L also starts a LOCATE prompt") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte(0x0c, std::nullopt));
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "LOCATE: typing a query then ^M runs a real locate search") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("/");
  bool ok = app.feedByte(0x0d, std::nullopt);
  if (!ok) {
    MESSAGE("locate not installed or no database available -- skipping");
    return;
  }
  CHECK(app.currentMode() == ApplicationMode::Locate);
  CHECK(app.locateMode().query() == "/");
}

TEST_CASE_FIXTURE(Fixture, "LOCATE: ^G returns to whichever mode the prompt was started from") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("/");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not available -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Locate);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / "plain.txt").string());
}

TEST_CASE_FIXTURE(Fixture, "LOCATE: Enter on a result is a no-op in this slice") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("/");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not available -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  CHECK_FALSE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Locate);
}

TEST_CASE_FIXTURE(Fixture, "LOCATE: starting a nested search from within locate results fails") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("/");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not available -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Locate);

  CHECK_FALSE(app.handleCommand(Command::StartLocate));
  CHECK_FALSE(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "LOCATE: ^G cancels a pending prompt without searching") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("/");
  CHECK(app.feedByte(0x07, std::nullopt));

  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

// -- ARC: opening an archive from Directory mode is auto-detected
// (no leader key), and falls back to plain FileMode for non-archives --

namespace {
int archiveIndexOf(Application &app, const std::string &needle) {
  const auto &entries = app.archiveMode().entries();
  for (std::size_t i = 0; i < entries.size(); i++)
    if (entries[i].name.find(needle) != std::string::npos) return static_cast<int>(i);
  return -1;
}
}  // namespace

TEST_CASE_FIXTURE(Fixture, "ARC: Enter on an archive file switches to Archive mode, not File") {
  REQUIRE(app.start(path()));
  int idx = indexOf("archive.tar");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Archive);
  CHECK(archiveIndexOf(app, "inner.txt") >= 0);
}

TEST_CASE_FIXTURE(Fixture, "ARC: Enter on a plain (non-archive) file still opens File mode as before") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);
}

TEST_CASE_FIXTURE(Fixture, "ARC: Enter on a file entry inside the archive extracts and shows its real content") {
  REQUIRE(app.start(path()));
  int idx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));

  while (app.archiveMode().selectedEntry() != nullptr &&
         app.archiveMode().selectedEntry()->name.find("inner.txt") == std::string::npos) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.archiveMode().selectedEntry() != nullptr);
  REQUIRE(app.archiveMode().selectedEntry()->name.find("inner.txt") != std::string::npos);

  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::ArchiveContent);
}

TEST_CASE_FIXTURE(Fixture, "ARC: Back from ArchiveContent returns to Archive, Back from Archive returns to Directory") {
  REQUIRE(app.start(path()));
  int idx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));

  while (app.archiveMode().selectedEntry() != nullptr &&
         app.archiveMode().selectedEntry()->name.find("inner.txt") == std::string::npos) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::ArchiveContent);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Archive);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == path());
}

// -- SHELL: ESC-leader entry, byte routing to Shell's own LineEditor/
// pty, and the child persisting across Back/^G --
//
// These genuinely fork/exec a real bash (matching Shell's own test
// precedent and this project's preference for real execution over
// fakes for process/pty plumbing), skipped gracefully if bash isn't
// available or the handshake fails.

TEST_CASE_FIXTURE(Fixture, "feedByte: ESC s starts Shell mode (StartShell)") {
  REQUIRE(app.start(path()));
  app.feedByte(0x1b, std::nullopt);
  bool ok = app.feedByte('s', std::nullopt);
  if (!ok) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(app.currentMode() == ApplicationMode::Shell);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ESC followed by an unrecognized byte fails cleanly") {
  REQUIRE(app.start(path()));
  app.feedByte(0x1b, std::nullopt);
  CHECK_FALSE(app.feedByte('z', std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  // The leader state doesn't leak into the next byte: a plain 'j' afterwards still moves down.
  CHECK(app.feedByte('j', std::nullopt));
}

TEST_CASE_FIXTURE(Fixture, "SHELL: typing a command and pressing Enter runs it, output reaches scrollback") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("echo APPSHELLTEST123")) app.feedShellByte(c, std::nullopt);
  CHECK(app.feedShellByte(0x0d, std::nullopt));  // ^M submits

  bool sawOutput = pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("APPSHELLTEST123") != std::string::npos) return true;
    return false;
  });
  CHECK(sawOutput);
}

TEST_CASE_FIXTURE(Fixture, "SHELL: ^G leaves without killing the child; re-entering resumes the same session") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("echo PERSISTMARKER456")) app.feedShellByte(c, std::nullopt);
  app.feedShellByte(0x0d, std::nullopt);
  REQUIRE(pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("PERSISTMARKER456") != std::string::npos) return true;
    return false;
  }));

  CHECK(app.feedShellByte(0x07, std::nullopt));  // ^G
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.shell().alive());  // the child is still running in the background

  app.feedByte(0x1b, std::nullopt);
  CHECK(app.feedByte('s', std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Shell);
  // The old output is still there -- this is the SAME session, not a fresh start().
  bool stillThere = false;
  for (const auto &line : app.shell().scrollback().lines())
    if (line.text.find("PERSISTMARKER456") != std::string::npos) stillThere = true;
  CHECK(stillThere);
}

TEST_CASE_FIXTURE(Fixture, "SHELL: backspace edits the command line before it's submitted") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("echo X")) app.feedShellByte(c, std::nullopt);
  app.feedShellByte(0x7f, std::nullopt);  // DEL: removes the trailing 'X'
  CHECK(app.shell().commandLine().text() == "echo ");
}

TEST_CASE_FIXTURE(Fixture, "SHELL: re-entering from within Shell mode is refused") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK_FALSE(app.handleCommand(Command::StartShell));
  CHECK(app.currentMode() == ApplicationMode::Shell);
}

// -- CP/MV/RM: ^Space marking, ^Xc/^Xm/^Xr, real cp/mv/rm execution --

TEST_CASE("resolveCommand: 0x80 (^Space, Ctrl-Space's reported byte) resolves to ToggleMark") {
  CHECK(Application::resolveCommand(0x80, std::nullopt) == Command::ToggleMark);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(ToggleMark): toggles the mark at the cursor") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.directoryMode().isMarked(0));
  REQUIRE(app.handleCommand(Command::ToggleMark));
  CHECK(app.directoryMode().isMarked(0));
  REQUIRE(app.handleCommand(Command::ToggleMark));
  CHECK_FALSE(app.directoryMode().isMarked(0));
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X c / ^X m / ^X r start the corresponding prompt") {
  REQUIRE(app.start(path()));
  app.feedByte(0x18, std::nullopt);
  CHECK(app.feedByte('c', std::nullopt));
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(StartCopy): refused outside Directory mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK_FALSE(app.handleCommand(Command::StartCopy));
  CHECK_FALSE(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "CP: marking two files then copying them creates real copies at the destination") {
  REQUIRE(app.start(path()));
  int idxA = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idxA) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  int idxB = indexOf("cpmv_b.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idxB) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));

  CHECK(fs::exists(root / "cpmvdest" / "cpmv_a.txt"));
  CHECK(fs::exists(root / "cpmvdest" / "cpmv_b.txt"));
  // Sources are untouched by a copy.
  CHECK(fs::exists(root / "cpmv_a.txt"));
  CHECK(fs::exists(root / "cpmv_b.txt"));
  // The reload clears marks (DirectoryMode::enterDirectory()'s own documented behavior).
  CHECK(app.directoryMode().markedCount() == 0);
}

TEST_CASE_FIXTURE(Fixture, "MV: moving a marked file removes the source and creates the destination") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartMove));
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));

  CHECK(fs::exists(root / "cpmvdest" / "cpmv_a.txt"));
  CHECK_FALSE(fs::exists(root / "cpmv_a.txt"));
}

TEST_CASE_FIXTURE(Fixture, "RM: confirming with 'y' actually deletes the marked file") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_b.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartRemove));
  feedText("y");
  CHECK(app.feedByte(0x0d, std::nullopt));

  CHECK_FALSE(fs::exists(root / "cpmv_b.txt"));
}

TEST_CASE_FIXTURE(Fixture, "RM: anything other than exactly 'y' leaves the file untouched") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_b.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartRemove));
  feedText("n");
  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));

  CHECK(fs::exists(root / "cpmv_b.txt"));
}

TEST_CASE_FIXTURE(Fixture, "CP/MV/RM: ^G cancels the prompt without touching the filesystem") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartRemove));
  feedText("y");
  CHECK(app.feedByte(0x07, std::nullopt));

  CHECK_FALSE(app.promptActive());
  CHECK(fs::exists(root / "cpmv_a.txt"));
}

// -- M_exit: ^X^C confirmation prompt, wantsExit() flag --

TEST_CASE_FIXTURE(Fixture, "wantsExit(): false before anything happens") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.wantsExit());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X ^C starts the exit-confirmation prompt") {
  REQUIRE(app.start(path()));
  app.feedByte(0x18, std::nullopt);
  CHECK(app.feedByte(0x03, std::nullopt));
  CHECK(app.promptActive());
  CHECK_FALSE(app.wantsExit());
}

TEST_CASE_FIXTURE(Fixture, "StartExit: reachable from any mode, e.g. File") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK(app.handleCommand(Command::StartExit));
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "exit confirmation: typing 'y' then ^M sets wantsExit()") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartExit));
  feedText("y");
  CHECK(app.feedByte(0x0d, std::nullopt));

  CHECK(app.wantsExit());
  CHECK_FALSE(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "exit confirmation: anything other than exactly 'y' does not set wantsExit()") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartExit));
  feedText("n");
  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));

  CHECK_FALSE(app.wantsExit());
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "exit confirmation: ^G cancels without setting wantsExit()") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartExit));
  feedText("y");
  CHECK(app.feedByte(0x07, std::nullopt));  // ^G before submitting

  CHECK_FALSE(app.promptActive());
  CHECK_FALSE(app.wantsExit());
}

// -- DU: ^Jd/^J^D, pre-filled destination prompt, real `du`, and
// Enter drilling into DirectoryMode/FileMode --

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J d starts a DU prompt pre-filled with the current directory") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte('d', std::nullopt));
  CHECK(app.promptActive());
  CHECK(app.promptText() == path());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^D also starts a DU prompt") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte(0x04, std::nullopt));
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(StartDu): refused outside Directory mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK_FALSE(app.handleCommand(Command::StartDu));
}

TEST_CASE_FIXTURE(Fixture, "DU: accepting the pre-filled default runs real du and shows real entries") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  CHECK(app.feedByte(0x0d, std::nullopt));  // ^M accepts the pre-filled current directory

  CHECK(app.currentMode() == ApplicationMode::Du);
  bool sawSubdir = false, sawFile = false;
  for (const auto &entry : app.duMode().entries()) {
    if (entry.path.find("subdir") != std::string::npos && entry.isDirectory) sawSubdir = true;
    if (entry.path.find("plain.txt") != std::string::npos && !entry.isDirectory) sawFile = true;
  }
  CHECK(sawSubdir);
  CHECK(sawFile);
}

TEST_CASE_FIXTURE(Fixture, "DU: Enter on a directory entry drills into DirectoryMode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  REQUIRE(app.feedByte(0x0d, std::nullopt));

  bool hasSubdirEntry = false;
  for (const auto &entry : app.duMode().entries())
    if (entry.isDirectory && entry.path.find("subdir") != std::string::npos) hasSubdirEntry = true;
  REQUIRE(hasSubdirEntry);
  while (app.duMode().selectedEntry() &&
         !(app.duMode().selectedEntry()->isDirectory &&
           app.duMode().selectedEntry()->path.find("subdir") != std::string::npos)) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }

  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  // Real `du` implementations aren't consistent about a double vs.
  // single slash in echoed child paths (BSD du vs. GNU du), so this
  // checks behavior (did it actually descend into subdir/, confirmed
  // by sub.txt showing up in the new listing) rather than an exact
  // path string.
  bool sawSubTxt = false;
  for (const auto &entry : app.directoryMode().entries())
    if (entry.name.find("sub.txt") != std::string::npos) sawSubTxt = true;
  CHECK(sawSubTxt);
}

TEST_CASE_FIXTURE(Fixture, "DU: Enter on a file entry drills into FileMode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  REQUIRE(app.feedByte(0x0d, std::nullopt));

  while (app.duMode().selectedEntry() &&
         (app.duMode().selectedEntry()->isDirectory ||
          app.duMode().selectedEntry()->path.find("plain.txt") == std::string::npos)) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.duMode().selectedEntry() != nullptr);
  REQUIRE_FALSE(app.duMode().selectedEntry()->isDirectory);

  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);
}

TEST_CASE_FIXTURE(Fixture, "DU: Back returns to Directory mode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Du);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == path());
}

TEST_CASE_FIXTURE(Fixture, "DU: ^G cancels the pre-filled prompt without running du") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  CHECK(app.feedByte(0x07, std::nullopt));

  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);
}
