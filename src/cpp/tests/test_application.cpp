#include <algorithm>
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
using fm::RingCategory;
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
  // homeDirectory (the trailing arg) is `root` itself -- root is
  // declared/initialized before app (member order), so this is valid;
  // makes wFilePath_ (K-17/K-64's write-buffer target) land inside
  // this fixture's own disposable temp dir instead of a real $HOME.
  Application app{
      display, keyInput, history, configuration, Charset::Encoding::Ascii, 80, testShellOptions(), root.string()};

  Fixture() : root(makeTempDir()) {
    fs::create_directory(root / ".usilic");  // wFilePath_'s own parent dir, see app{}'s own comment above
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

    // Nested archives (an archive inside an archive inside an
    // archive, to also exercise more than one level of descent) for
    // the nested-ARC descendToNestedArchive() tests. Built bottom-up:
    // innermost.txt -> level2.tar -> level1.tar -> outer.tar.
    fs::create_directory(root / "nestsrc");
    fs::create_directory(root / "nestsrc2");
    { std::ofstream(root / "nestsrc" / "innermost.txt") << "deepest content\n"; }
    std::string nestCmd = "cd '" + root.string() +
                           "' && tar cf nestsrc/level2.tar -C nestsrc innermost.txt"
                           " && rm nestsrc/innermost.txt"
                           " && tar cf nestsrc2/level1.tar -C nestsrc level2.tar"
                           " && tar cf outer.tar -C nestsrc2 level1.tar";
    REQUIRE(std::system(nestCmd.c_str()) == 0);

    // Real files for the CP/MV/RM-related tests.
    { std::ofstream(root / "cpmv_a.txt") << "content-a\n"; }
    { std::ofstream(root / "cpmv_b.txt") << "content-b\n"; }
    fs::create_directory(root / "cpmvdest");

    // Destination for the K-57/K-58 (ARC extraction) tests.
    fs::create_directory(root / "arcdest");
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

TEST_CASE("resolveCommand: plain bytes -- j/k, ^N/^P, ^M, ^G, space/b") {
  // j/k and ^N/^P are DELIBERATELY different commands, not synonyms
  // (2026-07-31 correction, see resolveCommand()'s own comment):
  // usilicrc documents 'j'/'k' as "move window one line" (K-8/K-9)
  // and ^N/^P as "move cursor one line" (K-10/K-11).
  CHECK(Application::resolveCommand('j', std::nullopt) == Command::ScrollLineDown);
  CHECK(Application::resolveCommand('k', std::nullopt) == Command::ScrollLineUp);
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

TEST_CASE("resolveCommand: ^F/^B are usilicrc's own aliases for space/b (M_forw2/M_back2)") {
  CHECK(Application::resolveCommand(0x06, std::nullopt) == Command::PageDown);  // ^F
  CHECK(Application::resolveCommand(0x02, std::nullopt) == Command::PageUp);    // ^B
}

TEST_CASE("resolveCommand: ^L resolves to Redraw, 'q' resolves to QuitPager") {
  CHECK(Application::resolveCommand(0x0c, std::nullopt) == Command::Redraw);
  CHECK(Application::resolveCommand('q', std::nullopt) == Command::QuitPager);
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
  // The leader state doesn't leak into the next byte: a plain 'j' afterwards is still handled normally.
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

TEST_CASE_FIXTURE(Fixture, "CUT (regular file, K-35/^Jb): splits the SELECTED entry directly, without opening it first") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.currentMode() == ApplicationMode::Directory);  // never entered File mode

  REQUIRE(app.handleCommand(Command::StartCutFileFixed));
  feedText("marker");
  REQUIRE(app.feedByte(0x0d, std::nullopt));

  CHECK(app.currentMode() == ApplicationMode::Cut);
  REQUIRE(app.cutMode().segments().size() == 3);
  CHECK(app.cutMode().segments()[0].label == "marker one");
  CHECK(app.fileMode().currentFile() == (root / "log.txt").string());  // loaded behind the scenes
}

TEST_CASE_FIXTURE(Fixture, "CUT (regular file): Back returns to Directory mode, matching where it was entered from") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::StartCutFileFixed));
  feedText("marker");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(StartCutFileFixed): refused on a directory entry or outside Directory mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  CHECK_FALSE(app.handleCommand(Command::StartCutFileFixed));  // selected entry is a directory
  CHECK_FALSE(app.promptActive());
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

// -- ARC nested-archive descent (docs/ideas/IDEAS.md's own original
// motivating example for this whole C++ port -- see
// Application::descendToNestedArchive()'s own comment) --

TEST_CASE_FIXTURE(Fixture, "ARC nested: Enter on an archive entry inside an archive descends into IT instead of showing raw bytes") {
  REQUIRE(app.start(path()));
  int idx = indexOf("outer.tar");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Archive);
  REQUIRE(archiveIndexOf(app, "level1.tar") >= 0);

  while (app.archiveMode().selectedEntry() != nullptr &&
         app.archiveMode().selectedEntry()->name.find("level1.tar") == std::string::npos) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Archive);  // still Archive, not ArchiveContent -- it descended
  CHECK(archiveIndexOf(app, "level2.tar") >= 0);          // now listing level1.tar's OWN contents
}

TEST_CASE_FIXTURE(Fixture, "ARC nested: descending three levels deep still reaches the innermost real file's content") {
  REQUIRE(app.start(path()));
  int idx = indexOf("outer.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // into outer.tar

  auto descendTo = [&](const std::string &needle) {
    while (app.archiveMode().selectedEntry() != nullptr &&
           app.archiveMode().selectedEntry()->name.find(needle) == std::string::npos) {
      if (!app.handleCommand(Command::MoveDown)) break;
    }
    REQUIRE(app.handleCommand(Command::Enter));
  };
  descendTo("level1.tar");  // into level1.tar
  descendTo("level2.tar");  // into level2.tar
  REQUIRE(app.currentMode() == ApplicationMode::Archive);
  REQUIRE(archiveIndexOf(app, "innermost.txt") >= 0);

  descendTo("innermost.txt");  // a real (non-archive) file -- extracts to ArchiveContent as usual
  REQUIRE(app.currentMode() == ApplicationMode::ArchiveContent);
  REQUIRE(!app.activeContentView().lines().empty());
  CHECK(app.activeContentView().lines()[0].text == "deepest content");
}

TEST_CASE_FIXTURE(Fixture, "ARC nested: Back climbs the nested chain one level at a time before finally reaching Directory") {
  REQUIRE(app.start(path()));
  int idx = indexOf("outer.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // outer.tar's listing

  while (app.archiveMode().selectedEntry() != nullptr &&
         app.archiveMode().selectedEntry()->name.find("level1.tar") == std::string::npos) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.handleCommand(Command::Enter));  // now inside level1.tar's listing
  REQUIRE(app.archiveMode().currentArchive().find("outer.tar") == std::string::npos);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Archive);
  CHECK(archiveIndexOf(app, "level1.tar") >= 0);  // back to outer.tar's own listing

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);  // outermost -- ordinary Back-to-Directory, unchanged
  CHECK(app.directoryMode().currentDirectory() == path());
}

TEST_CASE_FIXTURE(Fixture, "ARC nested: opening a fresh top-level archive clears any earlier nested-descent chain") {
  REQUIRE(app.start(path()));
  int outerIdx = indexOf("outer.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != outerIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  while (app.archiveMode().selectedEntry() != nullptr &&
         app.archiveMode().selectedEntry()->name.find("level1.tar") == std::string::npos) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.handleCommand(Command::Enter));  // now 1 level deep inside outer.tar
  REQUIRE(app.handleCommand(Command::Back));   // pop the nested frame
  REQUIRE(app.handleCommand(Command::Back));   // pop the outer.tar frame -- back to a clean Directory-mode start

  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  int plainIdx = indexOf("archive.tar");
  REQUIRE(plainIdx >= 0);
  // The cursor is still wherever it was left before the archive
  // excursion (on outer.tar, AFTER archive.tar in listing order) --
  // returnFromArchive()'s Directory fallback doesn't reset it (see
  // its own comment), and MoveDown only goes forward, so the search
  // needs to start from the top rather than assume archive.tar is
  // reachable by moving down from here.
  REQUIRE(app.handleCommand(Command::GoToTop));
  while (indexOf(app.directoryMode().selectedEntry()->name) != plainIdx) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.directoryMode().selectedEntry()->name == "archive.tar");
  REQUIRE(app.handleCommand(Command::Enter));  // a DIFFERENT, non-nested top-level archive
  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);  // one Back reaches Directory directly -- no stale chain
}

// -- ARC extraction to a real destination (K-57/K-58, ^X^Ka/^Xa) --

TEST_CASE_FIXTURE(Fixture, "ARC extract (^Xa, K-58, Directory mode): extracts the WHOLE archive to the destination") {
  REQUIRE(app.start(path()));
  int idx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  CHECK(app.promptActive());
  feedText((root / "arcdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));

  CHECK(fs::exists(root / "arcdest" / "arcsrc" / "inner.txt"));
  CHECK(fs::exists(root / "archive.tar"));  // untouched by extraction
}

TEST_CASE_FIXTURE(Fixture, "ARC extract (^X^Ka, K-57 \"with SHELL window\"): also extracts the whole archive") {
  REQUIRE(app.start(path()));
  int idx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte(0x0b, std::nullopt));  // ^K
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  feedText((root / "arcdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));

  CHECK(fs::exists(root / "arcdest" / "arcsrc" / "inner.txt"));
}

TEST_CASE_FIXTURE(Fixture, "ARC extract from within Archive mode's own TOC: marking a specific entry extracts only it") {
  REQUIRE(app.start(path()));
  int idx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Archive);

  while (app.archiveMode().selectedEntry() != nullptr &&
         app.archiveMode().selectedEntry()->name.find("inner.txt") == std::string::npos) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.archiveMode().selectedEntry() != nullptr);
  REQUIRE(app.archiveMode().selectedEntry()->name.find("inner.txt") != std::string::npos);
  REQUIRE(app.handleCommand(Command::ToggleMark));  // ^Space -- this port's own marking key (see ArchiveMode::toggleMark())

  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  feedText((root / "arcdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));

  CHECK(fs::exists(root / "arcdest" / "arcsrc" / "inner.txt"));
  CHECK(app.currentMode() == ApplicationMode::Archive);  // extraction doesn't leave the TOC screen
}

TEST_CASE_FIXTURE(Fixture, "ARC extract: refused outside Directory/Archive mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK_FALSE(app.handleCommand(Command::StartArchiveExtract));
  CHECK_FALSE(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "ARC extract: refused in Directory mode when the selection isn't a recognized archive") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  CHECK_FALSE(app.handleCommand(Command::StartArchiveExtract));
  CHECK_FALSE(app.promptActive());
}

// -- ERROR/LOG buffers (K-67/K-68, ^X^Ke/^X^Kl) --

TEST_CASE_FIXTURE(Fixture, "ERROR (^X^Ke, K-67): refused when nothing has been logged this session yet") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.handleCommand(Command::ShowErrorLog));
}

TEST_CASE_FIXTURE(Fixture, "ERROR (^X^Ke, K-67): a failed CP's stderr lands in errorLogPath_, viewable via File mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte('c', std::nullopt));   // c -- non-interactive CP (K-52)
  // A destination whose PARENT doesn't exist either -- cp fails and
  // writes a diagnostic to stderr (unlike a merely-missing leaf name,
  // which cp happily creates).
  feedText((root / "no_such_parent" / "dest").string());
  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));  // cp fails

  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte(0x0b, std::nullopt));  // ^K
  CHECK(app.feedByte('e', std::nullopt));     // e
  CHECK(app.currentMode() == ApplicationMode::File);
  REQUIRE(!app.activeContentView().lines().empty());
}

TEST_CASE_FIXTURE(Fixture, "LOG (^X^Kl, K-68): refused when Shell was never started") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.handleCommand(Command::ShowShellLog));
}

TEST_CASE_FIXTURE(Fixture, "TCB banner (^T, K-0): shows a status screen with real usilicrc/history/log paths") {
  REQUIRE(app.start(path()));
  CHECK(app.feedByte(0x14, std::nullopt));  // ^T
  CHECK(app.currentMode() == ApplicationMode::File);

  std::string joined;
  for (const auto &line : app.activeContentView().lines()) joined += line.text + "\n";
  CHECK(joined.find("usilic v1.5b") != std::string::npos);
  CHECK(joined.find((root / ".usilic" / "usilicrc").string()) != std::string::npos);
  CHECK(joined.find((root / ".usilic" / "history").string()) != std::string::npos);
  CHECK(joined.find((root / ".usilic" / "error").string()) != std::string::npos);
  CHECK(joined.find((root / ".usilic" / "shell").string()) != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^O a (K-27) cycles the charset Ascii -> Euc -> Utf8 -> Ascii") {
  REQUIRE(app.start(path()));
  CHECK(app.encoding() == Charset::Encoding::Ascii);

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  CHECK(app.feedByte('a', std::nullopt));
  CHECK(app.encoding() == Charset::Encoding::Euc);

  REQUIRE(app.feedByte(0x0f, std::nullopt));
  CHECK(app.feedByte('a', std::nullopt));
  CHECK(app.encoding() == Charset::Encoding::Utf8);

  REQUIRE(app.feedByte(0x0f, std::nullopt));
  CHECK(app.feedByte('a', std::nullopt));
  CHECK(app.encoding() == Charset::Encoding::Ascii);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(CycleCharset): has no mode restriction, unlike most other K-? commands") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK(app.handleCommand(Command::CycleCharset));
  CHECK(app.encoding() == Charset::Encoding::Euc);
  // Still showing the same file, un-crashed, after the reflow() this
  // triggers for the currently-open File mode screen.
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / "plain.txt").string());
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

TEST_CASE_FIXTURE(Fixture, "saveBufferToFile (^X^W, K-17): writes the active view's lines to wFilePath_") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  CHECK(app.feedByte(0x17, std::nullopt));    // ^W

  std::ifstream in(root / ".usilic" / "w.usilic");
  REQUIRE(in.good());
  std::string firstLine;
  std::getline(in, firstLine);
  CHECK(firstLine == app.directoryMode().entries()[0].lsLine);
}

TEST_CASE_FIXTURE(Fixture, "shellChangeDirectory (^J., K-61): starts Shell and submits cd '<directory>'") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  bool ok = app.feedByte('.', std::nullopt);
  if (!ok) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(app.currentMode() == ApplicationMode::Shell);
  CHECK(app.shell().commandLine().text().empty());  // submitLine() clears it after sending
}

TEST_CASE_FIXTURE(Fixture, "shellRunLsL (^J,, K-62): starts Shell and submits ls -l") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  bool ok = app.feedByte(',', std::nullopt);
  if (!ok) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(app.currentMode() == ApplicationMode::Shell);
}

TEST_CASE_FIXTURE(Fixture, "shellInsertName (^Jw, K-63): starts Shell and PRE-FILLS the selected entry's name") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  bool ok = app.feedByte('w', std::nullopt);
  if (!ok) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(app.currentMode() == ApplicationMode::Shell);
  CHECK(app.shell().commandLine().text() == "echo plain.txt");  // NOT submitted
}

TEST_CASE_FIXTURE(Fixture, "shellInsertBufferContent (^J^W, K-64): writes the buffer then PRE-FILLS cat <file>") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  bool ok = app.feedByte(0x17, std::nullopt);  // ^W
  if (!ok) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(app.currentMode() == ApplicationMode::Shell);
  std::string wPath = (root / ".usilic" / "w.usilic").string();
  CHECK(app.shell().commandLine().text() == "cat " + wPath);

  std::ifstream in(wPath);
  REQUIRE(in.good());
  std::string firstLine;
  std::getline(in, firstLine);
  CHECK(firstLine.find("./") != std::string::npos);  // the directory listing's own "./" entry line
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ESC followed by an unrecognized byte fails cleanly") {
  REQUIRE(app.start(path()));
  app.feedByte(0x1b, std::nullopt);
  CHECK_FALSE(app.feedByte('z', std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  // The leader state doesn't leak into the next byte: a plain 'j' afterwards is still handled normally.
  CHECK(app.feedByte('j', std::nullopt));
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ESC< / ESC> jump to the first/last line (M_head/M_tail)") {
  REQUIRE(app.start(path()));
  fm::ContentView &view = app.activeContentView();
  view.setCursorIndex(2);

  REQUIRE(app.feedByte(0x1b, std::nullopt));
  CHECK(app.feedByte('>', std::nullopt));
  CHECK(view.cursorIndex() == static_cast<int>(view.lines().size()) - 1);

  REQUIRE(app.feedByte(0x1b, std::nullopt));
  CHECK(app.feedByte('<', std::nullopt));
  CHECK(view.cursorIndex() == 0);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ESC ^M (K-24, \"Read as is\") opens the selected file, same as a plain Enter") {
  // Ported as a bare alias for Command::Enter: legacy's M_code (K-24)
  // and M_fp (K-23, see the next test) both existed to pick between
  // two DIFFERENT internal file-reading implementations (load the
  // whole file into one memory buffer vs. stream it line-by-line via
  // a pipe, plus M_code additionally skipped legacy's own charset
  // auto-detection pass) -- a distinction that doesn't exist in this
  // port's architecture at all: FileMode always reads the whole file
  // and always hands it to the one shared PipeBuffer, uniformly (see
  // FileMode's own class comment). So both keys are wired straight to
  // Command::Enter, giving them real, working behavior instead of
  // silently no-opping as an unrecognized sequence, while the
  // memory-representation distinction itself is simply moot here.
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.feedByte(0x1b, std::nullopt));  // ESC
  CHECK(app.feedByte(0x0d, std::nullopt));    // ^M
  CHECK(app.currentMode() == ApplicationMode::File);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^O ^M (K-23, \"Read per line\") opens the selected file, same as a plain Enter") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  CHECK(app.feedByte(0x0d, std::nullopt));    // ^M
  CHECK(app.currentMode() == ApplicationMode::File);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^L (Redraw) is a safe no-op that doesn't change cursor/mode state") {
  REQUIRE(app.start(path()));
  fm::ContentView &view = app.activeContentView();
  view.setCursorIndex(1);
  CHECK(app.feedByte(0x0c, std::nullopt));
  CHECK(view.cursorIndex() == 1);
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: 'q' (QuitPager) is a no-op in Directory mode, matching usilicrc's pager-only scoping") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.feedByte('q', std::nullopt));
  CHECK_FALSE(app.wantsExit());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^O h toggles Display::smoothScrollEnabled() and persists it to History") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(display.smoothScrollEnabled());
  CHECK_FALSE(history.smoothScrollEnabled());

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  CHECK(app.feedByte('h', std::nullopt));
  CHECK(display.smoothScrollEnabled());
  CHECK(history.smoothScrollEnabled());

  REQUIRE(app.feedByte(0x0f, std::nullopt));
  CHECK(app.feedByte('h', std::nullopt));
  CHECK_FALSE(display.smoothScrollEnabled());
  CHECK_FALSE(history.smoothScrollEnabled());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^O l toggles DirectoryMode::filenameOnlyMode() and persists it to History") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  REQUIRE(idx >= 0);
  CHECK_FALSE(app.directoryMode().filenameOnlyMode());
  CHECK(history.lsMode() == 0);
  CHECK(app.activeContentView().lines()[idx].text != "plain.txt");

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  CHECK(app.feedByte('l', std::nullopt));
  CHECK(app.directoryMode().filenameOnlyMode());
  CHECK(history.lsMode() != 0);
  CHECK(app.activeContentView().lines()[idx].text == "plain.txt");

  REQUIRE(app.feedByte(0x0f, std::nullopt));
  CHECK(app.feedByte('l', std::nullopt));
  CHECK_FALSE(app.directoryMode().filenameOnlyMode());
  CHECK(history.lsMode() == 0);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^O ^K jumps to the parent directory (K-19, M_m)") {
  REQUIRE(app.start(path() + "subdir/"));
  REQUIRE(app.directoryMode().currentDirectory() == path() + "subdir/");

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  CHECK(app.feedByte(0x0b, std::nullopt));    // ^K
  CHECK(app.directoryMode().currentDirectory() == path());
  CHECK(app.directoryMode().canGoBack());  // ^G returns to subdir/, same as descending into it normally would
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToParentDirectory): refused outside Directory mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK_FALSE(app.handleCommand(Command::GoToParentDirectory));
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(ToggleLsMode): refused outside Directory mode (K-26's own scoping)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK_FALSE(app.handleCommand(Command::ToggleLsMode));
}

TEST_CASE_FIXTURE(Fixture, "feedByte: 'q' exits immediately outside Directory mode, without the y/n confirmation") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK(app.feedByte('q', std::nullopt));
  CHECK(app.wantsExit());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X f prompts, and a typed FILE path switches to File mode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte('f', std::nullopt));   // f

  for (char c : (root / "plain.txt").string()) app.feedByte(static_cast<unsigned char>(c), std::nullopt);
  CHECK(app.feedByte(0x0d, std::nullopt));  // ^M submits
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / "plain.txt").string());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X d prompts, and a typed DIR path switches to Directory mode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte('d', std::nullopt));   // d

  for (char c : (root / "subdir").string()) app.feedByte(static_cast<unsigned char>(c), std::nullopt);
  CHECK(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == (root / "subdir").string() + "/");
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X f with a RELATIVE path resolves against the currently browsed directory, "
                           "not the process's own cwd (2026-08-01 user report)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.directoryMode().currentDirectory() == (root / "subdir").string() + "/");

  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte('f', std::nullopt));   // f
  for (char c : std::string("sub.txt")) app.feedByte(static_cast<unsigned char>(c), std::nullopt);  // relative
  CHECK(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / "subdir" / "sub.txt").string());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X f/d with a nonexistent path fails cleanly, staying in the current mode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x18, std::nullopt));
  REQUIRE(app.feedByte('f', std::nullopt));

  for (char c : std::string("/no/such/path/at/all")) app.feedByte(static_cast<unsigned char>(c), std::nullopt);
  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X f prompt, ^I (K-65) completes an unambiguous filename") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte('f', std::nullopt));   // f

  // "plai" only matches plain.txt in root -- an absolute path so
  // completion isn't relative to DirectoryMode's current directory.
  for (char c : (root / "plai").string()) app.feedByte(static_cast<unsigned char>(c), std::nullopt);
  CHECK(app.feedByte(0x09, std::nullopt));  // ^I
  CHECK(app.feedByte(0x0d, std::nullopt));  // ^M submits whatever got completed
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / "plain.txt").string());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X f prompt, ^I (K-65) with an ambiguous prefix leaves the text unchanged") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x18, std::nullopt));
  REQUIRE(app.feedByte('f', std::nullopt));

  // "cpmv_" matches both cpmv_a.txt and cpmv_b.txt -- ambiguous, so
  // nothing should be spliced in (matching Shell::onTab()'s own
  // "single unambiguous match only" contract).
  for (char c : (root / "cpmv_").string()) app.feedByte(static_cast<unsigned char>(c), std::nullopt);
  CHECK(app.feedByte(0x09, std::nullopt));
  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));  // still an incomplete path -- doesn't resolve to a real file
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ESC s, real Shell Tab-completion resolves a real filesystem match (K-65)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x1b, std::nullopt));  // ESC
  bool ok = app.feedByte('s', std::nullopt);  // s
  if (!ok) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  // Force a known cwd so completion has a deterministic base directory,
  // rather than depending on the already-tested ^J. cd wiring's timing.
  // Compared without a trailing slash: bash's own \w PS1 expansion
  // (Shell::buildSetupCommand()) reports getcwd()'s own output, which
  // -- unlike DirectoryMode::currentDirectory()'s own convention --
  // never carries one except for "/" itself.
  app.shell().commandLine().setText("cd '" + root.string() + "'");
  app.shell().submitLine();
  REQUIRE(pumpUntil(app, [&] { return app.shell().currentDirectory() == root.string(); }));

  app.shell().commandLine().setText("cat plai");
  app.shell().onTab();
  CHECK(app.shell().commandLine().text() == "cat plain.txt");
}

// runGuiPhase()'s w/s inertial-scroll trigger (see CLAUDE.md's
// 2026-07-31 note) checks this before hijacking a plain 'w'/'s' byte,
// so ESC s (StartShell) and typing "w"/"s" into a prompt still work.
TEST_CASE_FIXTURE(Fixture, "awaitingSequenceOrPromptByte: false in plain top-level navigation") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.awaitingSequenceOrPromptByte());
}

TEST_CASE_FIXTURE(Fixture, "awaitingSequenceOrPromptByte: true right after ESC, until the next byte resolves it") {
  REQUIRE(app.start(path()));
  app.feedByte(0x1b, std::nullopt);
  CHECK(app.awaitingSequenceOrPromptByte());
  app.feedByte('z', std::nullopt);  // unrecognized -- resolves (fails) the pending ESC either way
  CHECK_FALSE(app.awaitingSequenceOrPromptByte());
}

TEST_CASE_FIXTURE(Fixture, "awaitingSequenceOrPromptByte: true right after the ^J leader byte") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);  // ^J
  CHECK(app.awaitingSequenceOrPromptByte());
}

TEST_CASE_FIXTURE(Fixture, "awaitingSequenceOrPromptByte: true while an FGREP prompt is active") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  CHECK(app.awaitingSequenceOrPromptByte());
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

TEST_CASE_FIXTURE(Fixture, "SHELL: a real submitted command is actually recorded into RingCategory::ShellCommand "
                           "(2026-08-02: setHistoryRecorder() was declared but never wired to anything)") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("echo RECORDTEST789")) app.feedShellByte(c, std::nullopt);
  app.feedShellByte(0x0d, std::nullopt);

  const auto &entries = history.recentEntries(RingCategory::ShellCommand);
  bool recorded = std::find(entries.begin(), entries.end(), "echo RECORDTEST789") != entries.end();
  CHECK(recorded);
}

TEST_CASE_FIXTURE(Fixture, "SHELL leader keys: ^P/^N move scrollback_'s own cursor, matching legacy's unified "
                           "buffer model (2026-08-02(続4): replaces an earlier RingCategory::ShellCommand-backed "
                           "index disconnected from what was actually on screen -- see CLAUDE.md)") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("echo MARKERPN001")) app.feedShellByte(c, std::nullopt);
  app.feedShellByte(0x0d, std::nullopt);
  // !line.shellCommand: the frozen prompt line ITSELF already contains
  // "MARKERPN001" as part of its own attached command text -- wait
  // specifically for the shell's REAL echoed output line, so there are
  // at least two lines in scrollback_ (frozen prompt + its output) for
  // ^P to actually move between.
  REQUIRE(pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (!line.shellCommand && line.text.find("MARKERPN001") != std::string::npos) return true;
    return false;
  }));

  int bottom = app.shell().scrollback().cursorIndex();
  CHECK(app.feedShellByte(0x10, std::nullopt));  // ^P
  CHECK(app.shell().scrollback().cursorIndex() == bottom - 1);
  CHECK(app.currentMode() == ApplicationMode::Shell);  // navigating history does NOT leave Shell mode

  CHECK(app.feedShellByte(0x0e, std::nullopt));  // ^N
  CHECK(app.shell().scrollback().cursorIndex() == bottom);
}

TEST_CASE_FIXTURE(Fixture, "SHELL: Enter on a past prompt line resubmits its command, matching legacy shell.c's "
                           "own '\\r' case (the user's own words: \"^Pでそこへ戻ってEnterでlsが再実行される\")") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("echo MARKERRERUN002")) app.feedShellByte(c, std::nullopt);
  app.feedShellByte(0x0d, std::nullopt);
  REQUIRE(pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("MARKERRERUN002") != std::string::npos) return true;
    return false;
  }));

  int promptIndex = -1;
  {
    const auto &lines = app.shell().scrollback().lines();
    for (std::size_t i = 0; i < lines.size(); i++)
      if (lines[i].shellCommand && *lines[i].shellCommand == "echo MARKERRERUN002")
        promptIndex = static_cast<int>(i);
  }
  REQUIRE(promptIndex >= 0);

  int occurrencesBefore = 0;
  for (const auto &line : app.shell().scrollback().lines())
    if (line.text.find("MARKERRERUN002") != std::string::npos) occurrencesBefore++;

  app.shell().scrollback().setCursorIndex(promptIndex);  // as if reached via repeated ^P
  CHECK(app.feedShellByte(0x0d, std::nullopt));           // Enter while browsing: resubmits
  CHECK(app.shell().scrollback().cursorIndex() ==
        static_cast<int>(app.shell().scrollback().lines().size()) - 1);  // snapped back to the bottom

  REQUIRE(pumpUntil(app, [&] {
    int count = 0;
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("MARKERRERUN002") != std::string::npos) count++;
    return count > occurrencesBefore;
  }));
}

TEST_CASE_FIXTURE(Fixture, "SHELL: Enter on a plain output line (no attached command) just snaps back to the "
                           "live prompt instead of submitting raw output text as a command") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("echo MARKERPLAIN003")) app.feedShellByte(c, std::nullopt);
  app.feedShellByte(0x0d, std::nullopt);
  REQUIRE(pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("MARKERPLAIN003") != std::string::npos && !line.shellCommand) return true;
    return false;
  }));

  int plainIndex = -1;
  {
    const auto &lines = app.shell().scrollback().lines();
    for (std::size_t i = 0; i < lines.size(); i++)
      if (!lines[i].shellCommand && lines[i].text.find("MARKERPLAIN003") != std::string::npos)
        plainIndex = static_cast<int>(i);
  }
  REQUIRE(plainIndex >= 0);

  app.shell().scrollback().setCursorIndex(plainIndex);
  app.shell().commandLine().setText("");
  CHECK(app.feedShellByte(0x0d, std::nullopt));
  CHECK(app.shell().scrollback().cursorIndex() ==
        static_cast<int>(app.shell().scrollback().lines().size()) - 1);
  CHECK(app.shell().commandLine().text().empty());  // did NOT get filled with the output text
}

TEST_CASE_FIXTURE(Fixture, "SHELL: typing while browsing history snaps the cursor back to the live prompt "
                           "first, matching legacy shell.c's own default case") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("echo MARKERTYPE004")) app.feedShellByte(c, std::nullopt);
  app.feedShellByte(0x0d, std::nullopt);
  // !line.shellCommand: wait for the shell's REAL echoed output line
  // specifically (see the ^P/^N test's own comment above) so ^P below
  // has somewhere to actually move to.
  REQUIRE(pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (!line.shellCommand && line.text.find("MARKERTYPE004") != std::string::npos) return true;
    return false;
  }));

  app.feedShellByte(0x10, std::nullopt);  // ^P: now browsing, not at the bottom
  int bottom = static_cast<int>(app.shell().scrollback().lines().size()) - 1;
  REQUIRE(app.shell().scrollback().cursorIndex() != bottom);

  app.shell().commandLine().setText("");
  CHECK(app.feedShellByte('x', std::nullopt));
  CHECK(app.shell().scrollback().cursorIndex() == bottom);  // snapped back
  CHECK(app.shell().commandLine().text() == "x");
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

// -- Global leader-key access from within Shell mode (^J/^X/^O, NOT
// bare ESC) -- a real user report (2026-08-02) comparing side by side
// against the legacy build found ^X^F/^X^C etc. work directly from
// within SHELL there, silently doing nothing here before this existed
// (currentMode_==Shell routed every byte to feedShellByte() with no
// leader-key access at all). See feedShellByte()'s own comment. --

TEST_CASE_FIXTURE(Fixture, "SHELL leader keys: ^X^F (FILE history) switches out of Shell and runs, "
                           "matching legacy") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(app.feedShellByte(0x18, std::nullopt));  // ^X
  CHECK(app.feedShellByte(0x06, std::nullopt));  // ^F
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);  // left Shell, ^X^F's own real target mode
}

TEST_CASE_FIXTURE(Fixture, "SHELL leader keys: ^X^C (EXIT) switches back to the mode SHELL was entered "
                           "from FIRST, then opens the confirmation prompt") {
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.directoryMode().currentDirectory() == (root / "subdir").string() + "/");
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }

  CHECK(app.feedShellByte(0x18, std::nullopt));  // ^X
  CHECK(app.feedShellByte(0x03, std::nullopt));  // ^C
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == (root / "subdir").string() + "/");  // shellReturnMode_'s own state, not reset
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "SHELL leader keys: ^J d (DU) works too, exercising a DIFFERENT leader (^J, not ^X)") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(app.feedShellByte(0x0a, std::nullopt));  // ^J
  CHECK(app.feedShellByte('d', std::nullopt));   // d
  CHECK(app.currentMode() == ApplicationMode::Directory);  // DU is prompt-first, like FGREP -- see startDuPrompt()
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "SHELL leader keys: bare ESC is NOT intercepted -- passes straight through as "
                           "literal shell input, unlike ^J/^X/^O") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(app.feedShellByte(0x1b, std::nullopt));  // bare ESC
  CHECK(app.currentMode() == ApplicationMode::Shell);  // still in Shell -- NOT treated as an app-wide leader trigger
}

TEST_CASE_FIXTURE(Fixture, "SHELL leader keys: normal typing is completely unaffected (regression check)") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  for (unsigned char c : std::string("echo hello")) app.feedShellByte(c, std::nullopt);
  CHECK(app.shell().commandLine().text() == "echo hello");
  CHECK(app.currentMode() == ApplicationMode::Shell);
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

TEST_CASE_FIXTURE(Fixture, "SHELL mode-line: entering from Directory mode shows the current directory as the trailing field") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  std::string text = app.shell().scrollback().modeLineText();
  CHECK(text.rfind("SHELL ", 0) == 0);
  CHECK(text.find(path()) != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "SHELL mode-line: entering from File mode shows the open file's name, not a directory (2026-08-01 user correction)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  std::string text = app.shell().scrollback().modeLineText();
  std::string expectedTrailing = (root / "plain.txt").string();
  CHECK(text.substr(text.size() - expectedTrailing.size()) == expectedTrailing);
}

TEST_CASE_FIXTURE(Fixture, "SHELL mode-line: shows the running shell program's own basename in parentheses "
                           "(2026-08-02 user report: missing entirely, compared against the legacy build's own "
                           "\"(bash)\" field)") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  std::string text = app.shell().scrollback().modeLineText();
  CHECK(text.find("(" + app.shell().programName() + ")") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "LOG (^X^Kl, K-68): snapshots Shell's scrollback to logFilePath_, viewable via File mode") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  app.shell().commandLine().setText("echo LOGTESTMARKER123");
  app.shell().submitLine();
  bool sawOutput = pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("LOGTESTMARKER123") != std::string::npos) return true;
    return false;
  });
  REQUIRE(sawOutput);

  // ^X^Kl is a leader-key sequence, only recognized by dispatchByte()'s
  // OWN state machine -- while actively in Shell mode, every byte
  // routes straight to feedShellByte() instead (see dispatchByte()'s
  // very first check), matching real usage: ^X while typing at a live
  // shell prompt is command-line input, not tcb's own leader key. ^G
  // back to Directory first, same as a real user would.
  REQUIRE(app.feedByte(0x07, std::nullopt));  // ^G
  REQUIRE(app.currentMode() == ApplicationMode::Directory);

  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte(0x0b, std::nullopt));  // ^K
  CHECK(app.feedByte('l', std::nullopt));     // l
  CHECK(app.currentMode() == ApplicationMode::File);

  bool found = false;
  for (const auto &line : app.activeContentView().lines())
    if (line.text.find("LOGTESTMARKER123") != std::string::npos) found = true;
  CHECK(found);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X s restarts a fresh SHELL child, even from within Shell mode (K-60)") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  bool ok = app.feedByte('s', std::nullopt);
  if (!ok) {
    MESSAGE("bash not available or handshake failed on restart -- skipping");
    return;
  }
  CHECK(app.currentMode() == ApplicationMode::Shell);
  CHECK(app.shell().alive());
  // NOT asserted: childPid() != firstPid. Observed flaking (reused
  // the exact same pid number on a rerun) -- this kernel apparently
  // hands the just-freed low pid straight back out to the very next
  // fork() when little else on the system is forking, so a bare pid
  // comparison isn't a reliable "it restarted" signal (matches
  // Shell::terminate()'s own comment about kill()/waitpid() edge
  // cases around this same child-teardown path). The functional
  // signal that actually matters -- the new child is a genuinely
  // live, working bash -- is checked below instead: it actually
  // executes a freshly submitted command.
  app.shell().commandLine().setText("echo RESTARTSHELLTESTMARKER");
  app.shell().submitLine();
  bool sawOutput = pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("RESTARTSHELLTESTMARKER") != std::string::npos) return true;
    return false;
  });
  CHECK(sawOutput);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(RestartShell): also works from OUTSIDE Shell mode (K-60 has no mode guard in legacy either)") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::RestartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(app.currentMode() == ApplicationMode::Shell);
  CHECK(app.shell().alive());
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(RestartShell): refused while Shell::busy() (an interactive PROGRAM is running)") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  app.shell().setBusy(true);
  CHECK_FALSE(app.handleCommand(Command::RestartShell));
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

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X ^Y marks all entries (K-72), ^X ^N clears all marks (K-73)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  CHECK(app.feedByte(0x19, std::nullopt));    // ^Y
  CHECK(app.directoryMode().markedCount() == app.directoryMode().entries().size());

  REQUIRE(app.feedByte(0x18, std::nullopt));
  CHECK(app.feedByte(0x0e, std::nullopt));  // ^N
  CHECK(app.directoryMode().markedCount() == 0);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(MarkAll/ClearAllMarks): refused outside Directory mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK_FALSE(app.handleCommand(Command::MarkAll));
  CHECK_FALSE(app.handleCommand(Command::ClearAllMarks));
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X c / ^X m / ^X r start the corresponding prompt") {
  REQUIRE(app.start(path()));
  app.feedByte(0x18, std::nullopt);
  CHECK(app.feedByte('c', std::nullopt));
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X ^K c / ^X ^K m / ^X ^K r (K-51/K-53/K-55) each start the same prompt") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte(0x0b, std::nullopt));  // ^K
  CHECK(app.feedByte('c', std::nullopt));
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "CP (^X^Kc, K-51 \"with SHELL window\"): still creates a real copy at the destination") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte(0x0b, std::nullopt));  // ^K
  REQUIRE(app.feedByte('c', std::nullopt));   // c
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));

  CHECK(fs::exists(root / "cpmvdest" / "cpmv_a.txt"));
  CHECK(fs::exists(root / "cpmv_a.txt"));  // untouched by a copy
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

// -- Prompt mode-line labels (activatePrompt()/promptLabelText()) --
// a real user report (2026-08-02) found NO prompt anywhere showed any
// visible indication of what it was asking for; every startXxxPrompt()
// drew an unlabeled blank edit field. --

TEST_CASE_FIXTURE(Fixture, "prompt labels: EXIT shows its own label with the y/n hint, in reverse video") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartExit));
  CHECK(display.rowText(display.rows() - 1).find("EXIT (y/n)?") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "prompt labels: FGREP vs EGREP shows the right one depending on syntax") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  CHECK(display.rowText(display.rows() - 1).find("FGREP") != std::string::npos);

  REQUIRE(app.feedByte(0x07, std::nullopt));  // ^G cancel
  REQUIRE(app.handleCommand(Command::StartEgrep));
  CHECK(display.rowText(display.rows() - 1).find("EGREP") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "prompt labels: DU shows its own label, and the pre-filled directory text still "
                           "appears right after it") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  std::string row = display.rowText(display.rows() - 1);
  CHECK(row.find("DU") != std::string::npos);
  CHECK(row.find(path()) != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "prompt labels: cancelling (^G) clears the label along with the prompt") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartExit));
  REQUIRE(display.rowText(display.rows() - 1).find("EXIT") != std::string::npos);

  REQUIRE(app.feedByte(0x07, std::nullopt));  // ^G
  CHECK(display.rowText(display.rows() - 1).find("EXIT") == std::string::npos);
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

TEST_CASE_FIXTURE(Fixture, "requestImmediateExit: sets wantsExit() without going through the confirmation prompt") {
  REQUIRE(app.start(path()));
  app.requestImmediateExit();

  CHECK(app.wantsExit());
  CHECK_FALSE(app.promptActive());
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

// -- History/list browser: ^R (recent directories), ^J^R (shell
// command history) -- see class comment for why only these 2 of
// m_list()'s 9 categories are wired in this slice --

TEST_CASE_FIXTURE(Fixture, "resolveCommand: ^R resolves to StartDirectoryHistory") {
  CHECK(Application::resolveCommand(0x12, std::nullopt) == Command::StartDirectoryHistory);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^R starts the command-history browser (StartCommandHistory)") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte(0x12, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(StartDirectoryHistory): refused outside Directory mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK_FALSE(app.handleCommand(Command::StartDirectoryHistory));
}

TEST_CASE_FIXTURE(Fixture, "history browser: shows real pushRecent() entries, newest first") {
  // app.start() itself now auto-records path() via DirectoryMode::
  // enterDirectory()'s own pushRecent() call (K-48's data source) --
  // done FIRST here so it's the OLDEST entry, keeping the three
  // pushed below exactly as the newest three, same relative order
  // this test always asserted.
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentDirectory, "/one");
  history.pushRecent(RingCategory::RecentDirectory, "/two");
  history.pushRecent(RingCategory::RecentDirectory, "/three");

  REQUIRE(app.handleCommand(Command::StartDirectoryHistory));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.listBrowserMode().items().size() == 4);
  CHECK(app.listBrowserMode().items()[0] == "/three");
  CHECK(app.listBrowserMode().items()[1] == "/two");
  CHECK(app.listBrowserMode().items()[2] == "/one");
  CHECK(app.listBrowserMode().items()[3] == path());
}

TEST_CASE_FIXTURE(Fixture, "history browser: Enter on a recent directory navigates DirectoryMode there") {
  // Same ordering note as the test above -- start() first so its own
  // auto-recorded path() is the OLDEST entry, leaving subdir/ as the
  // newest (and therefore default-selected, index 0) one.
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentDirectory, (root / "subdir").string());

  REQUIRE(app.handleCommand(Command::StartDirectoryHistory));
  REQUIRE(app.handleCommand(Command::Enter));

  CHECK(app.currentMode() == ApplicationMode::Directory);
  bool sawSubTxt = false;
  for (const auto &entry : app.directoryMode().entries())
    if (entry.name.find("sub.txt") != std::string::npos) sawSubTxt = true;
  CHECK(sawSubTxt);
}

TEST_CASE_FIXTURE(Fixture, "command history: Enter on an entry is a no-op in this slice") {
  history.pushRecent(RingCategory::ShellCommand, "echo hi");

  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartCommandHistory));
  REQUIRE(app.listBrowserMode().items().size() == 1);

  CHECK_FALSE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
}

// -- K-46/K-48 (^X^F/^X^D, "FILE/DIR: Select file/directory name") --
// HELP.jp 2.9's own picker of previously-accessed files/directories,
// backed by the SAME RingCategory::RecentFile/RecentDirectory rings
// K-49's plain ^R already used -- see DirectoryMode::enterDirectory()/
// FileMode::loadFile()'s own pushRecent() calls for how they get
// populated in the first place (every real access, not just ones made
// through this specific picker).

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X ^F (K-46) opens a ListBrowser over real, auto-recorded RecentFile entries") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // opens plain.txt via FileMode::loadFile() -- auto-records it
  REQUIRE(app.handleCommand(Command::Back));   // back to Directory (^X^F is Directory-only, see startListBrowser())

  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  CHECK(app.feedByte(0x06, std::nullopt));    // ^F
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.listBrowserMode().items().size() == 1);
  CHECK(app.listBrowserMode().items()[0] == (root / "plain.txt").string());
}

TEST_CASE_FIXTURE(Fixture, "K-46: Enter on a recent file entry opens it via FileMode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.handleCommand(Command::Back));

  REQUIRE(app.handleCommand(Command::StartFileHistory));
  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / "plain.txt").string());
}

TEST_CASE_FIXTURE(Fixture, "List Browser mode-line: shows the RingCategory label as trailing, and the highlighted "
                           "item as name (2026-08-02 user report: \"^X^F（FILE）が置き換わってない\")") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.handleCommand(Command::Back));

  REQUIRE(app.handleCommand(Command::StartFileHistory));
  std::string text = app.activeContentView().modeLineText();
  CHECK(text.rfind((root / "plain.txt").string() + " ", 0) == 0);  // highlighted item -- as name, left
  CHECK(text.find("(FILE)") != std::string::npos);                // RingCategory label -- as trailing, right

  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.handleCommand(Command::StartDirectoryHistory));
  CHECK(app.activeContentView().modeLineText().find("(DIR)") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(StartFileHistory): refused outside Directory mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK_FALSE(app.handleCommand(Command::StartFileHistory));
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X ^D (K-48) reuses ^R's own StartDirectoryHistory picker") {
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // descends into subdir/ -- auto-records it
  REQUIRE(app.handleCommand(Command::Back));

  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  CHECK(app.feedByte(0x04, std::nullopt));    // ^D
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  bool sawSubdir = false;
  for (const auto &item : app.listBrowserMode().items())
    if (item == (root / "subdir").string() + "/") sawSubdir = true;
  CHECK(sawSubdir);
}

TEST_CASE_FIXTURE(Fixture, "history browser: Back returns to Directory mode") {
  history.pushRecent(RingCategory::RecentDirectory, "/somewhere");
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDirectoryHistory));

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == path());
}

// -- EDIT (^Je) --

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J e starts EDIT (StartEdit)") {
  configuration.loadRcText("EDITOR\n sh -c 'echo \"EDITED:$0\" >> \"$1\"'\n", true);
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  app.feedByte(0x0a, std::nullopt);  // ^J
  app.feedByte('e', std::nullopt);   // e

  std::ifstream in(root / "plain.txt");
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(content.find("EDITED:+1") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "EDIT: from File mode, edits the open file at the real cursor line and reloads it") {
  configuration.loadRcText("EDITOR\n sh -c 'echo \"EDITED:$0\" >> \"$1\"'\n", true);
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  app.fileMode().currentFile();  // sanity: loaded

  REQUIRE(app.handleCommand(Command::StartEdit));
  CHECK(app.currentMode() == ApplicationMode::File);

  std::ifstream in(root / "plain.txt");
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(content.find("EDITED:+1") != std::string::npos);  // cursor starts at index 0 -> line 1
  // FileMode reloaded and redrew, so the newly-appended line is now
  // visible on screen too (checked via the RecordingDisplay rather
  // than a ContentView accessor, since Application doesn't expose
  // fileView_ directly).
  bool sawEdited = false;
  for (int row = 0; row < display.rows(); row++)
    if (display.rowText(row).find("EDITED") != std::string::npos) sawEdited = true;
  CHECK(sawEdited);
}

TEST_CASE_FIXTURE(Fixture, "EDIT: an editor that leaves the file untouched does not force a reload") {
  configuration.loadRcText("EDITOR\n sh -c 'exit 0'\n", true);
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));

  REQUIRE(app.handleCommand(Command::StartEdit));

  std::ifstream in(root / "plain.txt");
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(content == "hello\nworld\n");
}

TEST_CASE_FIXTURE(Fixture, "EDIT: from Directory mode, edits the highlighted entry directly, always at line 1") {
  configuration.loadRcText("EDITOR\n sh -c 'echo \"EDITED:$0\" >> \"$1\"'\n", true);
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  // Move the LISTING cursor down a few times first -- this must NOT
  // leak into the line number passed to the editor (see class
  // comment: unlike File mode, Directory-mode edits always open at
  // line 1, deliberately not carrying over do_edit()'s own dubious
  // "directory listing row as file line number" behavior).
  app.handleCommand(Command::MoveDown);
  app.handleCommand(Command::MoveDown);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::StartEdit));
  CHECK(app.currentMode() == ApplicationMode::Directory);  // EDIT never switches mode

  std::ifstream in(root / "plain.txt");
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(content.find("EDITED:+1") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "EDIT: refused on a directory entry from Directory mode") {
  configuration.loadRcText("EDITOR\n sh -c 'echo \"EDITED:$0\" >> \"$1\"'\n", true);
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  CHECK_FALSE(app.handleCommand(Command::StartEdit));
}

TEST_CASE_FIXTURE(Fixture, "EDIT: refused outside Directory/File mode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);

  CHECK_FALSE(app.handleCommand(Command::StartEdit));
}

// -- EXEC (suffix-mapped external viewer, Enter on a matching file) --

TEST_CASE_FIXTURE(Fixture, "EXEC: Enter on a suffix-mapped file runs the external program, staying in Directory mode") {
  configuration.loadRcText("EXEC\n .marker sh -c 'echo EXECRAN >> \"$0\"'\n", true);
  { std::ofstream(root / "target.marker") << "original\n"; }
  REQUIRE(app.start(path()));
  int idx = indexOf("target.marker");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Directory);  // EXEC never switches mode, like PROGRAM/EDIT

  std::ifstream in(root / "target.marker");
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(content.find("EXECRAN") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "EXEC: a suffix NOT configured as EXEC still opens normally as a plain file") {
  configuration.loadRcText("EXEC\n .marker sh -c 'echo EXECRAN >> \"$0\"'\n", true);
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == path() + "plain.txt");
}

// -- handleResize() (SIGWINCH) --

TEST_CASE_FIXTURE(Fixture, "handleResize: does nothing harmful from Directory mode with nothing loaded") {
  REQUIRE(app.start(path()));
  display.setSize(10, 40);
  app.handleResize();  // must not crash
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "handleResize: reflows File mode's content at the new width") {
  { std::ofstream(root / "longline.txt") << "abcdefghijklmnopqrstuvwxyz\n"; }
  REQUIRE(app.start(path()));
  int idx = indexOf("longline.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  display.setSize(24, 10);  // much narrower than the 80-column width File mode was constructed with
  app.handleResize();

  bool sawWrappedLine = false;
  for (int row = 0; row < display.rows(); row++)
    if (display.rowText(row).substr(0, 10) == "abcdefghij") sawWrappedLine = true;
  CHECK(sawWrappedLine);
}

TEST_CASE_FIXTURE(Fixture, "handleResize: does NOT reflow File mode's content while a different mode is active") {
  { std::ofstream(root / "longline.txt") << "abcdefghijklmnopqrstuvwxyz\n"; }
  REQUIRE(app.start(path()));
  int idx = indexOf("longline.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.handleCommand(Command::Back));  // back to Directory -- File mode's own content is now inactive
  REQUIRE(app.currentMode() == ApplicationMode::Directory);

  display.setSize(24, 10);
  app.handleResize();  // must not crash even though File mode isn't active
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "handleResize: propagates the new size to Shell's pty, even while Shell isn't visible") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));
  REQUIRE(app.handleCommand(Command::Back));  // Shell keeps running in the background
  CHECK(app.currentMode() == ApplicationMode::Directory);

  display.setSize(10, 40);
  app.handleResize();

  // Switch back to Shell and confirm its command-line field really
  // did move to the new bottom row (rows - 2, one row above
  // scrollback_'s own mode-line -- see Shell::commandLineRow()'s own
  // comment) -- the same cursorRow()-based verification
  // test_shell.cpp's own onResize() test uses, here confirming
  // Application actually calls it.
  REQUIRE(app.handleCommand(Command::StartShell));
  for (char c : std::string("x")) app.feedShellByte(static_cast<unsigned char>(c), std::nullopt);
  CHECK(display.cursorRow() == 8);
}

// -- HELP (^J^H) --

namespace {
// Matches the fixed header pattern HELP always splits on ("^  [0-9]+\\."),
// same shape as the real src/doc/HELP file's own numbered sections,
// but synthetic content (no overstrike bytes needed -- CutMode::split()
// operates on already wrapped/collapsed plain text, same as any other
// CUT source, see Application's own class comment).
std::string testHelpText() {
  return "  11. Section One\nSection one content line A.\nSection one content line B.\n"
         "  12. Section Two\nSection two content.\n";
}
}  // namespace

TEST_CASE_FIXTURE(Fixture, "HELP: refused if no help text was loaded") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.handleCommand(Command::StartHelp));
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^H starts HELP (StartHelp)") {
  REQUIRE(app.loadHelpText(testHelpText()));
  REQUIRE(app.start(path()));

  app.feedByte(0x0a, std::nullopt);  // ^J
  CHECK(app.feedByte(0x08, std::nullopt));  // ^H
  CHECK(app.currentMode() == ApplicationMode::Cut);
}

TEST_CASE_FIXTURE(Fixture, "HELP: shows the numbered sections as a CUT segment list") {
  REQUIRE(app.loadHelpText(testHelpText()));
  REQUIRE(app.start(path()));

  REQUIRE(app.handleCommand(Command::StartHelp));
  CHECK(app.currentMode() == ApplicationMode::Cut);
  REQUIRE(app.cutMode().segments().size() == 2);
  CHECK(app.cutMode().segments()[0].label == "  11. Section One");
  CHECK(app.cutMode().segments()[1].label == "  12. Section Two");
}

TEST_CASE_FIXTURE(Fixture, "HELP: splits on real doc/HELP-shaped \"chapter.section.\" headers, not just bare numbers "
                           "(2026-08-01 user report)") {
  // The real src/doc/HELP(.jp) uses two-level "N.M." numbering (e.g.
  // "2.4.", "2.18.", "5.3."), not the bare "NN." shape testHelpText()
  // uses above -- a previous kHelpPattern ("^  [1-6][1-6]+\\.", fixed
  // 2026-08-01) never matched this real shape at all (confirmed via a
  // real PTY run against doc/HELP.jp: HELP loaded as a single
  // unsplit "1/1 END" segment instead of a proper table of contents).
  std::string dottedHelp =
      "  1. Overview\nTop-level content.\n"
      "  2.1. First subsection\nSubsection content.\n"
      "  2.18. Eighteenth subsection\nMore content.\n"
      "  5.3. Another chapter's subsection\nFinal content.\n";
  REQUIRE(app.loadHelpText(dottedHelp));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartHelp));
  REQUIRE(app.cutMode().segments().size() == 4);
  CHECK(app.cutMode().segments()[0].label == "  1. Overview");
  CHECK(app.cutMode().segments()[1].label == "  2.1. First subsection");
  CHECK(app.cutMode().segments()[2].label == "  2.18. Eighteenth subsection");
  CHECK(app.cutMode().segments()[3].label == "  5.3. Another chapter's subsection");
}

TEST_CASE_FIXTURE(Fixture, "HELP: Enter on a section shows its content (CutContent), like a real CUT segment") {
  REQUIRE(app.loadHelpText(testHelpText()));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartHelp));

  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::CutContent);
}

TEST_CASE_FIXTURE(Fixture, "HELP: Back returns to wherever the user was, not always Directory") {
  { std::ofstream(root / "plain2.txt") << "hello\n"; }
  REQUIRE(app.loadHelpText(testHelpText()));
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartHelp));
  CHECK(app.currentMode() == ApplicationMode::Cut);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);  // back to File mode, not Directory
}

TEST_CASE_FIXTURE(Fixture, "HELP: invoked mid-CUT preserves the real session; Back restores it") {
  REQUIRE(app.loadHelpText(testHelpText()));
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("hello");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);
  std::string realPattern = app.cutMode().pattern();
  auto realSegmentCount = app.cutMode().segments().size();

  REQUIRE(app.handleCommand(Command::StartHelp));
  CHECK(app.cutMode().pattern() != realPattern);  // now showing HELP's own split
  REQUIRE(app.cutMode().segments().size() == 2);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Cut);
  CHECK(app.cutMode().pattern() == realPattern);  // the real CUT session, restored intact
  CHECK(app.cutMode().segments().size() == realSegmentCount);
}

TEST_CASE_FIXTURE(Fixture, "handleResize: reflows HELP's own content (a top-level split)") {
  std::string longHelp =
      "  11. Section One\nabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz\n  12. Section Two\ntext\n";
  REQUIRE(app.loadHelpText(longHelp));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartHelp));
  REQUIRE(app.handleCommand(Command::Enter));  // into Section One's content
  CHECK(app.currentMode() == ApplicationMode::CutContent);

  display.setSize(24, 10);
  app.handleResize();

  bool sawWrappedLine = false;
  for (int row = 0; row < display.rows(); row++)
    if (display.rowText(row).substr(0, 10) == "abcdefghij") sawWrappedLine = true;
  CHECK(sawWrappedLine);
}

TEST_CASE_FIXTURE(Fixture, "handleResize: does NOT reflow HELP when it's nested on top of a real CUT session") {
  std::string longHelp =
      "  11. Section One\nabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz\n  12. Section Two\ntext\n";
  REQUIRE(app.loadHelpText(longHelp));
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("hello");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.handleCommand(Command::StartHelp));  // pushed -- canGoBack() is now true
  REQUIRE(app.cutMode().canGoBack());

  display.setSize(24, 10);
  app.handleResize();  // must not crash; content reflow is skipped for nested levels

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Cut);  // the real CUT session is still intact
}

// -- KEY (^H, M_key): a browsable table of this port's own key
// bindings, Enter re-executes the highlighted one in the mode KEY was
// invoked from (HELP.jp 2.2) --

TEST_CASE("resolveCommand: ^H resolves to StartKey") {
  CHECK(Application::resolveCommand(0x08, std::nullopt) == Command::StartKey);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(StartKey): opens a browsable table of key bindings, without leaving "
                           "Directory mode's own return-anchor") {
  REQUIRE(app.start(path()));
  CHECK(app.handleCommand(Command::StartKey));
  CHECK(app.currentMode() == ApplicationMode::Key);
  CHECK_FALSE(app.activeContentView().lines().empty());
}

TEST_CASE_FIXTURE(Fixture, "Key: entries actually RENDER on screen, not just stored -- regression for a missing "
                           "keyView_.setViewport() call (2026-08-02 user report: real screenshots showed only the "
                           "mode-line row drawn, at row 0, with zero content rows)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  app.activeContentView().redraw();
  bool sawFirstEntry = false;
  for (int row = 0; row < display.rows() - 1; row++)  // every row EXCEPT the mode-line's own bottom row
    if (display.rowText(row).find("Move window down") != std::string::npos) sawFirstEntry = true;
  CHECK(sawFirstEntry);
}

TEST_CASE_FIXTURE(Fixture, "Key: re-entering from within Key mode itself is refused") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  CHECK_FALSE(app.handleCommand(Command::StartKey));
  CHECK(app.currentMode() == ApplicationMode::Key);
}

TEST_CASE_FIXTURE(Fixture, "Key: reachable from File mode too, and Back returns there (not always Directory)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartKey));
  CHECK(app.currentMode() == ApplicationMode::Key);

  REQUIRE(app.feedByte(0x07, std::nullopt));  // ^G
  CHECK(app.currentMode() == ApplicationMode::File);  // back to File, not Directory
}

TEST_CASE_FIXTURE(Fixture, "Key mode-line: shows the mode KEY was invoked from as the trailing field "
                           "(2026-08-02 user report: a real legacy screenshot showed \"KEY .../HELP.rej\" after "
                           "invoking KEY while viewing that file; this port showed nothing at all)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartKey));
  std::string text = app.activeContentView().modeLineText();
  CHECK(text.rfind("KEY ", 0) == 0);
  CHECK(text.find((root / "plain.txt").string()) != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "Key: Enter on the '^S' entry returns to Directory mode AND actually starts FGREP "
                           "(single-byte binding)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  const auto &lines = app.activeContentView().lines();
  int row = -1;
  for (std::size_t i = 0; i < lines.size(); i++)
    if (lines[i].text.rfind("^S ", 0) == 0) row = static_cast<int>(i);  // trailing space excludes "^Space"'s own row
  REQUIRE(row >= 0);
  app.activeContentView().setCursorIndex(row);

  CHECK(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Directory);  // FGREP is a prompt, not a mode switch
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "Key: Enter on the '^X ^C' entry (EXIT) chains a full multi-byte leader sequence "
                           "(^X leader -> ^C sub-command)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  const auto &lines = app.activeContentView().lines();
  int row = -1;
  for (std::size_t i = 0; i < lines.size(); i++)
    if (lines[i].text.rfind("^X ^C ", 0) == 0) row = static_cast<int>(i);
  REQUIRE(row >= 0);
  app.activeContentView().setCursorIndex(row);

  CHECK(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Directory);  // EXIT is a y/n prompt, not an immediate mode switch
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "Key: Back (^G) on any entry returns without executing anything") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  const auto &lines = app.activeContentView().lines();
  int row = -1;
  for (std::size_t i = 0; i < lines.size(); i++)
    if (lines[i].text.rfind("^X ^C ", 0) == 0) row = static_cast<int>(i);  // EXIT's own row -- would be very visible if wrongly executed
  REQUIRE(row >= 0);
  app.activeContentView().setCursorIndex(row);

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK_FALSE(app.promptActive());  // NOT the EXIT prompt -- Back never executed the highlighted entry
}

// -- Recorded macros (p_km, ^X(/^X)/^Xe) --

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X ( starts recording; the control sequence itself isn't recorded") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.isRecordingMacro());

  app.feedByte(0x18, std::nullopt);  // ^X
  CHECK(app.feedByte('(', std::nullopt));  // (
  CHECK(app.isRecordingMacro());
  CHECK(app.recordedMacroLength() == 0);  // the ^X( sequence itself is excluded

  app.feedByte('j', std::nullopt);
  app.feedByte('j', std::nullopt);
  CHECK(app.recordedMacroLength() == 2);

  app.feedByte(0x18, std::nullopt);  // ^X
  CHECK(app.feedByte(')', std::nullopt));  // )
  CHECK_FALSE(app.isRecordingMacro());
  CHECK(app.recordedMacroLength() == 2);  // the ^X) sequence is excluded too
}

TEST_CASE_FIXTURE(Fixture, "playRecordedMacro: replays recorded cursor movement") {
  // ^N (0x0e), not 'j' -- 'j' is "move window one line" (see
  // resolveCommand()'s 2026-07-31 correction) and would be a no-op
  // beep on a list this short (nothing to scroll), unlike ^N's plain
  // cursor movement this test actually wants to record/replay.
  REQUIRE(app.start(path()));
  app.feedByte(0x18, std::nullopt);
  app.feedByte('(', std::nullopt);
  app.feedByte(0x0e, std::nullopt);
  app.feedByte(0x0e, std::nullopt);
  app.feedByte(0x18, std::nullopt);
  app.feedByte(')', std::nullopt);

  std::string nameAfterRecording = app.directoryMode().selectedEntry()->name;

  app.handleCommand(Command::MoveUp);
  app.handleCommand(Command::MoveUp);
  REQUIRE(app.directoryMode().selectedEntry()->name != nameAfterRecording);

  app.feedByte(0x18, std::nullopt);
  CHECK(app.feedByte('e', std::nullopt));  // ^X e : play it back
  CHECK(app.directoryMode().selectedEntry()->name == nameAfterRecording);
}

TEST_CASE_FIXTURE(Fixture, "playRecordedMacro: false when nothing was ever recorded") {
  REQUIRE(app.start(path()));
  app.feedByte(0x18, std::nullopt);
  CHECK_FALSE(app.feedByte('e', std::nullopt));
}

TEST_CASE_FIXTURE(Fixture, "stopMacroRecording (^X)): a safe no-op when not currently recording") {
  REQUIRE(app.start(path()));
  app.feedByte(0x18, std::nullopt);
  CHECK_FALSE(app.feedByte(')', std::nullopt));
  CHECK_FALSE(app.isRecordingMacro());
}

TEST_CASE_FIXTURE(Fixture, "recording: a real leaderX command (^Xc etc) IS recorded, unlike macro control itself") {
  REQUIRE(app.start(path()));
  app.feedByte(0x18, std::nullopt);
  app.feedByte('(', std::nullopt);

  app.feedByte(0x18, std::nullopt);  // ^X
  app.feedByte('c', std::nullopt);   // c : a real CP prompt start, not macro control
  CHECK(app.promptActive());
  // Both bytes of this real ^Xc sequence are recorded, unlike the
  // ^X(/^X)/^Xe control sequences.
  CHECK(app.recordedMacroLength() == 2);
  app.feedByte(0x07, std::nullopt);  // ^G : cancel the prompt
}

TEST_CASE_FIXTURE(Fixture, "recording: a ^J-leader sequence (^Jd for DU) is captured and replays correctly") {
  REQUIRE(app.start(path()));
  app.feedByte(0x18, std::nullopt);
  app.feedByte('(', std::nullopt);
  app.feedByte(0x0a, std::nullopt);  // ^J
  app.feedByte('d', std::nullopt);   // d : DU, pre-filled with the current directory
  REQUIRE(app.promptActive());
  app.feedByte(0x0d, std::nullopt);  // ^M : accept the pre-filled path
  REQUIRE(app.currentMode() == ApplicationMode::Du);
  app.handleCommand(Command::Back);
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  app.feedByte(0x18, std::nullopt);
  app.feedByte(')', std::nullopt);

  app.feedByte(0x18, std::nullopt);
  app.feedByte('e', std::nullopt);  // replay: ^Jd, ^M
  CHECK(app.currentMode() == ApplicationMode::Du);
}

TEST_CASE_FIXTURE(Fixture, "recording: typed prompt text (a real GREP pattern) is captured and replays correctly") {
  REQUIRE(app.start(path()));
  app.feedByte(0x18, std::nullopt);
  app.feedByte('(', std::nullopt);

  app.feedByte(0x13, std::nullopt);  // ^S : start FGREP
  feedText("plain.txt");
  app.feedByte(0x0d, std::nullopt);  // submit
  REQUIRE(app.currentMode() == ApplicationMode::Grep);
  REQUIRE(app.grepMode().matches().size() == 1);
  app.handleCommand(Command::Back);
  REQUIRE(app.currentMode() == ApplicationMode::Directory);

  app.feedByte(0x18, std::nullopt);
  app.feedByte(')', std::nullopt);

  app.feedByte(0x18, std::nullopt);
  app.feedByte('e', std::nullopt);
  CHECK(app.currentMode() == ApplicationMode::Grep);
  REQUIRE(app.grepMode().matches().size() == 1);
  CHECK(app.grepMode().matches()[0].text.find("plain.txt") != std::string::npos);
}

// -- GUI mode (^Og toggle, mouse click) --

TEST_CASE_FIXTURE(Fixture, "feedByte: ^O g requests a GUI-mode toggle, consumed once") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.consumeGuiModeToggleRequest());

  app.feedByte(0x0f, std::nullopt);  // ^O
  CHECK(app.feedByte('g', std::nullopt));  // g
  CHECK(app.consumeGuiModeToggleRequest());
  CHECK_FALSE(app.consumeGuiModeToggleRequest());  // consumed -- false the second time
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^O followed by an unrecognized byte fails cleanly") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0f, std::nullopt);
  CHECK_FALSE(app.feedByte('z', std::nullopt));
  CHECK_FALSE(app.consumeGuiModeToggleRequest());
  // The leader state doesn't leak into the next byte.
  CHECK(app.feedByte('j', std::nullopt));
}

TEST_CASE_FIXTURE(Fixture, "handleMouseClick: a click on a different row selects it (like Up/Down)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  REQUIRE(idx >= 0);

  CHECK(app.handleMouseClick(idx, 0));
  CHECK(app.directoryMode().selectedEntry()->name == "plain.txt");
  CHECK(app.currentMode() == ApplicationMode::Directory);  // selecting alone doesn't open it
}

TEST_CASE_FIXTURE(Fixture, "handleMouseClick: a second click on the already-selected row opens it") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  REQUIRE(idx >= 0);

  REQUIRE(app.handleMouseClick(idx, 0));  // first click: select
  REQUIRE(app.handleMouseClick(idx, 0));  // second click on the same row: open
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == path() + "plain.txt");
}

TEST_CASE_FIXTURE(Fixture, "handleMouseClick: false outside the viewport or outside Directory mode") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.handleMouseClick(999, 0));  // way past any real row

  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  CHECK_FALSE(app.handleMouseClick(0, 0));  // Directory-only in this slice
}
