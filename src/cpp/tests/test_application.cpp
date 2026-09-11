#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <poll.h>
#include <unistd.h>

#include "application.hpp"
#include "doctest.h"
#include "help_text.hpp"
#include "mode_line.hpp"
#include "recording_display.hpp"

namespace fs = std::filesystem;

using fm::Application;
using fm::ApplicationMode;
using fm::Charset;
using fm::Command;
using fm::Configuration;
using fm::ConfiguredRole;
using fm::DuEntry;
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
    if (poll(&pfd, 1, 20) > 0 && !app.shell().onPtyReadable()) app.handleShellChildExited();
  }
  return predicate();
}

// 2026-09-19追加、runDuOnCurrentFolder()自身の非同期化(直接のユーザー
// 依頼"LOCATEと同じく^Gでduを中断し、構築中断時点のDUリストへ移動")に
// 伴い、既存のDU関連テスト群がこれまで頼っていた「StartDu直後に
// 即座にDuモードへ着地する」という同期的な前提が崩れたため必要に
// なった、pumpUntil()自身と同じ発想の小さな待機ヘルパー――実際の
// `du`は小さなfixtureディレクトリに対してならほぼ即座に終わるはず
// なので、ごく短い間隔でpollDuCompletion()を呼び続けるだけでよい。
void waitForDu(Application &app, int maxAttempts = 500) {
  for (int i = 0; i < maxAttempts && app.awaitingDu(); i++) {
    app.pollDuCompletion();
    if (app.awaitingDu()) usleep(2000);
  }
}

// executeCopy()/executeMove()/executeRemove() (2026-08-22 redesign, see
// their own comment): the real cp/mv/rm command now runs by injecting
// it into shell_'s own real, persistent SHELL session
// (shell_.commandLine().setText()+submitLine(), currentMode() becomes
// Shell) instead of a synchronous ChildProcess call -- completion is
// noticed asynchronously by pollPendingCpMvRmShellRun() (called every
// real run-loop iteration in app/main.cpp; tests call it themselves
// here, same as pumpUntil() above stands in for the run loop's own
// shell_.ptyFd() polling), which switches back to
// ApplicationMode::Directory once the shell's prompt reappears. Any
// test that submits a CP/MV/RM destination/confirm prompt now needs to
// pump until that happens BEFORE checking the filesystem/mode-line
// result, exactly like waiting for any other real bash round-trip
// elsewhere in this file.
bool pumpCpMvRmToCompletion(Application &app) {
  return pumpUntil(app, [&]() {
    app.pollPendingCpMvRmShellRun();
    return app.currentMode() == fm::ApplicationMode::Directory;
  });
}

// executeArchiveExtract() (2026-08-23 redesign, see its own comment):
// same async-completion story as CP/MV/RM's own pump helper above, but
// the return mode can be either Directory or Archive (whichever the
// extraction was launched from) -- "not Shell anymore" is the
// completion signal here instead of a fixed target mode.
bool pumpArchiveExtractToCompletion(Application &app) {
  return pumpUntil(app, [&]() {
    app.pollPendingArchiveExtractShellRun();
    return app.currentMode() != fm::ApplicationMode::Shell;
  });
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

    // A real directory (with a nested file) for the CP-of-a-directory
    // trailing-slash regression test -- see collectTargetPaths()'s own
    // comment.
    fs::create_directory(root / "nested_dir");
    { std::ofstream(root / "nested_dir" / "nested.txt") << "nested content\n"; }

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

// -- usilicrc's GREP section: seeds RecentEgrepPattern at construction --
// (2026-08-05: Configuration::grepSeedPatterns() already parsed this
// section, with its own doctest coverage, but nothing ever consumed
// it -- these get a fresh Configuration/History/Application built
// LOCALLY, in that order, rather than reusing Fixture: Fixture's own
// configuration.loadRcText() call runs in ITS constructor BODY, which
// is always AFTER its app{} member has already finished constructing
// (C++ member init order follows declaration order, not the
// initializer list), so seeding-at-construction can never be observed
// through Fixture regardless of what rc text it loads.)

// 2026-08-28 redesign, a direct user report ("登録済みのパターンは
// 履歴ではなく検索に使用するための文字列候補に過ぎない"): usilicrc's
// GREP section is a static CANDIDATE list (legacy's own str.c conf_grep()
// loads it into a separate LIST *lg, distinct from hp->ss.p.hegrep --
// the ring save_history() only grows when a search is actually
// SUBMITTED, menu.c:833). Pre-seeding RecentEgrepPattern itself (the
// OLD 2026-08-05 fix this replaces) conflated the two, so ^R (which
// legacy's own M_hist shows as the genuine USAGE history) displayed
// these static seeds even on a session where nothing has ever been
// searched. Now: RecentEgrepPattern itself stays empty until a real
// search happens, but the seeds still appear in the DIRECT-ENTRY
// candidate list (^J^S itself), merged in at display time -- see
// Configuration::grepSeedPatterns()'s own callers.
TEST_CASE_FIXTURE(Fixture, "usilicrc's GREP seed patterns appear in the ^J^S direct-entry candidate list, "
                           "but do NOT seed RecentEgrepPattern itself (so ^R's own usage-history view stays "
                           "genuinely empty until a real search has been submitted)") {
  configuration.loadRcText("GREP\n \"diff \\-\"\n ^Question\n \"^Subject: \"\n", true);
  REQUIRE(app.start(path()));

  // history_ itself: no seeding at construction.
  CHECK(history.recentEntries(RingCategory::RecentEgrepPattern).empty());

  // The ^J^S candidate list still shows the seeds (real CUI defaults,
  // matching the original 2026-08-05 report this test used to cover).
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  const auto &items = app.listBrowserMode().items();
  CHECK(std::find(items.begin(), items.end(), "diff \\-") != items.end());
  CHECK(std::find(items.begin(), items.end(), "^Question") != items.end());
  CHECK(std::find(items.begin(), items.end(), "^Subject: ") != items.end());
}

// 2026-08-28: ^R (real usage history, toggleListBrowserHistory()'s
// chronological branch) with usilicrc GREP seeds present but NOTHING
// ever actually searched yet -- must come back empty (the seeds are
// candidates to search WITH, not history of searches already done),
// shown via the existing "No String" empty-placeholder convention
// (listBrowserView_.setEmptyPlaceholder(), already wired since
// 2026-08-13 -- this redesign just makes RecentEgrepPattern itself
// genuinely empty here for the first time, so that existing mechanism
// now actually applies).
TEST_CASE_FIXTURE(Fixture, "^R usage-history view is genuinely empty (\"No String\") when usilicrc has GREP "
                           "seeds but nothing has ever actually been searched") {
  configuration.loadRcText("GREP\n \"diff \\-\"\n ^Question\n \"^Subject: \"\n", true);
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  CHECK(app.feedByte(0x12, std::nullopt));  // ^R: toggle to chronological usage history
  CHECK(app.listBrowserMode().items().empty());
  CHECK(display.rowText(0).find("No String") != std::string::npos);  // content row, not the mode line
}

// 2026-08-28: once a real search IS submitted, ^R's own chronological
// view shows exactly that one submitted pattern -- not the seeds too.
TEST_CASE_FIXTURE(Fixture, "^R usage-history view shows ONLY an actually-submitted search, not usilicrc's own "
                           "GREP seeds") {
  configuration.loadRcText("GREP\n \"diff \\-\"\n ^Question\n \"^Subject: \"\n", true);
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.feedByte(0x15, std::nullopt));  // ^U: start editing, clear the pre-filled seed
  feedText("plain\\.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // submit
  REQUIRE(app.currentMode() == ApplicationMode::Grep);
  REQUIRE(app.handleCommand(Command::Back));  // -> back to candidate list (LIFO)
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  CHECK(app.feedByte(0x12, std::nullopt));  // ^R: toggle to chronological usage history
  const auto &items = app.listBrowserMode().items();
  REQUIRE(items.size() == 1);
  CHECK(items[0] == "plain\\.txt");
}

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

TEST_CASE("resolveCommand: ';' is JumpPrevious (Back+MoveUp+Enter), ':' is JumpNext (Back+MoveDown+Enter)") {
  // 2026-08-16: a real user report ("「;」「:」の機能が逆") showed these two
  // had been swapped -- kKeyTable's own M-0/M-1 entries (item 177,
  // sourced VERBATIM from usilicrc's own macro definitions) say ';'
  // (M-0) = ^G ^P Enter = "Back, cursor up" and ':' (M-1) = ^G ^N
  // Enter = "Back, cursor down", which is what this asserts.
  CHECK(Application::resolveCommand(';', std::nullopt) == Command::JumpPrevious);
  CHECK(Application::resolveCommand(':', std::nullopt) == Command::JumpNext);
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

TEST_CASE_FIXTURE(Fixture, "File mode: shows a real display cursor at the current line and follows "
                           "MoveDown, then Back hides it again (2026-08-04 user request: plain file content "
                           "has no highlight of its own, but the current line still needs to be visibly and "
                           "programmatically knowable in both -c/-g)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK_FALSE(display.visibleCursorHidden());
  CHECK(display.cursorRow() == app.activeContentView().cursorDisplayRow());

  REQUIRE(app.handleCommand(Command::MoveDown));  // "hello\nworld\n" -- moves onto "world"
  CHECK_FALSE(display.visibleCursorHidden());
  CHECK(display.cursorRow() == app.activeContentView().cursorDisplayRow());

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  // 2026-08-05 fix: Directory mode's own row-highlight is drawn with
  // SGR reverse-video, but the terminal's own REAL cursor is now also
  // parked on that same highlighted row's right edge rather than
  // hidden outright (see ContentView::syncVisibleCursor()'s own
  // comment on why "hidden" never actually meant hidden on the real
  // terminal backend to begin with) -- not left floating over File
  // mode's old position either way.
  CHECK_FALSE(display.visibleCursorHidden());
  CHECK(display.cursorRow() == app.activeContentView().cursorDisplayRow());
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(MoveDown): clears a stale hover highlight left over from a "
                           "previous mouse hover, so keyboard navigation's highlight follows the ACTUAL "
                           "cursor again (2026-08-05 user report: mode-line/content were already correct "
                           "after switching to the keyboard, but the highlight bar itself stayed stuck on "
                           "the last-hovered row)") {
  REQUIRE(app.start(path()));
  fm::ContentView &view = app.activeContentView();
  REQUIRE(view.cursorIndex() == 0);

  // DirectoryMode's own ls-style highlight starts partway through the
  // row (past the permission/date fields, at the filename itself, see
  // Line::highlightStartColumn's own comment), not at column 0 -- scan
  // the whole row rather than assuming a fixed column.
  auto rowIsHighlighted = [&](int row) {
    for (int col = 0; col < display.columns(); col++)
      if (display.standoutAt(row, col)) return true;
    return false;
  };

  // Mirrors app/main.cpp's own applyHoverRowAndRedraw(): setHoverIndex()
  // then redraw(), same as a real mouse hover over a DIFFERENT row than
  // the current cursor would do.
  view.setHoverIndex(2);
  view.redraw();
  CHECK(rowIsHighlighted(2));        // the hovered row shows the highlight
  CHECK_FALSE(rowIsHighlighted(0));  // cursorIndex_'s own row does not (hoverIndex_ outranks it)

  REQUIRE(app.handleCommand(Command::MoveDown));
  CHECK(app.activeContentView().cursorIndex() == 1);
  CHECK(rowIsHighlighted(1));        // highlight follows the NEW cursor position...
  CHECK_FALSE(rowIsHighlighted(2));  // ...and the stale hover row is no longer highlighted
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(JumpNext/JumpPrevious): JumpNext is Back+MoveDown+Enter and "
                           "JumpPrevious is Back+MoveUp+Enter -- see resolveCommand()'s own test for which "
                           "of ';'/':' reaches which (corrected 2026-08-16, see application.cpp's own comment)") {
  // A dedicated subdirectory of three plain files, alphabetically
  // adjacent and none of them directories -- avoids depending on the
  // shared fixture's own top-level mix of files/subdirectories (whose
  // neighbor of plain.txt is a directory, not a file, which would
  // switch to Directory mode instead of File mode and make this test's
  // assertions fixture-order-fragile).
  fs::create_directory(root / "jumpdir");
  { std::ofstream(root / "jumpdir" / "aaa.txt") << "a\n"; }
  { std::ofstream(root / "jumpdir" / "bbb.txt") << "b\n"; }
  { std::ofstream(root / "jumpdir" / "ccc.txt") << "c\n"; }

  REQUIRE(app.start(path()));
  int idx = indexOf("jumpdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // Directory mode inside jumpdir/
  CHECK(app.currentMode() == ApplicationMode::Directory);

  int bIdx = -1;
  for (int i = 0; i < static_cast<int>(app.directoryMode().entries().size()); i++)
    if (app.directoryMode().entries()[i].name == "bbb.txt") bIdx = i;
  REQUIRE(bIdx >= 0);
  while (app.directoryMode().selectedEntry()->name != "bbb.txt") app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::Enter));  // File mode on bbb.txt
  REQUIRE(app.handleCommand(Command::JumpNext));  // Back (-> Directory), MoveDown, Enter
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / "jumpdir" / "ccc.txt").string());

  REQUIRE(app.handleCommand(Command::JumpPrevious));  // Back, MoveUp, Enter -- lands back on bbb.txt
  REQUIRE(app.handleCommand(Command::JumpPrevious));  // again -- one more entry up
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / "jumpdir" / "aaa.txt").string());
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

// -- resolveCommand/feedByte: ^S (single-byte), ^O^S (leader), and ^J^S
// (leader) for GREP/FGREP/EGREP --

// 2026-09-05変更(直接のユーザー依頼、"発動キーはそれぞれ^O^S、^J^S、
// ^Sとする"): プレーンな^Sは、旧来のFGREP(^O^Sへ移動)ではなく新設の
// GREPを起動するようになった。
TEST_CASE("resolveCommand: ^S resolves to StartGrep") {
  CHECK(Application::resolveCommand(0x13, std::nullopt) == Command::StartGrep);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^O ^S (StartFgrep, moved from the old plain ^S)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  CHECK(app.feedByte(0x13, std::nullopt));    // ^S
  CHECK(app.promptActive());
  CHECK(display.rowText(display.rows() - 1).find("FGREP") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: plain ^S (StartGrep, new) opens a blank prompt labeled GREP, not "
                           "FGREP/EGREP") {
  REQUIRE(app.start(path()));
  CHECK(app.feedByte(0x13, std::nullopt));  // ^S
  CHECK(app.promptActive());
  std::string row = display.rowText(display.rows() - 1);
  CHECK(row.find("FGREP") == std::string::npos);
  CHECK(row.find("EGREP") == std::string::npos);
  CHECK(row.find("GREP") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "GrepMode::search (Basic/plain grep): 'grep' role runs with basic-regex syntax, "
                           "distinct from Fixed/Regex") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x13, std::nullopt));  // ^S -> GREP
  feedText("plain");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // submit
  // 2026-09-09: plain ^S (Basic syntax) from Directory now stays
  // in-place (see .steering/20260909-grep-inline-highlight/design.md)
  // instead of switching to ApplicationMode::Grep -- the search itself
  // (and grepMode_.matches()) is unaffected.
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.grepMode().matches().size() == 1);
  CHECK(app.grepMode().matches()[0].text.find("plain.txt") != std::string::npos);
}

// -- in-place GREP (2026-09-09, a direct user proposal replacing the
// short-lived '{'/'}' macro): plain ^S (Basic syntax) from Directory/
// File no longer switches to the separate ApplicationMode::Grep
// results screen -- it jumps the cursor to the first match, in place,
// and ^N/^P step between matches (wrapping at the ends) while the
// session is active. See .steering/20260909-grep-inline-highlight/
// design.md for the full rationale.

TEST_CASE_FIXTURE(Fixture, "in-place GREP (Directory origin): ^S stays in Directory, jumps the cursor to the "
                           "first match, and shows GREP/(grep) on the mode line") {
  { std::ofstream(root / "one.json") << "{}\n"; }
  { std::ofstream(root / "two.json") << "{}\n"; }
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x13, std::nullopt));  // ^S -> GREP
  feedText("json$");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // submit

  CHECK(app.currentMode() == ApplicationMode::Directory);  // never switches to ApplicationMode::Grep
  REQUIRE(app.directoryMode().selectedEntry() != nullptr);
  CHECK(app.directoryMode().selectedEntry()->name == "one.json");  // first match, alphabetically first
  const std::string &modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.find("GREP") != std::string::npos);
  CHECK(modeLine.find("(grep)") != std::string::npos);

  REQUIRE(app.grepMode().matches().size() == 2);
  int idx = indexOf("one.json");
  CHECK_FALSE(display.highlightAt(idx, 0));   // permissions column -- NOT part of the match
  CHECK(display.highlightAt(idx, 57));        // "json" starts at byte/column 57
}

TEST_CASE_FIXTURE(Fixture, "in-place GREP (File origin): ^S from an open file stays in File mode and jumps to "
                           "the first matching line") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x13, std::nullopt));  // ^S -> GREP
  feedText("marker two");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // submit

  CHECK(app.currentMode() == ApplicationMode::File);  // never switches to ApplicationMode::Grep
  const std::string &modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.find("GREP") != std::string::npos);
  CHECK(modeLine.find("(grep)") != std::string::npos);
}

// 2026-09-11追加、直接のユーザー報告("FILE/DIRでも^O^S/^J^S同様
// ^S/^[iをサポートして欲しい"): 上のテストはDirectory経由(Enter)で
// Fileに入った、pagerMode_==falseのケースだけを確認していた。実機
// PTYで`usilic -c <file>`(CLIから直接ファイルを開く、pagerMode_==true、
// startPagerFileList()自身の宣言コメント参照)を再現したところ、
// 実際のハイライト/マッチ自体は正しく設定されるにも関わらず、
// FileMode自身のsetModeLineFieldsProvider()のpagerMode_早期returnが
// 一度もgrepInPlaceActive()/grepNoMatchActive()を確認していなかった
// ため、モードラインには"GREP"/(grep)バッジが一切反映されなかった
// (^O^S/^J^Sは専用の一覧画面へ切り替わるためこの欠落の影響を受け
// ない)。同じ条件をpagerMode_分岐にも複製して修正した。
TEST_CASE_FIXTURE(Fixture, "in-place GREP (File origin, pagerMode_ / CLI-direct launch): ^S from a "
                           "startPagerFileList() screen shows the GREP/(grep) mode-line badge too, not just "
                           "the Directory-Enter case") {
  REQUIRE(app.startPagerFileList({(root / "plain.txt").string()}));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x13, std::nullopt));  // ^S -> GREP
  feedText("world");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // submit

  CHECK(app.currentMode() == ApplicationMode::File);
  const std::string &modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.find("GREP") != std::string::npos);
  CHECK(modeLine.find("(grep)") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "in-place GREP (File origin, pagerMode_): a NO-MATCH search also shows the "
                           "GREP/(grep) badge, not just a bare pager mode line") {
  REQUIRE(app.startPagerFileList({(root / "plain.txt").string()}));

  REQUIRE(app.feedByte(0x13, std::nullopt));
  feedText("zzznotfound");
  // feedByte()/executeGrep() legitimately return false here -- the search
  // ran but found 0 matches (activateGrepInPlace()'s own empty-matches
  // branch), same as the Directory-origin no-match case elsewhere in this
  // file. Only the mode-line side effect (No String message) matters here.
  app.feedByte(0x0d, std::nullopt);

  const std::string &modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.find("GREP") != std::string::npos);
  CHECK(modeLine.find("(grep)") != std::string::npos);
  CHECK(modeLine.find("No String") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "in-place GREP: ^N/^P step between matches, wrapping at the ends") {
  { std::ofstream(root / "one.json") << "{}\n"; }
  { std::ofstream(root / "two.json") << "{}\n"; }
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x13, std::nullopt));
  feedText("json$");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.directoryMode().selectedEntry()->name == "one.json");

  CHECK(app.feedByte(0x0e, std::nullopt));  // ^N -> 2nd match
  CHECK(app.directoryMode().selectedEntry()->name == "two.json");

  CHECK(app.feedByte(0x0e, std::nullopt));  // ^N again -> wraps back to the 1st match
  CHECK(app.directoryMode().selectedEntry()->name == "one.json");

  CHECK(app.feedByte(0x10, std::nullopt));  // ^P -> wraps to the LAST match
  CHECK(app.directoryMode().selectedEntry()->name == "two.json");
}

TEST_CASE_FIXTURE(Fixture, "in-place GREP: marking ('.') keeps the session alive, and CP/PROGRAM see the "
                           "marked entries via targetEntries() -- the whole point of dropping '{'/'}' ") {
  { std::ofstream(root / "one.json") << "{}\n"; }
  { std::ofstream(root / "two.json") << "{}\n"; }
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x13, std::nullopt));
  feedText("json$");
  REQUIRE(app.feedByte(0x0d, std::nullopt));

  REQUIRE(app.feedByte('.', std::nullopt));  // ToggleMark on "one.json"
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().markedCount() == 1);
  CHECK(app.activeContentView().modeLineText().find("GREP") != std::string::npos);  // session still active

  REQUIRE(app.feedByte(0x0e, std::nullopt));  // ^N -> "two.json", session still active
  REQUIRE(app.feedByte('.', std::nullopt));   // mark it too
  CHECK(app.directoryMode().markedCount() == 2);

  std::vector<fm::DirectoryEntry> targets = app.directoryMode().targetEntries();
  REQUIRE(targets.size() == 2);
  CHECK((targets[0].name == "one.json" || targets[0].name == "two.json"));
  CHECK((targets[1].name == "one.json" || targets[1].name == "two.json"));
}

// 2026-09-10 revision, a direct user report: plain navigation/paging
// (MoveUp/MoveDown/PageUp/PageDown/ScrollLineUp/ScrollLineDown/GoToTop/
// GoToBottom/Redraw) is NOT a "clear mode switch" and must NOT end the
// session either -- only replaced the earlier "anything but ^N/^P/
// marking" rule (isGrepInPlaceSafeCommand()'s own comment).
TEST_CASE_FIXTURE(Fixture, "in-place GREP: plain navigation/paging (MoveDown, ScrollLineDown, Redraw, ...) "
                           "does NOT end the session -- only a genuine mode switch does") {
  { std::ofstream(root / "one.json") << "{}\n"; }
  { std::ofstream(root / "two.json") << "{}\n"; }
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x13, std::nullopt));
  feedText("json$");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.activeContentView().modeLineText().find("GREP") != std::string::npos);

  for (Command safe : {Command::MoveDown, Command::MoveUp, Command::PageDown, Command::PageUp,
                        Command::ScrollLineDown, Command::ScrollLineUp, Command::GoToBottom, Command::GoToTop,
                        Command::Redraw}) {
    app.handleCommand(safe);
    CHECK(app.activeContentView().modeLineText().find("GREP") != std::string::npos);  // still active
  }

  // GoToBottom/GoToTop above moved the cursor off "one.json" -- land
  // back on it (a real file) before Enter, so the mode switch actually
  // happens (GoToTop just ran, so this forward-only search is valid).
  while (indexOf(app.directoryMode().selectedEntry()->name) != indexOf("one.json")) app.handleCommand(Command::MoveDown);

  // A genuine mode switch (Enter, opening the entry under the cursor)
  // still ends the session, and the command itself still runs normally.
  CHECK(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);  // the command itself was not blocked

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.activeContentView().modeLineText().find("GREP") == std::string::npos);  // session ended
  CHECK(app.activeContentView().modeLineText().find("(ls)") != std::string::npos);  // back to normal
}

// 2026-09-10, direct user reports across two rounds: (1) =fv/=fw --
// the message must appear at the SAME position/style as LOCATE's own
// "Updating database..." (a short "GREP" label followed by the actual
// message), and the message text itself must be non-highlighted (a
// plain, non-Standout run) -- NOT a request to change drawPromptLabel()
// itself (that detour caused a real regression there, reverted).
// (2) the origin ("return value") must still be visible via trailing3,
// same as every other mode-switching prompt already shows.
TEST_CASE_FIXTURE(Fixture, "in-place GREP: no match shows a temporary, LOCATE-style mode-line message "
                           "(\"GREP\" label + non-highlighted message + origin path) that clears on the "
                           "next command, without starting a session") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x13, std::nullopt));
  // Short pattern (unlike an earlier draft of this test): "GREP" +
  // "zzz: No String" + the origin path all have to share one
  // 80-column row (trailing3's own established budget-based truncation
  // -- content_view.cpp's own comment -- drops it to empty rather than
  // partially showing it once the budget gets too tight, same as any
  // other trailing3 user already living with that tradeoff).
  feedText("zzz");
  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));  // no match -- activateGrepInPlace() returns false

  CHECK(app.currentMode() == ApplicationMode::Directory);
  {
    const std::string &row = display.rowText(display.rows() - 1);
    CHECK(row.find("GREP") != std::string::npos);
    std::size_t messagePos = row.find("zzz: No String");
    CHECK(messagePos != std::string::npos);
    CHECK(row.find(path()) != std::string::npos);  // the origin ("return value") survives, unlike =fv
    // The message itself must be non-highlighted (direct user request:
    // "非ハイライトの「〜:No String」を表示して欲しい") -- unlike the
    // "GREP" label (column 0, still Standout) right before it.
    int lastRow = display.rows() - 1;
    CHECK(display.standoutAt(lastRow, 0));                          // "GREP" IS still Standout
    CHECK_FALSE(display.standoutAt(lastRow, static_cast<int>(messagePos)));  // the message is NOT
  }

  CHECK(app.handleCommand(Command::MoveDown));  // any command clears it (DirectoryMode::clearGrepNoMatchMessage())
  {
    const std::string &row = display.rowText(display.rows() - 1);
    CHECK(row.find("No String") == std::string::npos);
  }
}

// 2026-09-10, a direct user follow-up with two side-by-side screenshots
// (=gd: ESC i live typing, ".git" starts immediately after "GREP"; =gc:
// after Enter on no match, "GREP" is followed by position/byte-count
// fields ("06/23 26%"/"1438") before the message finally starts) --
// ".git" landing at two DIFFERENT screen columns in the two states felt
// inconsistent: "why not just stay in state (1) if going back to it
// (via Backspace) lands you right back where you started anyway?" The
// fix suppresses the position text AND the byte-count trailing field
// specifically for this message (via setPositionTextOverride("") and
// clearing fields.trailing), so the message starts in the same place
// the live pattern text already does.
//
// 2026-09-10 further revision, a direct user follow-up ("これは致命的
// なバグと同じ。見える場所だから"): the fix above still left ONE extra
// space here (trailing2's own unconditional leading space, on top of
// name's own trailing space) that didn't match the live-typing state's
// single space. Verified via a temporary experiment (removing that
// leading space outright) that it's a genuinely shared, load-bearing
// separator elsewhere (ArchiveMode's trailing byte count would run
// into its own "(tar)" badge, e.g. "1765(tar)" instead of "1765 (tar)")
// -- so instead of removing it globally, ModeLineFields::
// trailing2NoLeadingSpace was added as an opt-in flag (default false,
// every other provider unaffected) and set true only here.
TEST_CASE_FIXTURE(Fixture, "in-place GREP: the no-match message starts immediately after \"GREP\" with "
                           "exactly ONE space, no position/byte-count fields and no extra space in "
                           "between (matching where the live ESC i pattern text already sits, per direct "
                           "user comparison screenshots)") {
  { std::ofstream(root / "one.json") << "{}\n"; }
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('i', std::nullopt));
  feedText("json ");  // "json " -- no match
  REQUIRE(app.grepMode().matches().empty());
  REQUIRE_FALSE(app.feedByte(0x0d, std::nullopt));  // Enter -- shows "No String", prompt stays open

  const std::string &row = display.rowText(display.rows() - 1);
  std::size_t nameEnd = row.find("GREP") + std::string("GREP").size();
  std::size_t messagePos = row.find("json : No String");
  REQUIRE(messagePos != std::string::npos);
  // Exactly one space -- pixel-for-pixel the same column the live ESC i
  // pattern text ("GREP json") already occupies.
  CHECK(messagePos == nameEnd + 1);
}

// 2026-09-10, a direct user report after actually trying the feature
// on a real file ("とにかくハイライト表示は出てくれないと...CLAUDE.md
// 等、通常ファイルでは何にヒットしたかもわからない"): the earlier
// design.md decision to skip simultaneous multi-match highlighting
// (relying on the cursor alone to show "the" current match) doesn't
// actually work for File-mode content, where there's no per-row
// highlight at all outside the live terminal caret. ALL matches are
// now Bold (Display::Attribute::Bold) simultaneously, independent of
// the cursor (Standout) and Directory's own dired-style marks
// (Underline) -- three separate, composable channels.
//
// 2026-09-10 further revision, another direct follow-up ("bold文字は
// 分かりやすくない...マッチ文字列をboldの代わりにハイライトでやって
// みて。色は変えられない？薄めの色とか"): Bold itself didn't stand out
// enough, and highlighting the whole line still didn't show WHAT
// matched -- superseded by substring-only Display::Attribute::Highlight
// (a pale/bright-yellow foreground, see terminal_display.cpp's
// attributeEscape() and graphical_display.cpp's kHighlightColor), scoped
// to just the matched span via GrepMatch::matchSpans. The test names/
// comments below still say "Bold" in places as a historical label for
// "the multi-match-simultaneously behavior" -- the assertions
// themselves use highlightAt(), the actual current mechanism.
TEST_CASE_FIXTURE(Fixture, "in-place GREP (Directory origin): ALL matches are Bold simultaneously, not just "
                           "the one under the cursor, and clears when the session ends") {
  { std::ofstream(root / "one.json") << "{}\n"; }
  { std::ofstream(root / "two.json") << "{}\n"; }
  REQUIRE(app.start(path()));
  int plainIdx = indexOf("plain.txt");  // a non-match, for contrast
  int oneIdx = indexOf("one.json");
  int twoIdx = indexOf("two.json");
  REQUIRE(app.feedByte(0x13, std::nullopt));
  feedText("json$");
  REQUIRE(app.feedByte(0x0d, std::nullopt));

  // 2026-09-10 revision: highlighting is now substring-only (just the
  // matched "json", not the whole line -- a direct user follow-up
  // request), so look up the exact matched column via matchSpans
  // rather than assuming column 0.
  REQUIRE(app.grepMode().matches().size() == 2);
  auto spanColFor = [&](int lineIdx) -> int {
    for (const auto &m : app.grepMode().matches())
      if (m.sourceLineIndex == lineIdx) return m.matchSpans.at(0).first;
    FAIL("no match found for line ", lineIdx);
    return -1;
  };
  int oneCol = spanColFor(oneIdx);
  int twoCol = spanColFor(twoIdx);

  CHECK(display.highlightAt(oneIdx, oneCol));   // matched, cursor is here
  CHECK(display.highlightAt(twoIdx, twoCol));   // ALSO matched, even though the cursor is NOT here
  CHECK_FALSE(display.highlightAt(oneIdx, 0));  // permissions column -- NOT part of the match
  CHECK_FALSE(display.highlightAt(plainIdx, 0));  // not a match at all

  REQUIRE(app.feedByte(0x0e, std::nullopt));  // ^N -> move to "two.json", both stay Bold
  CHECK(display.highlightAt(oneIdx, oneCol));
  CHECK(display.highlightAt(twoIdx, twoCol));

  // A genuine mode switch (Enter, opening "two.json" -- the cursor is
  // there after the ^N above) ends the session; verify the highlight
  // on BOTH previously-matched lines is cleared, not just the one the
  // cursor happened to land on.
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK_FALSE(display.highlightAt(oneIdx, oneCol));
  CHECK_FALSE(display.highlightAt(twoIdx, twoCol));
}

TEST_CASE_FIXTURE(Fixture, "in-place GREP (File origin): ALL matching lines are Bold simultaneously, "
                           "answering the direct user complaint that a plain text file's matches were "
                           "otherwise invisible") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");  // "marker one\nline1\nmarker two\nline2a\nline2b\nmarker three\nline3\n"
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x13, std::nullopt));
  feedText("marker");
  REQUIRE(app.feedByte(0x0d, std::nullopt));

  // "marker one"/"marker two"/"marker three" are lines 0, 2, 5 (0-indexed).
  CHECK(display.highlightAt(0, 0));
  CHECK(display.highlightAt(2, 0));
  CHECK(display.highlightAt(5, 0));
  CHECK_FALSE(display.highlightAt(1, 0));  // "line1" -- not a match
}

// -- incremental GREP (2026-09-10, a direct user request: "インクリメ
// ンタルサーチについてはgrepではなく...文字列位置を返す関数等だけで
// インプリメントは可能?...いずれも可能ならやってみて...切り替え
// キーを新設すればいい") -- ESC i starts a live, per-keystroke search
// using GrepMode::searchInProcess() (fgrep-style literal substring
// matching -- 2026-09-10 same-day revision, see its own declaration
// comment in grep_mode.hpp; no child process at all, unlike plain ^S
// which forks a real grep/-o once on Enter). Reuses the exact same
// in-place session machinery (activateGrepInPlace()/
// deactivateGrepInPlace()) so the end state is indistinguishable from
// a submitted ^S search.

TEST_CASE_FIXTURE(Fixture, "ESC i (StartIncrementalGrep): highlights update live as each character is "
                           "typed, before Enter is ever pressed") {
  { std::ofstream(root / "one.json") << "{}\n"; }
  { std::ofstream(root / "two.json") << "{}\n"; }
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x1b, std::nullopt));  // ESC
  REQUIRE(app.feedByte('i', std::nullopt));
  CHECK(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);  // never switches, same as plain ^S

  int oneIdx = indexOf("one.json");
  int twoIdx = indexOf("two.json");

  // 2026-09-10 revision: searchInProcess() is fgrep-style literal
  // substring matching now, not regex (a direct user follow-up after
  // "." matched every character as a wildcard) -- "json$" would no
  // longer match anything (the literal 5 bytes "json$" aren't present
  // in either filename), so a plain literal pattern is used here.
  feedText("json");  // NOT submitted yet -- no Enter
  CHECK(app.promptActive());  // still editing
  REQUIRE(app.grepMode().matches().size() == 2);
  auto spanColFor = [&](int lineIdx) -> int {
    for (const auto &m : app.grepMode().matches())
      if (m.sourceLineIndex == lineIdx) return m.matchSpans.at(0).first;
    FAIL("no match found for line ", lineIdx);
    return -1;
  };
  CHECK(display.highlightAt(oneIdx, spanColFor(oneIdx)));  // already highlighted, live
  CHECK(display.highlightAt(twoIdx, spanColFor(twoIdx)));

  // Enter finalizes: the prompt closes, the in-place session (already
  // built up live) is what's left showing.
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.activeContentView().modeLineText().find("GREP") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "ESC i: ^G cancels and reverts the live highlight entirely (emacs isearch's own "
                           "C-g convention -- unlike Enter, which keeps it)") {
  { std::ofstream(root / "one.json") << "{}\n"; }
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('i', std::nullopt));
  feedText("json");
  int idx = indexOf("one.json");
  REQUIRE(app.grepMode().matches().size() == 1);
  CHECK(display.highlightAt(idx, app.grepMode().matches()[0].matchSpans.at(0).first));

  REQUIRE(app.feedByte(0x07, std::nullopt));  // ^G
  CHECK_FALSE(app.promptActive());
  CHECK(app.activeContentView().modeLineText().find("GREP") == std::string::npos);  // reverted, not kept
}

TEST_CASE_FIXTURE(Fixture, "ESC i: an empty pattern (e.g. after backspacing everything) clears any live "
                           "highlight instead of leaving stale matches") {
  { std::ofstream(root / "one.json") << "{}\n"; }
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('i', std::nullopt));
  feedText("json");
  REQUIRE(app.grepMode().matches().size() == 1);
  CHECK(app.activeContentView().modeLineText().find("GREP") != std::string::npos);

  for (int i = 0; i < 4; i++) app.feedByte(0x7f, std::nullopt);  // backspace x4 -> back to empty
  CHECK(app.activeContentView().modeLineText().find("GREP") == std::string::npos);
}

// 2026-09-10, a direct user report after actually trying ESC i on a real
// directory: "^[iでの検索さなかでは^N^Pが利かない" -- dispatchByte()'s
// `if (promptKind_ != PromptKind::None) return feedPromptByte(byte);`
// caught ^N/^P first and routed them into the still-open prompt editor
// (a no-op there), because ESC i's prompt stays open until Enter --
// unlike a submitted ^S session, where GrepInPlaceNext/Previous's own
// ^N/^P intercept (a few lines further down in dispatchByte()) only
// ever got a chance to run once the prompt had already closed.
TEST_CASE_FIXTURE(Fixture, "ESC i: ^N/^P jump between matches DURING live typing, before Enter is ever "
                           "pressed (the exact user-reported bug)") {
  { std::ofstream(root / "one.json") << "{}\n"; }
  { std::ofstream(root / "two.json") << "{}\n"; }
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('i', std::nullopt));
  feedText("json");
  REQUIRE(app.grepMode().matches().size() == 2);
  REQUIRE(app.directoryMode().selectedEntry()->name == "one.json");
  CHECK(app.promptActive());  // still editing -- Enter was never pressed

  CHECK(app.feedByte(0x0e, std::nullopt));  // ^N -> 2nd match, all while the prompt is still open
  CHECK(app.directoryMode().selectedEntry()->name == "two.json");
  CHECK(app.promptActive());  // ^N did NOT close the prompt

  CHECK(app.feedByte(0x10, std::nullopt));  // ^P -> back to the 1st match
  CHECK(app.directoryMode().selectedEntry()->name == "one.json");
  CHECK(app.promptActive());

  // The pattern itself is still editable afterward -- ^N/^P didn't
  // consume the prompt or otherwise disturb it.
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // submit
  CHECK_FALSE(app.promptActive());
  CHECK(app.activeContentView().modeLineText().find("GREP") != std::string::npos);
}

// 2026-09-10, a direct user report after actually trying ESC i on a real
// directory (usilic -c ~/git/usilic): typing more characters (narrowing
// the match set, e.g. "." then ".j") left the OLD, now-stale highlight
// from an earlier keystroke still showing on lines that no longer
// match -- setGrepMatchHighlight() was re-reading view_.lines() (already
// dirtied by the PREVIOUS keystroke's own highlight) as its "clean"
// base and only ever ADDING runs, never removing the previous ones.
TEST_CASE_FIXTURE(Fixture, "ESC i: narrowing the pattern clears the highlight on lines that no longer "
                           "match, not just adds to lines that newly match (the exact user-reported "
                           "stale-highlight bug)") {
  { std::ofstream(root / "one.json") << "{}\n"; }
  { std::ofstream(root / "two.json") << "{}\n"; }
  { std::ofstream(root / "plain.txt") << "hi\n"; }
  REQUIRE(app.start(path()));
  int oneIdx = indexOf("one.json");
  int twoIdx = indexOf("two.json");
  int plainIdx = indexOf("plain.txt");

  auto spanColFor = [&](int lineIdx) -> int {
    for (const auto &m : app.grepMode().matches())
      if (m.sourceLineIndex == lineIdx) return m.matchSpans.at(0).first;
    FAIL("no match found for line ", lineIdx);
    return -1;
  };

  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('i', std::nullopt));
  // literal '.' matches every entry with an extension dot in its ls
  // line -- this fixture's shared root has plenty of others besides
  // the three below (archive.tar etc.), so this only checks that
  // these three are AMONG the matches, not the total count.
  feedText(".");
  CHECK(display.highlightAt(oneIdx, spanColFor(oneIdx)));
  CHECK(display.highlightAt(twoIdx, spanColFor(twoIdx)));
  CHECK(display.highlightAt(plainIdx, spanColFor(plainIdx)));

  feedText("j");  // pattern is now ".j" -- only one.json/two.json contain it, plain.txt no longer matches
  CHECK(display.highlightAt(oneIdx, spanColFor(oneIdx)));
  CHECK(display.highlightAt(twoIdx, spanColFor(twoIdx)));
  // The bug: plain.txt's highlight from the PREVIOUS keystroke (when
  // "." matched everything) used to survive here even though ".j"
  // doesn't match plain.txt at all.
  CHECK_FALSE(display.highlightAt(plainIdx, 0));
  CHECK_FALSE(display.highlightAt(plainIdx, 1));
  CHECK_FALSE(display.highlightAt(plainIdx, 2));
}

// 2026-09-10, a direct user report after actually trying ESC i on a real
// directory (usilic -c git/usilic): "^[i.→全マッチ文字.がハイライト
// 表示→さらに␣→.␣はマッチしないのにハイライトが消えない" --
// narrowing the pattern all the way down to ZERO matches (not just
// "fewer, but still some") hit a DIFFERENT bug than the one just above:
// activateGrepInPlace()'s own empty-matches branch reported the
// no-match message and returned early WITHOUT clearing the highlight
// or session state left over from the PREVIOUS (non-empty) keystroke,
// because that branch had only ever been reached before from a fresh,
// never-yet-activated search.
TEST_CASE_FIXTURE(Fixture, "ESC i: narrowing the pattern all the way down to ZERO matches clears the "
                           "stale highlight from a previous, broader match (the exact user-reported bug)") {
  { std::ofstream(root / "one.json") << "{}\n"; }
  REQUIRE(app.start(path()));
  int oneIdx = indexOf("one.json");
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('i', std::nullopt));
  feedText("json");
  REQUIRE(app.grepMode().matches().size() == 1);
  int oneCol = app.grepMode().matches()[0].matchSpans.at(0).first;
  CHECK(display.highlightAt(oneIdx, oneCol));
  CHECK(app.activeContentView().modeLineText().find("GREP") != std::string::npos);

  feedText("XYZNOMATCH");  // now matches nothing at all
  CHECK(app.grepMode().matches().empty());
  CHECK_FALSE(display.highlightAt(oneIdx, oneCol));  // the bug: this used to stay highlighted
  CHECK(app.activeContentView().modeLineText().find("No String") != std::string::npos);
}

// Same-day, a second bug surfaced by the same repro: pressing Enter
// after narrowing to zero live matches ("さらに^M→意味不明") used to
// re-run a REAL grep subprocess in GrepSyntax::Basic (regex) via the
// normal executeGrep() path -- a DIFFERENT semantics than
// searchInProcess()'s fgrep-style literal matching that produced the
// live preview. For a pattern containing regex metacharacters (a
// literal '.' followed by a space, i.e. ". "), the live/literal search
// correctly finds nothing, but the real grep would reinterpret it as
// "any character, then a space" and suddenly match many unrelated
// lines -- a jarring discontinuity between what's shown while typing
// and what Enter actually commits to.
TEST_CASE_FIXTURE(Fixture, "ESC i: Enter after narrowing to zero matches does NOT re-interpret the "
                           "pattern as regex and suddenly show unrelated matches (the exact user-reported "
                           "\"意味不明\" bug)") {
  { std::ofstream(root / "one.json") << "{}\n"; }
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('i', std::nullopt));
  feedText(".");  // literal '.' -- matches several entries with an extension dot
  REQUIRE_FALSE(app.grepMode().matches().empty());

  feedText(" ");  // pattern is now ". " -- no ls line literally contains "dot space"
  REQUIRE(app.grepMode().matches().empty());

  // 2026-09-10 revision, a direct user follow-up request (see the
  // "keeps the prompt open" test just below): Enter on a still-empty
  // match set no longer closes the prompt at all, so this now returns
  // false (submitPrompt()'s own "nothing was committed" convention) and
  // promptActive() stays true.
  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));  // submit
  CHECK(app.promptActive());
  // The bug: a real `grep -e '. '` (BRE, "." = any char) would match
  // virtually every ls line (any two adjacent characters ending in a
  // space), so this used to flip from "no match" to "GREP n/m (grep)"
  // with a large match count right here.
  CHECK(app.grepMode().matches().empty());
  CHECK(app.activeContentView().modeLineText().find("No String") != std::string::npos);
}

// 2026-09-10, a direct user follow-up after confirming the "意味不明"
// fix above ("それでもプロンプトは生きている、ってのはどうだろう？
// .git ␣はマッチしないけど^Hで.gitに戻せばマッチするので、No Stringが
// 消えると同時に検索も継続可能ってのは？"): pressing Enter on a
// pattern with zero live matches should show the "No String" message
// (same as before) but leave the prompt open, so backspacing back to a
// matching pattern resumes the search immediately instead of forcing
// the user to start a brand new ESC i session from scratch.
TEST_CASE_FIXTURE(Fixture, "ESC i: Enter on zero matches shows \"No String\" but keeps the prompt open, "
                           "so backspacing back to a matching pattern resumes the search live") {
  { std::ofstream(root / "one.json") << "{}\n"; }
  { std::ofstream(root / "two.json") << "{}\n"; }
  REQUIRE(app.start(path()));
  int oneIdx = indexOf("one.json");
  int twoIdx = indexOf("two.json");

  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('i', std::nullopt));
  feedText("json ");  // "json " (trailing space) -- no ls line contains this literal substring
  REQUIRE(app.grepMode().matches().empty());

  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));  // Enter -- rejected, nothing committed
  CHECK(app.promptActive());  // still editing, unlike a normal successful submission
  CHECK(app.activeContentView().modeLineText().find("No String") != std::string::npos);

  REQUIRE(app.feedByte(0x7f, std::nullopt));  // Backspace -- removes the trailing space, "json" matches again
  CHECK(app.promptActive());  // Enter never closed it, so this is still the SAME live session
  REQUIRE(app.grepMode().matches().size() == 2);
  auto spanColFor = [&](int lineIdx) -> int {
    for (const auto &m : app.grepMode().matches())
      if (m.sourceLineIndex == lineIdx) return m.matchSpans.at(0).first;
    FAIL("no match found for line ", lineIdx);
    return -1;
  };
  CHECK(display.highlightAt(oneIdx, spanColFor(oneIdx)));
  CHECK(display.highlightAt(twoIdx, spanColFor(twoIdx)));

  // The session is still fully usable afterward -- Enter now commits normally.
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK_FALSE(app.promptActive());
  CHECK(app.activeContentView().modeLineText().find("GREP") != std::string::npos);
}

// File origin's own version of the same bug: setGrepMatchHighlight()
// re-snapshotted view_.lines() (already dirtied by the previous
// keystroke) as the "clean" baseline on EVERY call, so a line that
// stopped matching kept its stale highlight baked permanently into
// what the code considered "the original" content.
TEST_CASE_FIXTURE(Fixture, "ESC i (File origin): narrowing the pattern clears the highlight on lines "
                           "that no longer match") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");  // "marker one\nline1\nmarker two\nline2a\nline2b\nmarker three\nline3\n"
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('i', std::nullopt));
  feedText("marker");
  REQUIRE(app.grepMode().matches().size() == 3);  // lines 0, 2, 5
  CHECK(display.highlightAt(0, 0));
  CHECK(display.highlightAt(2, 0));
  CHECK(display.highlightAt(5, 0));

  feedText(" o");  // pattern is now "marker o" -- only "marker one" (line 0) still matches
  REQUIRE(app.grepMode().matches().size() == 1);
  CHECK(app.grepMode().matches()[0].sourceLineIndex == 0);
  CHECK(display.highlightAt(0, 0));
  // The bug: lines 2 ("marker two") and 5 ("marker three") kept their
  // stale highlight from the previous, broader "marker" match.
  CHECK_FALSE(display.highlightAt(2, 0));
  CHECK_FALSE(display.highlightAt(5, 0));
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

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^S (StartEgrep) opens directly into the candidate-pattern list, "
                           "not a blank prompt (2026-08-05: \"Select string\" per usilicrc's own K-32 "
                           "documentation, confirmed against real legacy-usilic PTY behavior)") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte(0x13, std::nullopt));
  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
}

// -- feedRegexListByte: legacy's own EGREP/CUT-regex list is a live
// combobox, not a pure picker (2026-08-05 user report: "新たな文字列は
// 入力できないし既存の文字列編集も受け付けない") -- see its own
// definition comment for the design (a curated set of navigation
// bytes stays list-only, everything else transitions into an editable
// prompt pre-filled with the highlighted candidate).

TEST_CASE_FIXTURE(Fixture, "feedRegexListByte: ^N/^P still navigate the EGREP candidate list without "
                           "leaving it (regression check: these must stay list-only, not trigger the "
                           "edit-transition)") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentEgrepPattern, "one");
  history.pushRecent(RingCategory::RecentEgrepPattern, "two");
  history.pushRecent(RingCategory::RecentEgrepPattern, "three");
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  // Cursor starts on the TOP (oldest) entry for this direct-entry path
  // (2026-08-11 fix -- see promptHistoryRecallIsDirectEntry_'s own
  // comment: legacy's mode.c distinguishes M_fgrep/M_locate/M_cut2's
  // direct entry, defaulting to the top, from a real ^R recall's own
  // T_hfgrep/T_hegrep, which starts on the newest/bottom entry
  // instead), so ^N (down) is the one guaranteed not to be a no-op
  // boundary bounce here; ^P (up) is tried second, back to the
  // starting position.
  int startIndex = app.activeContentView().cursorIndex();

  CHECK(app.feedByte(0x0e, std::nullopt));  // ^N
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK(app.activeContentView().cursorIndex() != startIndex);

  CHECK(app.feedByte(0x10, std::nullopt));  // ^P
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK(app.activeContentView().cursorIndex() == startIndex);
}

// 2026-08-28 fix, a direct user report ("CLAUDE.md上で^J^S→モードライン
// に戻り値がない"): listBrowserView_'s own ModeLineFieldsProvider only
// ever showed a trailing2 destination when grepReturnMode_ was
// Directory -- the exact same narrow special-case drawPromptLabel()'s
// own destinationMode logic had already been corrected away from (see
// its own comment: "Restricting to 'only when the destination is
// Directory' was equally arbitrary"), just never mirrored to this read-
// only candidate-list screen. previousModeName(File) already returns
// the real file path generically, so reusing it here fixes this without
// a new File-specific branch.
TEST_CASE_FIXTURE(Fixture, "EGREP direct-entry candidate list: shows a trailing2 destination even when "
                           "returning to FILE mode, not just Directory") {
  history.pushRecent(RingCategory::RecentEgrepPattern, "one");
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  REQUIRE(app.feedByte(0x13, std::nullopt));  // ^S -> EGREP direct entry
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  // Tail-truncated to 16 chars by truncatedTrailingPath() when longer
  // (the fixture's own temp path always is) -- "plain.txt" itself, the
  // last 9 chars, is always within that tail regardless of the full
  // path's own length.
  CHECK(display.rowText(23).find("plain.txt") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "startPromptHistoryRecall (2026-08-12 fix): the EGREP/CUT candidate list's own "
                           "visible caret sits at the END of the mode line's own trailing field (the "
                           "candidate text shown there), not at the highlighted content row -- a direct "
                           "user correction: \"『候補一覧のハイライト行末尾へ正しくカーソルを移動する設計』"
                           "が有効なのはハイライト行を選択する場合の話で文字入力はモードラインで行われる"
                           "ので最下行の文字列末尾にないとおかしい\" (that design is right for SELECTING a "
                           "row, but text input happens in the mode line, so the caret belongs there), "
                           "independently confirmed by a real macOS report that ATOK's own input-mode icon "
                           "(drawn under the system's actual text caret) was appearing over the top content "
                           "row instead") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentEgrepPattern, "one");
  history.pushRecent(RingCategory::RecentEgrepPattern, "two");
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK(display.cursorRow() == 23);  // the mode line (24-row fixture), NOT row 0 (the highlighted candidate)
  CHECK_FALSE(display.visibleCursorHidden());

  // A plain FILE/DIR/PROGRAM-command picker (Enter simply selects, no
  // live-editing paradigm) must NOT get this treatment -- regression
  // check for the same code path. ^G here goes straight to Directory
  // (2026-08-12 fix: EGREP's own candidate list no longer resumes a
  // blank prompt on ^G -- see returnFromListBrowser()'s own comment),
  // so a single ^G is enough to get back to Directory to start a
  // DIFFERENT list from.
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);

  history.pushRecent(RingCategory::ShellCommand, "echo hi");
  REQUIRE(app.handleCommand(Command::StartCommandHistory));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK(display.cursorRow() == 0);
}

TEST_CASE_FIXTURE(Fixture, "returnFromListBrowser (2026-08-12 fix): ^G from the candidate list reached via "
                           "item 275's own LIFO return from Grep results clears the STALE \"EGREP: pattern\" "
                           "search annotation too, landing on the TRUE plain (ls) the whole excursion "
                           "started from -- a direct user correction: \"戻り値が(ls)ならちゃんとそこ"
                           "（(ls)でEGREPしたので当然(ls)のはず）へ戻ってほしい\". Before this fix, "
                           "currentMode_ was correctly restored to Directory, but the annotation set by the "
                           "earlier openSelectedGrepMatch() lingered, so the screen still read as \"just "
                           "picked a match\" instead of a genuinely fresh (ls)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartEgrep));  // ^J^S
  REQUIRE(app.feedByte(0x15, std::nullopt));         // ^U: start editing, clear the seed
  feedText("plain");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // submit -- search from the candidate list
  REQUIRE(app.currentMode() == ApplicationMode::Grep);
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // select the match
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.directoryMode().hasSearchAnnotation());  // "EGREP: plain" now showing

  REQUIRE(app.handleCommand(Command::Back));  // ^G #1: Directory+annotation -> Grep results
  REQUIRE(app.currentMode() == ApplicationMode::Grep);
  REQUIRE(app.handleCommand(Command::Back));  // ^G #2: results -> candidate list (LIFO, item 275)
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  REQUIRE(app.handleCommand(Command::Back));  // ^G #3: candidate list -> should be TRUE plain (ls)
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK_FALSE(app.directoryMode().hasSearchAnnotation());  // NOT still showing "EGREP: plain"
}

// 2026-08-22 fix, item 405 -- docs/HISTORY.md:10208, a direct user
// report: "（ls）のCLAUDE.md上で^J^B→...引き続き^G→...（ls）からCUTの
// 正規表現検索文字列一覧モードを起動してるんで戻り値の通り（ls）へ戻
// らないと". returnFromListBrowser()'s 2026-08-12 "^G from the direct-
// entry candidate list returns straight to where it was invoked from"
// fix was scoped to PromptKind::GrepPattern only at the time, with a
// note deferring CutPattern "for now" since CUT hadn't been exercised
// enough yet to know if the same gap mattered there. It does: CUT's
// own ^J^B/^J^C open into the exact same kind of direct-entry candidate
// list via startRegexPromptList(PromptKind::CutPattern) (see
// startCutFilePrompt()/startCutPrompt()'s own calls), so ^G with
// nothing typed/selected fell through to the same "reopen a blank
// editable prompt" fallback GrepPattern used to, instead of landing
// back on the plain Directory listing the search was launched from.
TEST_CASE_FIXTURE(Fixture, "returnFromListBrowser (2026-08-22 fix, item 405): ^G from CUT's own regex "
                           "direct-entry candidate list (^J^B on a selected file), with nothing typed or "
                           "selected, returns straight to the Directory listing it was invoked from -- "
                           "not a reopened blank CUT prompt") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::StartCutFileRegex));  // ^J^B on log.txt, cursor still in Directory
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK_FALSE(app.promptActive());  // direct entry into the list itself, not a live editable prompt yet

  REQUIRE(app.handleCommand(Command::Back));  // ^G, nothing typed/selected
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK_FALSE(app.promptActive());  // NOT a reopened blank CUT prompt
  CHECK_FALSE(app.directoryMode().hasSearchAnnotation());
}

TEST_CASE_FIXTURE(Fixture, "listBrowserView_'s own ModeLineFieldsProvider (2026-08-12 fix): the truncated "
                           "destination-directory field is right-justified to the SCREEN's own edge, not "
                           "drawn immediately after the candidate-preview text's own gap -- a real report + "
                           "reference screenshot (=ac): \"戻り値自体はいいけど位置が右端じゃない\" (the "
                           "value itself is fine, but the position isn't at the right edge) -- "
                           "drawPromptLabel()'s own matching field was already right-justified via an "
                           "explicit moveCursor(), this provider's own trailing2 wasn't") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentEgrepPattern, "x");
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  std::string row = display.rowText(23);
  std::string dir = app.directoryMode().currentDirectory();
  std::string expectedTail = dir;
  // Still the usual 16 here (unlike drawPromptLabel()'s own field,
  // 2026-08-28 fix): this is listBrowserView_'s own SEPARATE
  // ModeLineFieldsProvider, rendered through renderModeLine()'s own
  // budget-aware truncateFieldByKind() pipeline (unaffected by that
  // fix, which only touches drawPromptLabel()'s hand-rolled column math).
  if (expectedTail.size() > 16) expectedTail = "..." + expectedTail.substr(expectedTail.size() - 16);
  // Ends at the VERY LAST column, not somewhere in the middle of the row.
  CHECK(row.substr(row.size() - expectedTail.size()) == expectedTail);
  CHECK(row.back() == expectedTail.back());
}

TEST_CASE_FIXTURE(Fixture, "feedRegexListByte: the candidate list's own \"NN/MM TOP/END\" position stays "
                           "visible in the mode line once editing starts (2026-08-12 fix: a direct user "
                           "correction -- \"[bs]→「1/10 TOP」が消滅\", i.e. backspacing an existing "
                           "candidate straight into editing used to drop this the instant editing began, "
                           "even though it had been on screen the whole time the list was just being "
                           "browsed)") {
  REQUIRE(app.start(path()));
  // 10 entries, matching the double-digit "01/10" zero-padding real
  // screenshots showed (ModeLine::format() zero-pads current to
  // total's own digit width -- 2 entries would format as "1/2", not
  // "01/02").
  for (int i = 0; i < 10; i++) history.pushRecent(RingCategory::RecentEgrepPattern, "pattern" + std::to_string(i));
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  CHECK(app.feedByte(0x7f, std::nullopt));  // backspace: transitions into editing
  CHECK(app.promptActive());
  // "01/10 TOP" -- cursor started on the top (oldest) direct-entry
  // candidate, per promptHistoryRecallIsDirectEntry_'s own comment.
  CHECK(display.rowText(23).find("01/10 TOP") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "activatePrompt (2026-08-12 fix): the candidate's own text lands at the EXACT "
                           "same column once editing starts as it sat at while just browsing -- a real "
                           "report + reference screenshot found it shifted one column to the right (an "
                           "earlier version of the positionText fix above double-counted the gap after the "
                           "position field: one increment for the space actually drawn before it, another "
                           "+1 folded into the length-plus-gap increment after it, six vs. the seven the "
                           "listBrowserView_-provider math above actually produces)") {
  REQUIRE(app.start(path()));
  for (int i = 0; i < 10; i++) history.pushRecent(RingCategory::RecentEgrepPattern, "pattern" + std::to_string(i));
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  std::string browsingRow = display.rowText(23);
  std::size_t browsingCol = browsingRow.find("pattern0");
  REQUIRE(browsingCol != std::string::npos);

  // ^U doesn't move the cursor before starting to edit (unlike ^A/^E/
  // ^B/^F, which would confound this column comparison), so the
  // freshly-drawn seed text's own STARTING column is directly
  // comparable to where it sat while browsing.
  REQUIRE(app.feedByte(0x15, std::nullopt));  // ^U: starts editing, clears the seed
  REQUIRE(app.promptActive());
  // The field is now empty (^U cleared it), so compare the CURSOR's
  // own column instead -- it sits right where "pattern0" itself
  // started (the field's own col0_), which must match browsingCol.
  CHECK(display.cursorCol() == static_cast<int>(browsingCol));
}

TEST_CASE_FIXTURE(Fixture, "feedPromptByte: ^N/^P keep cycling through the STILL-VISIBLE candidate list "
                           "even after editing has started, re-seeding the prompt with each newly-"
                           "highlighted candidate's own text -- 2026-08-12 fix, a direct user report: "
                           "typing a character correctly starts editing, but \"^N^Pが利かなくなる（他の"
                           "ハイライト文字列へ移れない）\" (^N/^P stop working -- can't move to another "
                           "highlighted string)") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentEgrepPattern, "aaa");
  history.pushRecent(RingCategory::RecentEgrepPattern, "bbb");
  history.pushRecent(RingCategory::RecentEgrepPattern, "ccc");
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  std::string first = *app.listBrowserMode().selectedItem();

  CHECK(app.feedByte('X', std::nullopt));  // starts editing, appends 'X'
  REQUIRE(app.promptActive());
  CHECK(app.promptText() == first + "X");

  REQUIRE(app.feedByte(0x0e, std::nullopt));  // ^N: cycle to the next candidate
  CHECK(app.promptActive());                  // still editing -- ^N didn't cancel/submit
  std::string second = *app.listBrowserMode().selectedItem();
  CHECK(second != first);
  // Re-seeded with the NEW candidate's own text, not "first+X" with
  // anything appended/replaced -- the in-progress "X" edit is
  // deliberately discarded (matching a combobox's own "cycle through
  // the ORIGINAL suggestions" semantics).
  CHECK(app.promptText() == second);

  REQUIRE(app.feedByte(0x10, std::nullopt));  // ^P: cycle back
  CHECK(app.promptText() == first);
}

TEST_CASE_FIXTURE(Fixture, "feedRegexListByte: typing a character while browsing the EGREP candidate list "
                           "transitions into an editable prompt PRE-FILLED with the highlighted candidate's "
                           "own text, with the typed character appended -- this IS \"editing an existing "
                           "string\", not starting from blank") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentEgrepPattern, "one");
  history.pushRecent(RingCategory::RecentEgrepPattern, "two");
  history.pushRecent(RingCategory::RecentEgrepPattern, "three");
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  std::string highlighted = *app.listBrowserMode().selectedItem();

  CHECK(app.feedByte('X', std::nullopt));
  CHECK(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.promptText() == highlighted + "X");
}

// 2026-08-15 addition: a real user report ("^J^M→「No String」はいい
// けどモードラインに入力できない") -- key.c's own input_key() groups
// M_man2 (K-44's own list-editing case) in the exact same switch case
// as M_cut2/M_locate/T_locate/M_fgrep, confirming MAN's "Select manual
// page" list is ALSO meant to be a live combobox like GREP/CUT's own,
// not a plain navigate-only picker. Mirrors the EGREP test just above
// exactly, for PromptKind::ManName instead.
TEST_CASE_FIXTURE(Fixture, "feedRegexListByte: typing a character while browsing the MAN candidate list "
                           "also transitions into an editable prompt, matching EGREP/CUT") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentManPage, "one");
  history.pushRecent(RingCategory::RecentManPage, "two");
  history.pushRecent(RingCategory::RecentManPage, "three");
  REQUIRE(app.handleCommand(Command::StartManList));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  std::string highlighted = *app.listBrowserMode().selectedItem();

  CHECK(app.feedByte('X', std::nullopt));
  CHECK(app.promptActive());
  CHECK(app.promptText() == highlighted + "X");
}

TEST_CASE_FIXTURE(Fixture, "feedRegexListByte: backspace while browsing the EGREP candidate list also "
                           "transitions into editing, applied to the highlighted candidate's own text") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentEgrepPattern, "one");
  history.pushRecent(RingCategory::RecentEgrepPattern, "two");
  history.pushRecent(RingCategory::RecentEgrepPattern, "three");
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  std::string highlighted = *app.listBrowserMode().selectedItem();
  REQUIRE_FALSE(highlighted.empty());

  CHECK(app.feedByte(0x7f, std::nullopt));  // DEL
  CHECK(app.promptActive());
  CHECK(app.promptText() == highlighted.substr(0, highlighted.size() - 1));
}

TEST_CASE_FIXTURE(Fixture, "feedRegexListByte: Enter still submits the highlighted candidate as-is "
                           "(regression check: the edit-transition must not interfere with plain "
                           "pick-from-the-list use)") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentEgrepPattern, "one");
  history.pushRecent(RingCategory::RecentEgrepPattern, "two");
  history.pushRecent(RingCategory::RecentEgrepPattern, "three");
  REQUIRE(app.handleCommand(Command::StartEgrep));
  std::string highlighted = *app.listBrowserMode().selectedItem();

  REQUIRE(app.feedByte(0x0d, std::nullopt));  // Enter
  CHECK(app.currentMode() == ApplicationMode::Grep);
  CHECK_FALSE(app.promptActive());
  const auto &ring = history.recentEntries(RingCategory::RecentEgrepPattern);
  CHECK(ring.back() == highlighted);
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

TEST_CASE_FIXTURE(Fixture, "activatePrompt (2026-08-12 fix): the editable field's width reserves COLUMNS "
                           "- (mode name + 1) - (16 + 3 + 1) for a right-justified, \"...\"-truncated "
                           "trailing path field, matching a direct user formula") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));  // label "FGREP", 5 chars
  // labelWidth = 5+1 = 6, trailingReserve = 16+3+1 = 20, so the field
  // spans columns [6, 80-20) = [6, 60) -- none of the typed 'x' run
  // should reach column 60 or beyond, regardless of what (if anything)
  // the reserved trailing area itself shows.
  std::string typed(60, 'x');  // long enough to reach right up to the boundary if unreserved
  feedText(typed);
  const std::string &row = display.rowText(23);
  for (int col = 60; col < 80; col++) {
    CAPTURE(col);
    CHECK(row[col] != 'x');
  }
}

TEST_CASE_FIXTURE(Fixture, "activatePrompt (2026-08-12 fix): the reserved trailing area actually shows the "
                           "destination directory (right-justified, \"...\"-truncated to its last 15 "
                           "characters when longer) for FGREP/EGREP/CUT prompts returning to Directory mode "
                           "-- a real report (\"FGREPで戻り先が無い\", and for EGREP, the destination "
                           "vanishing the instant editing starts) found this space left blank before") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  std::string dir = app.directoryMode().currentDirectory();
  std::string expectedTail = dir;
  // 15, not the usual 16 (2026-08-28 fix): drawPromptLabel() reserves
  // one fewer tail character here specifically, to leave room for both
  // the mandatory gap after the editable field AND the "never write the
  // true last column" rule within its own fixed trailingReserve budget
  // -- see its own comment.
  if (expectedTail.size() > 16) expectedTail = "..." + expectedTail.substr(expectedTail.size() - 15);
  const std::string &row = display.rowText(23);
  // Ends 1 column short of the true right edge, not AT it (2026-08-18
  // auto-wrap fix, see drawPromptLabel()'s own comment) -- the true
  // last column is deliberately left non-Standout.
  CHECK(row.substr(row.size() - 1 - expectedTail.size(), expectedTail.size()) == expectedTail);
  CHECK(display.standoutAt(23, static_cast<int>(row.size()) - 2));  // shown reversed, like every other field
  CHECK_FALSE(display.standoutAt(23, static_cast<int>(row.size()) - 1));  // true last column: never Standout
}

// 2026-08-13 (CLAUDE.md history item 295): the 2026-08-12 fix above
// was scoped to GrepPattern/CutPattern only -- a follow-up report
// (LOCATE's own prompt, =ap: "モードラインにモード名しか無い") showed
// this same "reserved but blank" gap for every OTHER PromptKind with
// its own return-mode variable. Generalized via each PromptKind's own
// dedicated *ReturnMode_ variable instead of naming two of them.
TEST_CASE_FIXTURE(Fixture, "drawPromptLabel (2026-08-13 generalization): LOCATE/MAN prompts returning to "
                           "Directory mode also show the destination directory, not just GREP/CUT") {
  REQUIRE(app.start(path()));
  std::string dir = app.directoryMode().currentDirectory();
  std::string expectedTail = dir;
  // 15, not the usual 16 (2026-08-28 fix): drawPromptLabel() reserves
  // one fewer tail character here specifically, to leave room for both
  // the mandatory gap after the editable field AND the "never write the
  // true last column" rule within its own fixed trailingReserve budget
  // -- see its own comment.
  if (expectedTail.size() > 16) expectedTail = "..." + expectedTail.substr(expectedTail.size() - 15);

  REQUIRE(app.handleCommand(Command::StartLocate));
  {
    const std::string &row = display.rowText(23);
    // 1 column short of the true edge -- see drawPromptLabel()'s own
    // 2026-08-18 auto-wrap-avoidance comment.
    CHECK(row.substr(row.size() - 1 - expectedTail.size(), expectedTail.size()) == expectedTail);
  }
  REQUIRE(app.feedByte(0x07, std::nullopt));  // ^G cancels the prompt (feedPromptByte's own path, not Command::Back)

  REQUIRE(app.handleCommand(Command::StartMan));
  {
    const std::string &row = display.rowText(23);
    CHECK(row.substr(row.size() - 1 - expectedTail.size(), expectedTail.size()) == expectedTail);
  }
}

// 2026-08-13 (CLAUDE.md history item 294): a follow-up report --
// "モードライン共有ルーチンはできない？" -- found runLocateUpdateDb()'s
// own FIRST version hand-rolled its own copy of drawPromptLabel()'s
// label-drawing logic instead of reusing it (item 291's whole point
// was eliminating exactly this kind of one-off duplication). Now
// routed through drawPromptLabel()'s own new staticMessage parameter.
// 2026-08-14 (CLAUDE.md history item 299): the THIRD redesign of this
// call in one day -- see runLocateUpdateDb()'s own comment for why the
// first two (a blocking locateMode_.updateDb() call, then an unbounded
// poll()-in-one-call loop) each broke down, the second specifically in
// -g (a real report: "④の^[不具合は-gで起きる模様。^Jl後の^Gも受け
// 付けない"). runLocateUpdateDb() itself is now NON-BLOCKING: it
// launches the detached command and returns immediately, leaving
// awaitingLocateUpdateDb() true until either ^G arrives (through the
// normal feedByte() path) or pollLocateUpdateDbCompletion() (called
// every run-loop tick by app/main.cpp, both backends) notices the
// child has exited.
TEST_CASE_FIXTURE(Fixture, "LOCATE: ^Jl draws \"LOCATE \" + underlined \"Updating database...\" via "
                           "drawPromptLabel()'s own shared staticMessage path, not a separate one-off") {
  configuration.setCommandForTest(ConfiguredRole::UpdateDb, {{"true"}, std::nullopt});
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte('l', std::nullopt));
  // Returns immediately, BEFORE `true` has necessarily even exited --
  // this only asserts the "now awaiting" state (a real prompt hasn't
  // started yet); the message's own rendering was verified via a live
  // PTY reproduction (see CLAUDE.md history item 294).
  CHECK(app.awaitingLocateUpdateDb());
  CHECK_FALSE(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "LOCATE: pollLocateUpdateDbCompletion() chains into the LOCATE prompt once "
                           "the detached updatedb actually exits") {
  configuration.setCommandForTest(ConfiguredRole::UpdateDb, {{"true"}, std::nullopt});
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  app.feedByte('l', std::nullopt);
  REQUIRE(app.awaitingLocateUpdateDb());
  // `true` exits almost instantly, but exactly when is a real race --
  // poll a few times rather than asserting after exactly one call.
  for (int i = 0; i < 200 && app.awaitingLocateUpdateDb(); i++) {
    app.pollLocateUpdateDbCompletion();
    if (app.awaitingLocateUpdateDb()) usleep(1000);
  }
  CHECK_FALSE(app.awaitingLocateUpdateDb());
  CHECK(app.promptActive());
}

// 2026-08-14 (CLAUDE.md history item 298/299's follow-up, "常時^Gで...
// 移行"): ^G cancels out of a still-RUNNING updatedb at any point --
// arrives as an ordinary byte through feedByte(), handled by
// dispatchByte()'s own top-level awaitingLocateUpdateDb() gate, so no
// polling or real elapsed time is needed to prove this: the command
// doesn't even need to be slow anymore (unlike the previous, now-
// removed poll()-in-one-call design), since runLocateUpdateDb() itself
// no longer blocks waiting for it.
TEST_CASE_FIXTURE(Fixture, "feedByte: ^J l, ^G cancels a still-running updatedb and merges into ^J^L") {
  configuration.setCommandForTest(ConfiguredRole::UpdateDb, {{"sleep", "2"}, std::nullopt});
  history.pushRecent(RingCategory::RecentLocateQuery, "previous-query");
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  app.feedByte('l', std::nullopt);
  REQUIRE(app.awaitingLocateUpdateDb());
  CHECK(app.feedByte(0x07, std::nullopt));  // ^G
  CHECK_FALSE(app.awaitingLocateUpdateDb());
  CHECK(app.promptActive());
  CHECK(app.promptText() == "previous-query");  // ^J^L's own seeding, not a blank prompt
}

TEST_CASE_FIXTURE(Fixture, "feedByte: while awaiting updatedb, every byte except ^G is ignored") {
  configuration.setCommandForTest(ConfiguredRole::UpdateDb, {{"sleep", "2"}, std::nullopt});
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  app.feedByte('l', std::nullopt);
  REQUIRE(app.awaitingLocateUpdateDb());
  CHECK_FALSE(app.feedByte('x', std::nullopt));
  CHECK(app.awaitingLocateUpdateDb());  // unaffected -- still waiting
}

TEST_CASE_FIXTURE(Fixture, "feedPromptByte: ^A/^E/^B/^F move the cursor (beginning/end/back/forward) "
                           "instead of inserting literally -- 2026-08-12 fix, a real report: \"^A^E^B^F等"
                           "文字列編集のためのキーが利かずハイライトAEBFとなる\"") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("abc");
  REQUIRE(app.promptText() == "abc");

  CHECK(app.feedByte(0x01, std::nullopt));  // ^A: home
  feedText("X");
  CHECK(app.promptText() == "Xabc");

  CHECK(app.feedByte(0x05, std::nullopt));  // ^E: end
  feedText("Y");
  CHECK(app.promptText() == "XabcY");

  CHECK(app.feedByte(0x02, std::nullopt));  // ^B: back one char (now before the trailing 'Y')
  feedText("Z");
  CHECK(app.promptText() == "XabcZY");

  CHECK(app.feedByte(0x06, std::nullopt));  // ^F: forward one char (now after 'Y', i.e. at the end)
  feedText("!");
  CHECK(app.promptText() == "XabcZY!");
}

TEST_CASE_FIXTURE(Fixture, "feedPromptByte: ^D deletes forward -- the readline-family counterpart to "
                           "^A/^E/^B/^F above (2026-08-12 fix, a real CUT report: \"^A→^Dが利かない\", "
                           "found while trying ^A then ^D from the ^J^C candidate list)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("abc");
  REQUIRE(app.feedByte(0x01, std::nullopt));  // ^A: home
  CHECK(app.feedByte(0x04, std::nullopt));    // ^D: delete forward
  CHECK(app.promptText() == "bc");
}

TEST_CASE_FIXTURE(Fixture, "feedPromptByte: ^A/^E/^B/^F themselves can still be typed LITERALLY via ^Q, "
                           "matching the same quoted-insert escape hatch as any other control byte") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  CHECK(app.feedByte(0x11, std::nullopt));  // ^Q
  CHECK(app.feedByte(0x01, std::nullopt));  // quoted ^A: inserted literally, cursor NOT moved
  CHECK(app.promptText() == std::string("\x01"));
}

TEST_CASE_FIXTURE(Fixture, "feedPromptByte: an unrecognized, UNQUOTED control byte is rejected (BEL), not "
                           "inserted literally -- 2026-08-12 fix, a direct user instruction: \"コントロール"
                           "コード入力は^Qの前置が必須、を反映して\". A real report: a stray Tab (0x09, "
                           "meaningful only for PathInput's own completion) typed into an EGREP pattern "
                           "corrupted it with a literal control byte, rendered as reversed \"IIII\"-style "
                           "glyphs, instead of being rejected or requiring ^Q") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("ab");
  CHECK(app.feedByte(0x09, std::nullopt));  // Tab: not PathInput, so rejected outright
  CHECK(app.promptText() == "ab");          // unchanged -- NOT "ab\t"
  CHECK(app.promptActive());                // still editing, not cancelled/submitted
}

TEST_CASE_FIXTURE(Fixture, "grepView_'s empty-result placeholder reads \"<pattern>: No String\", not "
                           "ContentView's generic \"--\" -- and shows plain (not full reverse-video), with "
                           "ONLY \"No String\" underlined (2026-08-12, two rounds of user correction: first "
                           "\"EGREPはファイルの切り分けではないので「--」ではなくて「No String」\" (EGREP "
                           "isn't CUT's own region-splitting, so its empty placeholder shouldn't reuse "
                           "CUT's \"--\" marker), then \"ハイライトだと選択可能文字列と紛らわしいので"
                           "アンダーライン\" + \"マッチしないと検索対象文字列情報が失われるので「abc: No "
                           "String」かつ「No String」のみアンダーライン\" (full reverse-video reads as "
                           "\"selectable\", which this isn't -- underline instead; and losing which "
                           "pattern was searched is its own problem, so show it with only the \"No "
                           "String\" suffix underlined)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("this-pattern-matches-nothing-at-all");
  // A zero-match search is still a SUCCESSFUL search (GrepMode::search()'s
  // own comment: "exit 1 = ran fine but found none -- both are a
  // successful search"), so this enters Grep mode with an empty
  // matches_ list, not a rejected/failed submission.
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // ^M
  REQUIRE(app.currentMode() == ApplicationMode::Grep);
  REQUIRE(app.grepMode().matches().empty());
  std::string expected = "this-pattern-matches-nothing-at-all: No String";
  CHECK(display.rowText(0).substr(0, expected.size()) == expected);
  // Not fully reverse-video: the pattern prefix's own first cell must
  // NOT be in standout.
  CHECK_FALSE(display.standoutAt(0, 0));
  // "No String" itself is underlined, the "<pattern>: " prefix is not.
  int underlineFrom = static_cast<int>(expected.size() - std::string("No String").size());
  CHECK_FALSE(display.underlineAt(0, 0));
  CHECK(display.underlineAt(0, underlineFrom));
  CHECK(display.underlineAt(0, static_cast<int>(expected.size()) - 1));
}

TEST_CASE_FIXTURE(Fixture, "startRegexPromptList (^J^S/^J^C, 2026-08-12 fix): the direct-entry candidate "
                           "list is sorted + de-duplicated, not shown in raw load/chronological order -- a "
                           "direct user request (avoiding future duplicate growth as usilicrc's own GREP "
                           "section is edited over time), scoped ONLY to this direct-entry list, not to a "
                           "real ^R recall while typing (that case was already deliberately corrected to "
                           "stay raw/chronological earlier, backed by a real legacy screenshot)") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentEgrepPattern, "zzz");
  history.pushRecent(RingCategory::RecentEgrepPattern, "aaa");
  history.pushRecent(RingCategory::RecentEgrepPattern, "aaa");  // duplicate
  history.pushRecent(RingCategory::RecentEgrepPattern, "mmm");
  REQUIRE(app.handleCommand(Command::StartEgrep));  // ^J^S: direct-entry candidate list
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  const auto &items = app.listBrowserMode().items();
  std::vector<std::string> sorted(items.begin(), items.end());
  CHECK(std::is_sorted(sorted.begin(), sorted.end()));
  CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());  // no consecutive duplicates
  // A genuinely REAL ^R recall (not direct entry) still shows raw
  // chronological order, duplicate "aaa" included -- unaffected by
  // this fix. Reached by typing a character right here (the SAME
  // open list's own edit-transition, matching how legacy's own
  // T_hegrep real-recall is only ever reachable from an ALREADY-
  // active EGREP prompt -- ^J^S has no separate "start blank" entry
  // point of its own to invoke instead).
  REQUIRE(app.feedByte('X', std::nullopt));
  REQUIRE(app.promptActive());
  REQUIRE(app.feedByte(0x12, std::nullopt));  // ^R
  CHECK(app.listBrowserMode().items().back() == "mmm");  // newest at the bottom, chronological
}

TEST_CASE_FIXTURE(Fixture, "ensureShellActive (2026-08-12 fix): jumping into Shell mode from a still-"
                           "in-progress ^J^S/^J^C candidate-list recall (via ^J^W, not that recall's own "
                           "^G/Enter/edit-start consumption path) no longer leaves promptHistoryRecallKind_ "
                           "stale -- a real report: start, ^J^S, ^J^W, run a shell command, then ^R at the "
                           "live SHELL prompt showed the ListBrowser mode line reading \"EGREP\" instead of "
                           "\"PROGRAM\", and selecting an entry ran an EGREP search using the shell command "
                           "text as the pattern instead of inserting it into the command line") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartEgrep));  // ^J^S
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  bool ok = app.feedByte(0x0a, std::nullopt);  // ^J
  ok = app.feedByte(0x17, std::nullopt) && ok;  // ^W -- shellInsertBufferContent()
  if (!ok) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Shell);
  // Matches the report's own "|sort" [return] step: a real shell
  // command actually having been run (pushed into RingCategory::
  // ShellCommand) is what makes the later ^R meaningful at all. The
  // only entry in this ring in this test, so it's also the only (and
  // therefore already-selected) item in the ^R list below.
  history.pushRecent(RingCategory::ShellCommand, "echo shell-history-marker");

  REQUIRE(app.feedByte(0x12, std::nullopt));  // ^R at the live SHELL prompt
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.listBrowserMode().items() == std::vector<std::string>{"echo shell-history-marker"});

  REQUIRE(app.feedByte(0x0d, std::nullopt));  // Enter: select it
  // openSelectedListEntry() checks promptHistoryRecallKind_ BEFORE
  // shellHistoryRecallActive_ -- the bug had the former still stale
  // (== GrepPattern), so this used to run executeGrep("echo shell-
  // history-marker") instead (currentMode() would come back as Grep,
  // not Shell, and the shell command line would be untouched).
  CHECK(app.currentMode() == ApplicationMode::Shell);
  CHECK(app.shell().commandLine().text() == "echo shell-history-marker");
}

// 2026-08-05: feedPromptByte() (every prompt kind routed through it --
// MAN/LOCATE/FGREP/EGREP/CUT/PROGRAM/PathInput/Copy/Move/Remove/.../
// DuPath/ArchiveDestination) fell through to promptEditor_.insertByte()
// for EVERY byte it didn't specifically recognize, including DEL(0x7f)/
// ^H(0x08) -- there was no backspace/correction of any kind, the raw
// control byte just got inserted as a literal unprintable character. A
// real user report found this while typing a CUT pattern. feedShellByte()
// already had this same backspace()/redraw() pairing for its own
// LineEditor (see its own comment) -- promptEditor_ is the same
// LineEditor class and already exposed backspace(), this generic prompt
// path was just never wired to call it.
TEST_CASE_FIXTURE(Fixture, "feedByte: DEL/^H backspaces the prompt text before it's submitted "
                           "(2026-08-05 fix: every prompt kind was previously missing this entirely)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain.txX");
  CHECK(app.feedByte(0x7f, std::nullopt));  // DEL: removes the trailing 'X'
  feedText("t");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // ^M submits "plain.txt"

  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Grep);
  REQUIRE(app.grepMode().matches().size() == 1);
  CHECK(app.grepMode().matches()[0].text.find("plain.txt") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^H (0x08) also backspaces the prompt text, same as DEL") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("hellZ");
  CHECK(app.feedByte(0x08, std::nullopt));  // ^H: removes the trailing 'Z'
  feedText("o");
  REQUIRE(app.feedByte(0x0d, std::nullopt));

  CHECK_FALSE(app.promptActive());
  const auto &fgrepRing = history.recentEntries(RingCategory::RecentFgrepPattern);
  REQUIRE(fgrepRing.size() == 1);
  CHECK(fgrepRing[0] == "hello");
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

TEST_CASE_FIXTURE(Fixture, "GREP: Enter on a match returns to Directory mode with the cursor on the "
                           "matched entry and a \"FGREP: pattern\" mode-line annotation (2026-08-11 fix: "
                           "a real report found Enter did nothing at all here)") {
  REQUIRE(app.start(path()));
  int expectedIdx = indexOf("plain.txt");
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);

  CHECK(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.activeContentView().cursorIndex() == expectedIdx);
  CHECK(app.activeContentView().modeLineText().find("FGREP: plain.txt") != std::string::npos);
}

// 2026-08-24 addition (docs/CHANGES.md item 418, a direct user
// request: "LOCATE、FGREP等「.mp4」マッチ一覧上でも可能で"): a match
// here already corresponds to a real directory entry, so an
// EXEC-mapped suffix should fire on this SAME Enter, not require a
// second one after landing back on the (ls) row. Uses the same
// harmless "sh -c echo >> marker" EXEC trick as the Directory-mode
// EXEC tests just below (see their own section header).
TEST_CASE_FIXTURE(Fixture, "GREP: Enter on a match for an EXEC-mapped suffix runs EXEC immediately, in "
                           "addition to the usual jump-back-to-(ls) landing") {
  configuration.loadRcText("EXEC\n .marker sh -c 'echo EXECRAN >> \"$0\"'\n", true);
  { std::ofstream(root / "target.marker") << "original\n"; }
  REQUIRE(app.start(path()));
  int expectedIdx = indexOf("target.marker");
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("target.marker");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);

  CHECK(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Directory);  // still just lands back on (ls), same as before
  CHECK(app.activeContentView().cursorIndex() == expectedIdx);

  std::ifstream in(root / "target.marker");
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(content.find("EXECRAN") != std::string::npos);  // AND EXEC actually ran
}

TEST_CASE_FIXTURE(Fixture, "GREP: Back from the \"FGREP: pattern\"-annotated Directory listing returns to "
                           "the still-intact grep results, even with an empty ModeStack (2026-08-11 fix: "
                           "a real report found ^G did nothing here since there was no directory to pop "
                           "back to)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE_FALSE(app.directoryMode().canGoBack());  // no subdirectory was ever entered

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Grep);
  REQUIRE(app.grepMode().matches().size() == 1);  // the original search results, not re-run
  CHECK(app.grepMode().matches()[0].text.find("plain.txt") != std::string::npos);
}

// 2026-08-28 fix, a direct user report + precise repro ("CLAUDE.mdを
// 開いた状態で^J^S...[return]で最上行を選択→戻り値が「~/git/usilic/」
// のまま...これでは^Gで（ls）に戻ってしまう。検索結果一覧へ戻るのが
// 筋なので戻り値は「EGREP: ^-」であるべき"), refined by a same-day
// follow-up with exact before/after mode-line values ("EGREP: ^-"(不良,
// on the LANDED-on-match screen specifically) vs "- レガシーC実装...
// src/config.sh"(正解) -- the match's own line text, not the search
// label, since the label is redundant once you're actually looking at
// the match; the label belongs on the RESULTS-list screen instead, see
// the grepView_ trailing3 test elsewhere): the File-mode counterpart to
// the Directory-mode test just above -- before this fix,
// openSelectedGrepMatch()'s special handling (annotation + routing ^G
// back to the results list) only existed for the Directory case, so a
// match reached via a File-mode-originated search landed back on the
// file with its PLAIN mode line (no annotation) and Back went straight
// to returnToDirectory() (the (ls) listing), skipping the results
// screen the Directory case already preserves correctly.
TEST_CASE_FIXTURE(Fixture, "GREP: Enter on a match (search run from File mode) returns to the file with the "
                           "MATCHED LINE'S OWN TEXT as trailing2 (name unchanged), and Back returns to the "
                           "grep results instead of the directory listing") {
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
  std::string matchedText = app.grepMode().matches()[0].text;

  CHECK(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / "plain.txt").string());  // the file's own name, unchanged
  CHECK(app.activeContentView().modeLineText().find(matchedText) != std::string::npos);
  CHECK(app.activeContentView().modeLineText().find("FGREP: hello") == std::string::npos);

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Grep);  // NOT Directory -- see this test's own comment
  REQUIRE(app.grepMode().matches().size() == 1);       // the original search results, not re-run

  // 2026-08-28 fix, a real report ("^Gの連打で無限ループ"): a further
  // Back from here used to bounce right back to Grep forever (annotation
  // cleared but fileReturnMode_ stayed stuck at Grep) -- now correctly
  // reaches File (plain), then Directory, and stays there.
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK_FALSE(app.fileMode().hasSearchAnnotation());
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  app.handleCommand(Command::Back);  // one more, for good measure (a legitimate no-op here) -- must NOT loop back to Grep
  CHECK(app.currentMode() != ApplicationMode::Grep);
}

// 2026-08-28 fix, a real report + precise repro ("履歴Gが不良...②の
// はずが上記①"): a search reached via the ^J^S candidate list (NOT a
// plain StartFgrep, which skips the candidate list and goes straight to
// an editable prompt -- see the test just above) sets
// grepReturnsToCandidateList_, so returnFromGrep()'s OWN early-return
// branch (re-entering the candidate list instead of jumping straight to
// File) used to skip the annotation-clearing code entirely -- File's
// own stale match annotation (from openSelectedGrepMatch()) survived
// the whole candidate-list detour, so the NEXT time File's plain view
// got shown it still read as "landed on the match" instead of "backed
// all the way out", extending the ^G chain by one confusing extra hop.
TEST_CASE_FIXTURE(Fixture, "GREP (search reached via the ^J^S candidate list, File origin): repeated Back "
                           "correctly reaches the PLAIN file (no stale annotation) after re-entering the "
                           "candidate list, not a second landing on the match") {
  history.pushRecent(RingCategory::RecentEgrepPattern, "x");
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  REQUIRE(app.feedByte(0x13, std::nullopt));  // ^S -> EGREP direct entry (candidate list)
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.feedByte(0x15, std::nullopt));  // ^U -> starts editing
  feedText("hello");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // submit
  REQUIRE(app.currentMode() == ApplicationMode::Grep);

  REQUIRE(app.handleCommand(Command::Enter));  // select the top match
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.fileMode().hasSearchAnnotation());  // "①"

  REQUIRE(app.handleCommand(Command::Back));  // -> "③" (results)
  REQUIRE(app.currentMode() == ApplicationMode::Grep);

  REQUIRE(app.handleCommand(Command::Back));  // -> "④" (candidate list re-entered)
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  CHECK(app.handleCommand(Command::Back));  // -> should be "②" (plain file), NOT "①" again
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK_FALSE(app.fileMode().hasSearchAnnotation());
}

// 2026-09-19 re-reversal, a direct user report + side-by-side legacy
// comparison ("CLAUDE.mdを選択→^Ssrc^M...モードラインはやはりおかしい。
// GREPが二文字あるので...「GREP」「01/14 TOP」「1046」「(grep)」
// 「GREP: src」...旧版を見てみたら「FGREP」「01/14 TOP」「(fgrep)」
// 「/Users/haa15030/git/usilic/CLAUDE.md」となっていた...こちらが
// 正しいと思う"): the 2026-08-28 choice (label+pattern here, see the
// old version of this test's own comment, now corrected below) turned
// out to read as a literal duplicated word whenever grepSyntax_ is
// GrepSyntax::Basic -- name (top-left) and trailing3 both say "GREP"
// verbatim ("GREP" ... "GREP: src"). A direct side-by-side comparison
// against the real legacy binary (same file, same FGREP) showed legacy
// shows the origin file's own raw path here instead, with no label
// prefix at all -- reverting to that. The landed-match screen (the
// test just above, openSelectedGrepMatch()'s File branch) is a
// different screen and keeps its own convention (the matched line's
// text) unchanged -- not part of today's report.
TEST_CASE_FIXTURE(Fixture, "GREP results screen (grepView_, File origin): trailing3 shows the origin file's "
                           "raw path, not a redundant \"<name>: <pattern>\" label") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("hello");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);

  const std::string &row = display.rowText(23);
  CHECK(row.find((root / "plain.txt").string()) != std::string::npos);
  CHECK(row.find("FGREP: hello") == std::string::npos);
}

// 2026-08-28 fix, a direct user report + precise before/after mode-line
// values ("「^-」「...usilic/CLAUDE.md」"(不良、隙間無し) vs "「^-」
// 「...usilic/CLAUDE.md」"(正解、1桁の隙間あり)), refined by a same-day
// follow-up ("^Uを投入したとたんに１文字左へ寄る症状。そこだけ直せば
// いいのでは" -- confirming the READ-ONLY candidate list, rendered via
// ContentView::renderModeLine()'s own budget-aware truncation, was
// already correct; only drawPromptLabel()'s own separate, hand-rolled
// column math -- reached via ^U's transition into live editing -- had
// the bug): the trailing destination field must leave a real gap after
// the editable field, not sit flush against it, while STILL never
// writing to the row's true last column (79).
TEST_CASE_FIXTURE(Fixture, "EGREP candidate list, ^U into edit mode (File origin, long path): the trailing "
                           "destination field leaves a real gap after the editable field, and still never "
                           "writes to the true last column") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  REQUIRE(app.feedByte(0x13, std::nullopt));  // ^S -> EGREP direct entry
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.feedByte(0x15, std::nullopt));  // ^U
  feedText("^-");
  const std::string &row = display.rowText(23);
  std::size_t dots = row.find("...");
  REQUIRE(dots != std::string::npos);
  CHECK(row[dots - 1] == ' ');                                       // a real gap column right before the trailing field
  CHECK_FALSE(display.standoutAt(23, static_cast<int>(dots) - 1));   // and it's plain, not Standout
  CHECK_FALSE(display.standoutAt(23, 79));                            // the true last column: never written to
}

// 2026-08-28 fix, a direct user report ("検索前には画面にあったブロック
// カーソルが検索一覧のどれかを[return]後の画面では表示されない...どれ
// がマッチ行かわかりづらい"): openSelectedGrepMatch() unconditionally
// called hideVisibleCursor(), correct for the Directory case (whose own
// full-row highlight already marks the current entry) but wrong for
// File, which has no other highlight of its own and relies entirely on
// the terminal's real caret to show the current line.
TEST_CASE_FIXTURE(Fixture, "GREP: Enter on a match (search run from File mode) leaves the terminal's block "
                           "cursor VISIBLE, on the matched line -- not hidden the way the Directory case is") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("world");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);
  REQUIRE(app.grepMode().matches().size() == 1);

  CHECK(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK_FALSE(display.visibleCursorHidden());
  CHECK(display.cursorRow() == app.activeContentView().cursorDisplayRow());
}

TEST_CASE_FIXTURE(Fixture, "GREP: a second Back (out of the grep results reached via the first Back) "
                           "clears the stale \"FGREP: pattern\" annotation instead of carrying it forward "
                           "(2026-08-11 fix: a real report found the mode line still read \"FGREP: .json\" "
                           "here instead of the plain \"(ls)\" a fresh Back-out-of-grep should show)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.activeContentView().modeLineText().find("FGREP: plain.txt") != std::string::npos);
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK_FALSE(app.directoryMode().hasSearchAnnotation());
  CHECK(app.activeContentView().modeLineText().find("FGREP:") == std::string::npos);
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

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^C (StartCutRegex) also opens directly into the candidate list, "
                           "same as EGREP (2026-08-05: see startCutPrompt()'s own comment -- unlike "
                           "FGREP/EGREP, CUT's own two variants show identically as \"CUT\", so this is "
                           "the only visible cue distinguishing regex/\"Select\" from fixed/\"Input\")") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));

  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte(0x03, std::nullopt));
  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
}

// -- CUT: split, view a segment, resplit it, back out step by step --

TEST_CASE_FIXTURE(Fixture, "handleCommand(StartCutFixed): opens a prompt from Directory mode (2026-08-11 fix: "
                           "a real report found ^J^C was a silent no-op here, unlike ^J^S/EGREP which was "
                           "never restricted this way)") {
  REQUIRE(app.start(path()));
  CHECK(app.handleCommand(Command::StartCutFixed));
  CHECK(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "CUT (2026-08-11 fix): submitting a pattern from Directory mode splits the "
                           "directory listing itself, landing in Cut mode with a \"CUT: pattern\" mode-line "
                           "annotation to get back to it from Directory") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("cpmv");  // matches cpmv_a.txt/cpmv_b.txt/cpmvdest's own ls lines
  REQUIRE(app.feedByte(0x0d, std::nullopt));

  CHECK(app.currentMode() == ApplicationMode::Cut);
  // One leading unlabeled segment (everything before the first match)
  // plus one per matching line -- see CutMode::split()'s own comment.
  REQUIRE(app.cutMode().segments().size() == 4);

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.activeContentView().modeLineText().find("CUT: cpmv") != std::string::npos);

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Cut);
  CHECK(app.cutMode().segments().size() == 4);  // still intact, not re-split

  // 2026-08-24 fix, a real report: "^G連打→戻り値「CUT: ^##」との
  // 行き来ループ→（ls）へ戻らず" -- a THIRD ^G (leaving this RESUMED
  // results screen) used to rewrite the SAME "CUT: cpmv" annotation
  // yet again instead of clearing it, so a fourth ^G would just
  // re-enter Cut mode again, forever. It must now clear the
  // annotation and land on a genuinely plain (ls).
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.activeContentView().modeLineText().find("CUT:") == std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "CUT (2026-08-11 fix): the ^J^C candidate list shows the real mode name and "
                           "the destination directory highlighted on the right, not the fixed \"HISTORY\" "
                           "name + unhighlighted mode-name badge a real ^R recall uses (a real report + "
                           "legacy source read: direct entry via startRegexPromptList() is a different "
                           "screen from ^R, per mode.c's own T_hfgrep/T_hegrep vs plain M_cut2)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartCutRegex));  // ^J^C
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  std::string modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.find("CUT") != std::string::npos);
  CHECK(modeLine.find("HISTORY") == std::string::npos);
  // 2026-08-12 fix: the destination directory is now truncated to its
  // own last 16 characters (see truncatedTrailingPath()'s own
  // comment) -- checking for path()'s own trailing 16 chars survives
  // that truncation regardless of how long the fixture's temp dir path
  // happens to be, unlike the full path() itself.
  CHECK(modeLine.find(path().substr(path().size() - 16)) != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "CUT (2026-08-11 fix): the ^J^C candidate list starts on the TOP (oldest) "
                           "entry, not the bottom -- a real report found it landing on the LAST seed "
                           "pattern instead of the first") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentEgrepPattern, "first");
  history.pushRecent(RingCategory::RecentEgrepPattern, "second");
  history.pushRecent(RingCategory::RecentEgrepPattern, "third");
  REQUIRE(app.handleCommand(Command::StartCutRegex));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK(app.activeContentView().cursorIndex() == 0);
  CHECK(*app.listBrowserMode().selectedItem() == "first");
}

TEST_CASE_FIXTURE(Fixture, "CUT (2026-08-11 fix): the destination directory still shows even when the "
                           "highlighted candidate's own long pattern text would overflow the row width -- "
                           "fitModeLineFields() used to drop trailing/trailing2 together as one all-or-"
                           "nothing unit, silently hiding the directory too even though it alone would fit") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentEgrepPattern, std::string(70, 'x'));  // deliberately overflows 80 cols
  REQUIRE(app.handleCommand(Command::StartCutRegex));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  std::string modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.find("CUT") != std::string::npos);
  // 2026-08-12 fix: the destination directory is now truncated to its
  // own last 16 characters (see truncatedTrailingPath()'s own
  // comment) -- checking for path()'s own trailing 16 chars survives
  // that truncation regardless of how long the fixture's temp dir path
  // happens to be, unlike the full path() itself.
  CHECK(modeLine.find(path().substr(path().size() - 16)) != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "CUT (2026-08-11 fix): the highlighted candidate's own preview text in the mode "
                           "line is drawn plain (non-reverse-video), since that position reads as an "
                           "editable text area (what typing would replace), not a static highlighted label "
                           "-- a real report + reference screenshot found it wrongly highlighted") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentEgrepPattern, "abc");
  REQUIRE(app.handleCommand(Command::StartCutRegex));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  int modeLineRow = 23;  // rows()-1, the fixed mode-line row every ContentView uses
  const std::string &row = display.rowText(modeLineRow);
  CHECK(row.find("CUT") != std::string::npos);
  CHECK(display.standoutAt(modeLineRow, 0));  // "CUT" itself is still highlighted, unaffected

  std::size_t candidatePos = row.find("abc");
  REQUIRE(candidatePos != std::string::npos);
  CHECK_FALSE(display.standoutAt(modeLineRow, static_cast<int>(candidatePos)));
}

TEST_CASE_FIXTURE(Fixture, "CUT (2026-08-11 fix): the candidate list stays visible on screen while editing "
                           "starts (^U no longer replaces it with the full directory listing, which read "
                           "confusingly identical to ^Jc's own plain fgrep-style prompt -- a real report)") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentEgrepPattern, "abc");
  REQUIRE(app.handleCommand(Command::StartCutRegex));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  CHECK(app.feedByte(0x15, std::nullopt));  // ^U: starts in-place editing
  // currentMode_ switches internally so executeCut() resolves the
  // right source at submit time (see feedRegexListByte()'s own
  // comment) -- but what's actually ON SCREEN should still be the
  // candidate list's own content, not the full directory listing.
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.promptActive());

  bool foundCandidateRow = false;
  for (int row = 0; row < 23; ++row) {
    if (display.rowText(row).find("abc") != std::string::npos) foundCandidateRow = true;
  }
  CHECK(foundCandidateRow);
}

TEST_CASE_FIXTURE(Fixture, "feedPromptByte: ^U clears from the field start to the cursor (2026-08-11 fix: "
                           "LineEditor::killToStart() already existed but was never wired to any actual "
                           "keystroke, so ^U silently corrupted the prompt text -- a real report traced a "
                           "\"CUT search does nothing after ^U\" symptom to exactly this)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("garbage");
  REQUIRE(app.promptText() == "garbage");

  CHECK(app.feedByte(0x15, std::nullopt));  // ^U
  CHECK(app.promptText().empty());

  feedText("cpmv");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Cut);
  REQUIRE(app.cutMode().segments().size() == 4);
}

TEST_CASE_FIXTURE(Fixture, "feedPromptByte: ^Q (quoted insert) inserts the NEXT byte literally instead of "
                           "treating it as a command -- LineEditor::toggleQuotedInsert()/quotedInsertPending()/"
                           "clearQuotedInsert() already existed but were never wired (2026-08-11 fix: a real "
                           "user recollection, \"文字列入力の際のコントロールコードは^Qの前置で入力できた\")") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("ab");

  CHECK(app.feedByte(0x11, std::nullopt));  // ^Q: arms quoted-insert
  // The very next byte would normally backspace (0x08) -- quoted, it's
  // inserted literally instead.
  CHECK(app.feedByte(0x08, std::nullopt));
  CHECK(app.promptText() == std::string("ab\x08"));
  CHECK(app.promptActive());  // still editing, nothing submitted/cancelled

  feedText("c");
  // Split into adjacent literals ("ab\x08" "c"), NOT "ab\x08c" as one --
  // C++'s \x escape greedily consumes every following hex digit, and
  // 'c' is a valid hex digit, so a single literal would parse as "ab"
  // + chr(0x08c) instead of "ab" + BS + 'c'.
  CHECK(app.promptText() == std::string("ab\x08" "c"));
}


TEST_CASE_FIXTURE(Fixture, "feedPromptByte: ^Q also quotes Enter/^G/^U -- normally-special bytes -- so a "
                           "single ^Q only affects the ONE byte right after it, not a whole mode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("x");

  CHECK(app.feedByte(0x11, std::nullopt));  // ^Q
  CHECK(app.feedByte(0x07, std::nullopt));  // quoted ^G: inserted literally, prompt NOT cancelled
  CHECK(app.promptActive());
  CHECK(app.promptText() == std::string("x\x07"));

  // The very NEXT ^G (no longer quoted) cancels normally.
  CHECK(app.feedByte(0x07, std::nullopt));
  CHECK_FALSE(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "feedPromptByte: ^J^W (SHELL: insert buffer content) works from a PLAIN FGREP "
                           "prompt too, not just the ^J^S candidate list -- 2026-08-12 fix. A real report: "
                           "^J^W worked once already routed through feedRegexListByte()'s own 2026-08-11 "
                           "leader-dispatch fix, but FGREP/CUT's plain prompt entry (this shared "
                           "feedPromptByte(), used by every PromptKind) still showed literal reversed "
                           "\"JW\" text -- the same gap, just not yet closed in the OTHER routine that "
                           "feeds prompt bytes.") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));  // ^S: plain prompt, no candidate list
  REQUIRE(app.promptActive());
  feedText("abc");
  CHECK(app.promptText() == "abc");

  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  bool ok = app.feedByte(0x17, std::nullopt);  // ^W
  if (!ok) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  // "abc" must NOT have gained literal ^J/^W bytes -- the whole point is
  // that pressing ^J^W is interpreted as a command, not as two characters
  // typed into the pattern.
  CHECK(app.currentMode() == ApplicationMode::Shell);
  // 2026-08-18 REDESIGN: no longer w.N -- see shellInsertBufferContent()'s
  // own comment. Directory mode's own current directory's basename
  // (the Fixture's own temp root dir's name here).
  std::string savePath = (root / ".usilic" / "saved" / root.filename()).string();
  // Single-quoted (2026-08-27 fix, a real report + repro: a real
  // archive entry's own basename can contain shell glob metacharacters
  // like "[...]", which zsh/bash otherwise try to expand -- "no matches
  // found" -- see shellInsertBufferContent()'s own comment).
  CHECK(app.shell().commandLine().text() == "cat '" + savePath + "'");
}

TEST_CASE_FIXTURE(Fixture, "feedPromptByte: ^J^W also works from CUT's plain prompt (2026-08-12 fix, same "
                           "shared routine as the FGREP case above)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartCutFixed));
  REQUIRE(app.promptActive());
  feedText("x");

  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  bool ok = app.feedByte(0x17, std::nullopt);  // ^W
  if (!ok) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(app.currentMode() == ApplicationMode::Shell);
}

TEST_CASE_FIXTURE(Fixture, "CUT (2026-08-11 fix): the full reported reproduction -- ^J^C, ^U to clear the "
                           "pre-filled seed pattern, type a real pattern, Enter -- actually splits the "
                           "directory listing instead of silently doing nothing") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartCutRegex));  // ^J^C: candidate list
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  CHECK(app.feedByte(0x15, std::nullopt));  // ^U: transitions to editable, pre-filled text cleared
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.promptActive());
  CHECK(app.promptText().empty());

  feedText("cpmv");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // Enter
  CHECK(app.currentMode() == ApplicationMode::Cut);
  REQUIRE(app.cutMode().segments().size() == 4);
}

TEST_CASE_FIXTURE(Fixture, "CUT (2026-08-11 fix): Back from a Cut split reached via the ^J^C candidate "
                           "list re-opens that SAME candidate list (a proper LIFO stack -- Directory -> "
                           "list -> results, popping one level at a time), not Directory directly -- a "
                           "direct user clarification: \"検索への入口がCUTなら出るときもCUTから出るはず\"") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartCutRegex));  // ^J^C: candidate list
  CHECK(app.feedByte(0x15, std::nullopt));             // ^U: start editing
  feedText("cpmv");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // Enter: submits, lands in Cut
  REQUIRE(app.currentMode() == ApplicationMode::Cut);

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);  // back to the candidate list, NOT Directory
  // Confirms this is genuinely the CUT candidate-list-recall screen
  // (not some other ListBrowser use, e.g. FILE/DIR picker) -- see the
  // direct-entry mode-line fix's own test for what this field means.
  CHECK(app.activeContentView().modeLineText().find("CUT") != std::string::npos);

  // A second round-trip -- edit again, submit again -- still chains
  // back to the candidate list, not Directory, since THIS submission
  // also came from the list.
  CHECK(app.feedByte(0x15, std::nullopt));
  feedText("plain.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);

  // Only cancelling FROM the candidate list itself (^G there) reaches
  // Directory.
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "CUT (2026-08-11 fix): Back from a Cut split reached via ^Jc's plain blank "
                           "prompt (not the candidate list) still returns straight to Directory, unchanged "
                           "from item 269's own behavior") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartCutFixed));  // ^Jc: blank prompt, no candidate list involved
  feedText("cpmv");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
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

// 2026-08-24 fix, a real report + reference: "CUT" "01/13 TOP" "435"
// "/Users/.../CLAUDE.md         " -- this provider used to have no
// byte-count field at all for File/Directory-origin CUT, just the
// path/pattern alone.
TEST_CASE_FIXTURE(Fixture, "CUT (regular file, File-origin): mode-line shows the whole file's own byte "
                           "size (not just its path)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::StartCutFileFixed));
  feedText("marker");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);

  auto expectedBytes = std::filesystem::file_size(root / "log.txt");
  std::string text = app.activeContentView().modeLineText();
  CHECK(text.find(std::to_string(expectedBytes)) != std::string::npos);
  CHECK(text.find((root / "log.txt").string()) != std::string::npos);
}

// 2026-08-24 fix, a direct user follow-up question ("戻り値が正常化
// さえ実現すれば直る問題じゃないの？"): ^R-recalling a pattern
// originally used for a File-origin CUT, from Directory mode (having
// already backed all the way out via ^G), used to silently re-target
// the DIRECTORY LISTING instead of the same file -- currentMode_==
// Directory at recall time was enough to make executeCut() take its
// Directory branch, discarding the fact that cutOrigin_/
// fileMode_.currentFile() were both still correctly sitting there
// unchanged. Fixed by making a FRESH Directory-origin arm
// (startCutPrompt() from Directory) explicitly set cutOrigin_ itself,
// so reaching executeCut() with cutOrigin_ still File unambiguously
// means "no fresh Directory search since" -- reuse that file instead.
TEST_CASE_FIXTURE(Fixture, "CUT (regular file, File-origin, regex ^J^B): ^R-recalling the pattern still "
                           "re-targets the SAME file, not the directory listing") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::StartCutFileRegex));  // ^J^B -- the exact variant the report used
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  feedText("marker");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);
  REQUIRE(app.cutMode().segments().size() == 3);  // see the earlier "splits the SELECTED entry" test

  // Regex (direct-entry) CUT's own LIFO stack (see PendingCpMvRmOperation-
  // style comments elsewhere: "検索への入口がCUTなら出るときもCUTから
  // 出るはず"): the FIRST ^G returns to the candidate list itself, not
  // straight to Directory -- matching the report's own literal
  // "^G→^R" (a single ^G before ^R), not "^G^G→^R".
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  REQUIRE(app.feedByte(0x12, std::nullopt));  // ^R: restarts the SAME candidate list as a real history view
  REQUIRE(app.handleCommand(Command::Enter));  // select "marker" (the only/newest entry)

  CHECK(app.currentMode() == ApplicationMode::Cut);
  REQUIRE(app.cutMode().segments().size() == 3);  // same real split as before, not "1/1, no match"
  CHECK(app.cutMode().segments()[0].label == "marker one");  // log.txt's own content, not a directory listing
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

TEST_CASE_FIXTURE(Fixture, "CUT: ^Jc pressed WHILE STILL ON THE LABEL LIST (not yet entered into a segment's "
                           "own content) nests correctly too (2026-08-05 fix: a direct user reproduction -- "
                           "select HELP.jp, ^Jc \"man\", then IMMEDIATELY ^Jc \"usilic\" again without first "
                           "pressing Enter -- found this silently dropped the second ^Jc's whole keystroke "
                           "sequence into ordinary list navigation instead, since startCutPrompt()'s own guard "
                           "only allowed File/CutContent, not Cut)") {
  REQUIRE(app.start(path()));
  int rootIdx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != rootIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  int fileIdx = indexOf("sub.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != fileIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("alpha");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);  // label list -- NOT CutContent, no Enter pressed yet
  REQUIRE(app.cutMode().segments().size() == 2);
  REQUIRE(app.activeContentView().cursorIndex() == 0);  // segment 0 ["alpha", "beta"] is selected

  REQUIRE(app.handleCommand(Command::StartCutFixed));  // ^Jc AGAIN, straight from the label list
  feedText("beta");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Cut);
  // Re-splitting segment 0's own content (["alpha", "beta"]) by "beta"
  // yields a leading "--"-labeled segment (content before the first
  // match, ["alpha"]) plus the matched one (label="beta", ["beta"]) --
  // same shape doctest already covers for the CutContent-triggered
  // case just above, confirming this label-list-triggered path reused
  // the exact same split() logic rather than falling back to a fresh
  // top-level split of the WHOLE file (which would have produced 2
  // segments matching "alpha", not this).
  REQUIRE(app.cutMode().segments().size() == 2);
  CHECK(app.cutMode().segments()[0].label == "--");
  CHECK(app.cutMode().segments()[1].label == "beta");
}

TEST_CASE_FIXTURE(Fixture, "cutView_ mode-line: top-level split shows \"CUT\" and the originating file's FULL "
                           "path (2026-08-05 user-confirmed example: splitting doc/HELP.jp by \"man\" shows "
                           "trailing \"/Users/.../doc/HELP.jp\", the full path -- unlike FileMode's/CutContent's "
                           "own bare-filename convention)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("plain");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);

  std::string text = app.activeContentView().modeLineText();
  CHECK(text.rfind("CUT ", 0) == 0);
  CHECK(text.find((root / "plain.txt").string()) != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "cutView_ mode-line: a NESTED split shows \"CUT:<parent pattern>\" as trailing -- "
                           "the PARENT level's own pattern, not the just-typed one (2026-08-05 user-confirmed "
                           "example: split \"man\", select a match, re-split it by \"usilic\" -> trailing reads "
                           "\"CUT:man\", not \"CUT:usilic\")") {
  REQUIRE(app.start(path()));
  int rootIdx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != rootIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  int fileIdx = indexOf("sub.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != fileIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("alpha");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("beta");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);

  CHECK(app.cutMode().parentPattern() == "alpha");
  std::string text = app.activeContentView().modeLineText();
  CHECK(text.rfind("CUT ", 0) == 0);
  CHECK(text.find("CUT:alpha") != std::string::npos);
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

// 2026-09-15追加、直接のユーザー方針("CUT、F/EGREPは[MAN、LOCATE、
// DU等に]関与（検索）できる...基点の棲み分けで言えば...後者群は前者群
// [へ戻る]"): startCutPrompt()自身の新設のMan/Locate/Du分岐参照――
// Directory起点(directoryView_.lines()を分割する)と全く同じ発想で、
// manView_/locateView_/duView_.lines()を分割し、Backで発動元自身
// (Directory固定のMAN/LOCATE/DU自身とは対照的に)へ戻る。
TEST_CASE_FIXTURE(Fixture, "CUT: works from MAN too, splitting the currently-displayed page, and Back "
                           "returns to Man (not Directory)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartMan));
  feedText("ls");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("man not installed or 'ls' has no man page here -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Man);

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("a");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Cut);
  CHECK(app.activeContentView().modeLineText().find("MAN") != std::string::npos);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Man);
}

TEST_CASE_FIXTURE(Fixture, "CUT: works from LOCATE too, splitting the results list, and Back returns to "
                           "Locate (not Directory)") {
  fs::path target = root / "cut_locate_target.txt";
  { std::ofstream(target) << "x"; }
  fs::path fakeLocate = root / "fake_locate_cut.sh";
  {
    std::ofstream script(fakeLocate);
    script << "#!/bin/sh\necho '" << target.string() << "'\n";
  }
  std::filesystem::permissions(fakeLocate, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
  configuration.setCommandForTest(ConfiguredRole::Locate, {{fakeLocate.string()}, std::nullopt});

  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("anything");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Locate);

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("cut_locate_target");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Cut);
  CHECK(app.activeContentView().modeLineText().find("LOCATE") != std::string::npos);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Locate);
}

TEST_CASE_FIXTURE(Fixture, "CUT: works from DU too, splitting the results list, and Back returns to Du "
                           "(not Directory)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("plain.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Cut);
  CHECK(app.activeContentView().modeLineText().find("DU") != std::string::npos);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Du);
}

// 2026-09-15追加、同じユーザー方針の確認: startGrepPrompt()にはそもそも
// 一度もcurrentMode_の制限が無く、grepReturnMode_も既にcurrentMode_を
// そのまま動的に記憶している(Group2=CUT/F/EGREP自身の正しい仕様、
// startCutPrompt()自身の新設コメント参照)――そのためFGREP/EGREPは
// 実装変更無しで既にMAN/LOCATE/DUから使えていたはずだが、実際にDUから
// 検証しておく。
TEST_CASE_FIXTURE(Fixture, "FGREP: already worked from DU (no gate on startGrepPrompt()) -- Back returns to "
                           "Du, not Directory") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);

  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Grep);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Du);
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

// 2026-08-13 (CLAUDE.md history item 289's follow-up): ^J m and ^J ^M
// are no longer the same command -- ^J ^M (K-44, "Select manual
// page") now opens directly into a candidate list of recent MAN
// pages, the same "Input"/"Select" shape as EGREP's ^Jl/^J^S.
TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^M opens a MAN candidate list (StartManList), not a prompt") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte(0x0d, std::nullopt));
  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
}

TEST_CASE_FIXTURE(Fixture, "MAN: ^J ^M seeds the candidate list from RecentManPage history") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartMan));
  feedText("this-command-does-not-exist-xyz123");
  app.feedByte(0x0d, std::nullopt);  // pushed to RecentManPage regardless of loadPage()'s outcome
  REQUIRE(app.currentMode() == ApplicationMode::Directory);  // loadPage() failed, never left Directory
  app.feedByte(0x0a, std::nullopt);
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  const std::deque<std::string> &recent = history.recentEntries(RingCategory::RecentManPage);
  REQUIRE_FALSE(recent.empty());
  CHECK(recent.back() == "this-command-does-not-exist-xyz123");
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

// 2026-08-13 (CLAUDE.md history item 291's mode-line audit): manView_
// had no ModeLineFieldsProvider at all before this round -- see
// contentByteCount()'s own comment in application.cpp.
TEST_CASE_FIXTURE(Fixture, "MAN: mode line shows 'MAN' and a byte count once a page is loaded") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartMan));
  feedText("ls");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("man not installed or 'ls' has no man page here -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Man);
  const std::string &modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.find("MAN") != std::string::npos);
  std::size_t expectedBytes = 0;
  for (const auto &line : app.activeContentView().lines()) expectedBytes += line.text.size() + 1;
  REQUIRE(expectedBytes > 0);
  CHECK(modeLine.find(std::to_string(expectedBytes)) != std::string::npos);
}

// 2026-09-15追加、直接のユーザー指摘("戻り値は？MANもNGだったような
// …"): HELP.jp自身のmode-line図([6]="以前のモード名またはファイル
// 名...^Gでそこへ戻ります")に照らすと、MAN自身の画面はこのフィールド
// を一切持っていなかった(DU/LOCATE/ARCは同種の戻り値をとうに持って
// いたのに、MANだけ取り残されていた)。今やMAN自身の戻り先は常に
// 基点のDirectoryなので、それ(directoryMode_.currentDirectory())を
// trailing3として表示するようにした。
TEST_CASE_FIXTURE(Fixture, "MAN: mode line shows the Directory it will return to via trailing3 (HELP.jp's "
                           "own mode-line field [6], previously missing entirely for MAN)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartMan));
  feedText("ls");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("man not installed or 'ls' has no man page here -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Man);
  const std::string &modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.find(app.directoryMode().currentDirectory()) != std::string::npos);
}

// 2026-09-19再訂正、直接のユーザー方針の転換("MANを使ってみて思うのは、
// 通常ファイルを閲覧中にMANを発動した場合、そこへ戻る方が自然な気が
// する。読書でも注釈参照後は元の場所へ戻るように"): 2026-09-15の
// 「MAN/LOCATE/DUは自己完結した目的地であり、発動元がどこであれ常に
// 基点のDirectoryへ戻るべき」という判断(すぐ下のテストがかつて検証
// していたもの)を撤回する。nearestFileOrDirectoryAnchor()自身の
// コメント参照――HELP/KEY/SHELLの「全モード」棲み分けとは別に、MAN/
// LOCATE/DUは「通常ファイルかディレクトリ」を発動元から遡って基点
// とする。
TEST_CASE_FIXTURE(Fixture, "MAN: ^G returns to the normal FILE it was invoked from (not the base Directory), "
                           "matching a footnote returning to the page it was opened from") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string filePath = app.fileMode().currentFile();

  REQUIRE(app.handleCommand(Command::StartMan));
  feedText("ls");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("man not installed or 'ls' has no man page here -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Man);
  CHECK(app.activeContentView().modeLineText().find(filePath) != std::string::npos);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == filePath);
}

// MAN invoked directly from Directory (not from a normal file) still
// resolves its own base to Directory -- nearestFileOrDirectoryAnchor()
// returns currentMode_ itself (already a valid anchor) without any
// further indirection needed.
TEST_CASE_FIXTURE(Fixture, "MAN: ^G returns to the base Directory mode when invoked directly from Directory") {
  REQUIRE(app.start(path()));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);

  REQUIRE(app.handleCommand(Command::StartMan));
  feedText("ls");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("man not installed or 'ls' has no man page here -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Man);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

// 2026-09-19追加、直接のユーザー報告(2度目、MAN起点がFileの場合):
// "usilic -c git/usilicで起動→CLAUDE.mdを選択→^Jmls^M^S→戻り値不良。
// 正しくは「MAN: ls」...→sort^M→^G→戻り値不良。正しくは
// 「/Users/haa15030/git/usilic/CLAUDE.md」"。原因は2つ:
// (1) previousModeName()自身のManケースが裸の"ls"を返しており(すぐ
// 上のLocateのケースと同じ穴)、drawPromptLabel()自身のGrepPattern
// プロンプト(まだ未投入)のtrailing欄で「MAN: 」接頭辞が付かなかった。
// (2) openSelectedGrepMatch()自身のMan分岐がmanReturnMode_自体を
// 一時的にGrepへ上書きしていたため(その回の直前の修正)、
// returnFromGrep()側の対称的な後始末が「本来の値」を覚えておく手段が
// 無いままDirectoryへ無条件リセットするしかなく、MANがFileから発動
// されていた場合にその本来の戻り先(ファイル自身)を失っていた――
// 独立したフラグ(manReturnsToGrepResults_)へ設計し直し、
// manReturnMode_自体には一切触れないようにして解決した。
// 2026-09-11更新、直接のユーザー依頼("^Sと^[iをFILE/DIR以外へも反映
// してほしい")によりMan起点の^S(Basic)もin-place方式になった――
// LocateMode自身の対になるテストのコメント参照。currentMode_は最初
// からMan、専用のGrep一覧へは一度も切り替わらない。
TEST_CASE_FIXTURE(Fixture, "MAN started from File mode: the still-open GREP prompt's trailing field already "
                           "shows \"MAN: ls\", and backing out of an in-place grep search WITHOUT selecting a "
                           "match still correctly shows the origin file's path (not the base Directory)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string filePath = app.fileMode().currentFile();

  REQUIRE(app.handleCommand(Command::StartMan));
  feedText("ls");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("man not installed or 'ls' has no man page here -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Man);

  REQUIRE(app.handleCommand(Command::StartGrep));
  REQUIRE(app.promptActive());
  const std::string &promptRow = display.rowText(display.rows() - 1);
  CHECK(promptRow.find("MAN: ls") != std::string::npos);

  feedText("sort");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("'ls' man page has no 'sort' match here -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Man);  // in-place: never left Man
  REQUIRE_FALSE(app.grepMode().matches().empty());

  // Back WITHOUT ever leaving Man -- goes straight to Man's own normal
  // Back destination (the origin File).
  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == filePath);
}

// 2026-09-19追加、帰宅した直後の直接のユーザー報告("GREPが不良。
// ^Jmls^M^Ssort^M(MANを発動し、GREPでsortを検索)→モードライン不良、
// 正しくは「GREP」「01/22 TOP」「1514」「(grep)」「MAN: ls       」
// →^M→やはり不良、正しくは「MAN」「030/525  5%」「25921」「(man)」
// 「GREP: sort   」→^G→`/Users/haa15030/git/usilic/`になってしまう
// (正しくは検索結果一覧へ戻るべき)"): Group1/Group2の棲み分け方針
// ("後者群は前者群からも起動できる")の実装は、executeGrep()/
// viewForMode()側では既にMan/Locate/Duを一般的に扱っていたが、
// (1) grepView_自身のtrailing3にMan/Locate/Du起点のラベルの分岐が
// 一切無く常に空欄、(2) openSelectedGrepMatch()にもMan/Locate分岐が
// 無いのでmanReturnMode_/locateReturnMode_がGrepへ上書きされず、
// マッチへ着地した画面のtrailing3は(manView_自身の2026-09-15の修正が
// 常にdirectoryMode_.currentDirectory()を表示する前提だったため)
// 実際の戻り先(Grep結果一覧)と食い違う基点のDirectoryパスのまま
// だった――という2箇所の見落としが原因だった。
// 2026-09-11更新、直接のユーザー依頼("^Sと^[iをFILE/DIR以外へも反映
// してほしい")によりMan起点の^S(Basic)もin-place方式になった――
// currentMode_は最初からMan、専用のGrep一覧へは一度も切り替わらない。
// Manの内容はDirectory/DUのような「開けるエントリ」を持たないプレーン
// なテキストなので(Command::EnterのswitchにMan自身の分岐は無い)、
// 以前あった「Enterで一旦Grep結果からManへ着地する」という段階は
// そもそも意味を成さなくなった――マッチへは検索確定と同時に既に
// 着地済みである。
TEST_CASE_FIXTURE(Fixture, "GREP started from Man mode: stays in-place (Man) with \"MAN: ls\" as the mode "
                           "line's origin label, and Back returns straight to Directory") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartMan));
  feedText("ls");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("man not installed or 'ls' has no man page here -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Man);

  REQUIRE(app.handleCommand(Command::StartGrep));
  feedText("sort");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("'ls' man page has no 'sort' match here -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Man);  // in-place: never left Man
  CHECK(app.activeContentView().modeLineText().find("ls") != std::string::npos);
  REQUIRE_FALSE(app.grepMode().matches().empty());

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
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

// 2026-08-13 (CLAUDE.md history item 289's follow-up): ^J p and ^J ^P
// are no longer the same command -- ^J ^P (K-42, "Select program
// name") now opens directly into a candidate list, the same "Input"/
// "Select" shape as MAN's ^Jm/^J^M just above.
TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^P opens a PROGRAM candidate list (StartProgramList), not a prompt") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte(0x10, std::nullopt));
  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
}

TEST_CASE_FIXTURE(Fixture, "PROGRAM: ^J ^P seeds the candidate list from ShellCommand history, and Enter runs it") {
  REQUIRE(app.start(path()));
  std::string outPath = (root / "prog_out2.txt").string();
  REQUIRE(app.handleCommand(Command::StartProgram));
  feedText("echo hi > " + outPath);
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);  // executeProgram() never changes ApplicationMode

  fs::remove(outPath);
  app.feedByte(0x0a, std::nullopt);
  REQUIRE(app.feedByte(0x10, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // Enter on the seeded (most recent) entry
  CHECK(fs::exists(outPath));
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

// 2026-09-10 (a direct user request): "usilic" itself is always accepted
// even though the fixture's rc (see above) never registers it as a
// PROGRAM entry -- mirrors SHELL mode's own pre-existing special-casing
// of a literal "usilic" invocation (Shell::start()'s own
// setenv("USILIC", ...) comment): the user shouldn't have to add this
// executable's own name to their usilicrc for PROGRAM to allow
// recursively launching it, any more than SHELL requires that.
TEST_CASE_FIXTURE(Fixture, "PROGRAM: \"usilic\" itself is always recognized, with no usilicrc entry needed") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartProgram));
  feedText("usilic subdir");

  std::string receivedCommand;
  app.programMode().setForegroundLauncher([&](const std::string &command) {
    receivedCommand = command;
    return true;
  });

  CHECK(app.feedByte(0x0d, std::nullopt));
  CHECK(receivedCommand == "usilic subdir");
  CHECK(app.currentMode() == ApplicationMode::Directory);  // PROGRAM never changes ApplicationMode
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
  // 2026-08-17 fix: StartProgram now pre-fills the prompt with the open
  // file's own bare name, cursor at the START -- see its own comment.
  // "echo " (with the trailing space) prepends into "echo plain.txt";
  // an earlier version of this test typed "echo hi" without accounting
  // for the pre-fill, silently producing the nonsensical "echo
  // hiplain.txt" (visible as stray "hiplain.txt" output in a real test
  // run, caught while implementing this same fix).
  REQUIRE(app.promptText() == "plain.txt");
  feedText("echo ");
  REQUIRE(app.promptText() == "echo plain.txt");
  CHECK(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::File);
}

// 2026-08-17 fix (docs/ideas/IDEAS.md:218, a NEW feature -- legacy
// never had this): a real report + hand-drawn mockup (=cb) specified
// that ^Jp should pre-fill the prompt with the highlighted file's own
// BARE filename, cursor on the FIRST character, so typing a program
// name prepends naturally ("less " + "CLAUDE.md" -> "less CLAUDE.md").
TEST_CASE_FIXTURE(Fixture, "PROGRAM: ^Jp pre-fills the prompt with the highlighted file's own bare "
                           "filename, cursor at the START (so typing a program name prepends)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::StartProgram));
  CHECK(app.promptText() == "plain.txt");  // bare name, not the full path
  feedText("less ");
  CHECK(app.promptText() == "less plain.txt");  // prepended, proving the cursor started at position 0
}

TEST_CASE_FIXTURE(Fixture, "PROGRAM: ^Jp starts with an EMPTY prompt when a directory (not a file) is "
                           "highlighted, matching the pre-existing behavior") {
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::StartProgram));
  CHECK(app.promptText().empty());
}

// 2026-08-25 (a direct user request tying PROGRAM into the same
// dired-style marking workflow '.'/'['/']' already wired up for
// CP/MV/RM): marking exactly ONE entry must still pre-fill the SAME
// bare, unquoted name as the no-marks cursor-only case above -- this
// is the backward-compatibility guard for startProgramPrompt()'s
// names.size()==1 branch, kept byte-identical to the pre-2026-08-25
// behavior on purpose.
TEST_CASE_FIXTURE(Fixture, "PROGRAM: marking exactly one entry pre-fills the SAME bare unquoted name as "
                           "the no-marks case (single-mark stays byte-identical to the old behavior)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));  // '.' / ^Space -- exactly one mark

  REQUIRE(app.handleCommand(Command::StartProgram));
  CHECK(app.promptText() == "plain.txt");  // bare, NOT "'plain.txt'"
}

// 2026-08-25: marking more than one entry pre-fills ALL of them,
// space-separated and shell-quoted (so a marked name containing a
// space can't merge with its neighbor), in listing order -- same
// target-gathering (targetEntries()) CP/MV/RM's own collectTargetPaths()
// already reads.
TEST_CASE_FIXTURE(Fixture, "PROGRAM: marking more than one entry pre-fills ALL marked bare names, "
                           "space-separated and shell-quoted, in listing order") {
  REQUIRE(app.start(path()));
  int plainIdx = indexOf("plain.txt");
  int logIdx = indexOf("log.txt");
  REQUIRE(plainIdx >= 0);
  REQUIRE(logIdx >= 0);
  int idxA = std::min(plainIdx, logIdx);
  int idxB = std::max(plainIdx, logIdx);
  std::string nameA = (idxA == plainIdx) ? "plain.txt" : "log.txt";
  std::string nameB = (idxA == plainIdx) ? "log.txt" : "plain.txt";

  while (indexOf(app.directoryMode().selectedEntry()->name) != idxA) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));
  while (indexOf(app.directoryMode().selectedEntry()->name) != idxB) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartProgram));
  std::string expected = "'" + nameA + "' '" + nameB + "'";
  CHECK(app.promptText() == expected);
  feedText("cat ");
  CHECK(app.promptText() == "cat " + expected);  // cursor started at position 0, same prepend convention
}

// 2026-08-25: marked directories are excluded from the pre-fill, same
// as the pre-existing lone-cursor-on-a-directory case above -- if
// every mark happens to be a directory, the prompt falls all the way
// back to empty (an arbitrary PROGRAM command on a directory isn't a
// meaningful pre-fill).
TEST_CASE_FIXTURE(Fixture, "PROGRAM: a mix of marked files and a marked directory only pre-fills the "
                           "file(s), excluding the directory") {
  REQUIRE(app.start(path()));
  int plainIdx = indexOf("plain.txt");
  int subdirIdx = indexOf("subdir/");
  REQUIRE(plainIdx >= 0);
  REQUIRE(subdirIdx >= 0);
  int idxA = std::min(plainIdx, subdirIdx);
  int idxB = std::max(plainIdx, subdirIdx);

  while (indexOf(app.directoryMode().selectedEntry()->name) != idxA) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));
  while (indexOf(app.directoryMode().selectedEntry()->name) != idxB) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartProgram));
  CHECK(app.promptText() == "plain.txt");  // bare -- only one real file among the two marks
}

// The real end-to-end proof: usilic itself never chdir()s (see
// executePathInput()'s own comment), so a bare pre-filled filename
// only resolves correctly if executeProgram() actually `cd`s into the
// file's own directory before running the command -- this reads
// sub.txt's REAL content via shell command substitution ($(cat ...)),
// which only succeeds if that resolution is correct. Deliberately run
// from `subdir/` (not `root`, which happens to already be roughly
// where a naive implementation might accidentally work by coincidence
// of the test process's own cwd).
TEST_CASE_FIXTURE(Fixture, "PROGRAM: the pre-filled filename actually resolves to the REAL file on disk "
                           "when executed, not whatever directory the process happened to launch in") {
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // descend into subdir/
  REQUIRE(app.directoryMode().currentDirectory() == (root / "subdir").string() + "/");

  idx = indexOf("sub.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  std::string outPath = (root / "prog_resolve_out.txt").string();
  REQUIRE(app.handleCommand(Command::StartProgram));
  REQUIRE(app.promptText() == "sub.txt");
  feedText("echo $(cat ");        // prepends -> "echo $(cat sub.txt"
  REQUIRE(app.feedByte(0x05, std::nullopt));  // ^E: end
  feedText(") > '" + outPath + "'");
  CHECK(app.feedByte(0x0d, std::nullopt));

  std::ifstream in(outPath);
  std::string content;
  std::getline(in, content);
  // $(cat sub.txt) inside echo (unquoted) word-splits sub.txt's own
  // "alpha\nbeta\nalpha\ngamma\n" (see the Fixture's own setup) into
  // separate echo arguments, rejoined with single spaces -- real shell
  // behavior, not a simplification made for this test.
  CHECK(content == "alpha beta alpha gamma");
}

TEST_CASE_FIXTURE(Fixture, "PROGRAM: ^G cancels the prompt without running anything") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartProgram));
  feedText("echo hello");
  CHECK(app.feedByte(0x07, std::nullopt));
  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

// -- Command::QuickLook (EXPERIMENTAL, 2026-08-23): plain Space --
// see that enumerator's own comment and dispatchByte()'s.
// setForegroundLauncher() intercepts the command string instead of
// letting ProgramMode::run() actually fork/exec `qlmanage -p` (which
// would pop a real Quick Look window and hang the test waiting for it
// to be dismissed) -- same technique test_program_mode.cpp's own
// launcher tests already use.

TEST_CASE_FIXTURE(Fixture, "Space (Directory mode): runs `qlmanage -p` on the entry under the cursor via "
                           "ProgramMode, instead of paging") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  std::string receivedCommand;
  app.programMode().setForegroundLauncher([&](const std::string &command) {
    receivedCommand = command;
    return true;
  });

  CHECK(app.feedByte(' ', std::nullopt));
  CHECK(receivedCommand == "qlmanage -p '" + path() + "plain.txt'");
  CHECK(app.currentMode() == ApplicationMode::Directory);  // never switches mode, same as PROGRAM

  const auto &entries = history.recentEntries(RingCategory::ShellCommand);
  REQUIRE_FALSE(entries.empty());
  CHECK(entries.back() == "qlmanage -p plain.txt");  // the SHORT recordedCommand, not the quoted one
}

// 2026-08-25 (a direct user request tying Quick Look into the same
// dired-style marking workflow already wired up for CP/MV/RM/PROGRAM):
// with more than one entry marked, Space passes ALL of them to
// `qlmanage -p` at once (real qlmanage(1) shows multiple paths as a
// browsable gallery) instead of just the cursor entry.
TEST_CASE_FIXTURE(Fixture, "Space (Directory mode, multiple marks): runs `qlmanage -p` on ALL marked "
                           "entries at once, not just the entry under the cursor") {
  REQUIRE(app.start(path()));
  int plainIdx = indexOf("plain.txt");
  int logIdx = indexOf("log.txt");
  REQUIRE(plainIdx >= 0);
  REQUIRE(logIdx >= 0);
  int idxA = std::min(plainIdx, logIdx);
  int idxB = std::max(plainIdx, logIdx);
  std::string nameA = (idxA == plainIdx) ? "plain.txt" : "log.txt";
  std::string nameB = (idxA == plainIdx) ? "log.txt" : "plain.txt";

  while (indexOf(app.directoryMode().selectedEntry()->name) != idxA) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));
  while (indexOf(app.directoryMode().selectedEntry()->name) != idxB) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  std::string receivedCommand;
  app.programMode().setForegroundLauncher([&](const std::string &command) {
    receivedCommand = command;
    return true;
  });

  CHECK(app.feedByte(' ', std::nullopt));
  CHECK(receivedCommand == "qlmanage -p '" + path() + nameA + "' '" + path() + nameB + "'");

  const auto &entries = history.recentEntries(RingCategory::ShellCommand);
  REQUIRE_FALSE(entries.empty());
  CHECK(entries.back() == "qlmanage -p " + nameA + " " + nameB);  // SHORT label, bare names
}

// 2026-08-23 follow-up: an earlier cut made Space mean "advance the
// CP/MV/RM selection" while armed instead of Quick Look -- a direct
// user report ("ハイライト表示での[space]は、やはりCP/MV/RM（とARC）
// で混乱を招く") dropped that overload entirely. Space means Quick
// Look UNCONDITIONALLY now; marking while armed is '.'/'['/']'
// exclusively (see their own tests, just above the PROGRAM section).
// 2026-08-27 update: the 2026-08-27 CP/MV/RM redesign (see
// Application::startCopyPrompt()'s own comment) removed the "armed but
// still browsing" state this test used to arm via StartCopy WITHOUT
// marking anything first (that call would now just no-op) -- marking
// an entry and pressing ^Xc jumps straight into the destination TEXT
// PROMPT, where Space would simply be typed as a literal character,
// not dispatched as a command at all. The still-real, still-worth-
// testing part of the original report survives without any CP
// involvement: marking an entry (via '.'/'['/']') must never make
// Space stop meaning Quick Look on it.
TEST_CASE_FIXTURE(Fixture, "Space (Directory mode, entry marked but CP not invoked): STILL runs Quick Look, "
                           "not marking") {
  REQUIRE(app.start(path()));

  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  std::string receivedCommand;
  app.programMode().setForegroundLauncher([&](const std::string &command) {
    receivedCommand = command;
    return true;
  });

  CHECK(app.feedByte(' ', std::nullopt));
  CHECK(receivedCommand == "qlmanage -p '" + path() + "plain.txt'");
  CHECK(app.directoryMode().isMarked(idx));  // mark UNCHANGED (still marked from above) -- Space doesn't toggle it
}

TEST_CASE_FIXTURE(Fixture, "Space (File mode): plain PageDown, unchanged -- not one of the covered modes "
                           "(a single already-open file, no per-line file identity)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  bool launcherCalled = false;
  app.programMode().setForegroundLauncher([&](const std::string &) {
    launcherCalled = true;
    return true;
  });
  app.feedByte(' ', std::nullopt);
  CHECK_FALSE(launcherCalled);
  CHECK(app.currentMode() == ApplicationMode::File);
}

// 2026-08-23 widening (docs/CHANGES.md item 415, a direct user
// request: "LOCATE、FGREP等、ハイライト状態のレギュラーファイルなら
// どこでも発動できる方針で"): LOCATE results are bare real paths.
TEST_CASE_FIXTURE(Fixture, "Space (Locate mode): runs `qlmanage -p` on the highlighted result") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText(path() + "plain.txt");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not installed or no database available -- skipping");
    return;
  }
  if (app.currentMode() != ApplicationMode::Locate || !app.locateMode().selectedResult()) {
    MESSAGE("locate found no results for this fixture's own temp path -- skipping");
    return;
  }
  std::string expected = *app.locateMode().selectedResult();

  std::string receivedCommand;
  app.programMode().setForegroundLauncher([&](const std::string &command) {
    receivedCommand = command;
    return true;
  });
  CHECK(app.feedByte(' ', std::nullopt));
  CHECK(receivedCommand == "qlmanage -p '" + expected + "'");
}

// Grep's own coverage is split across TWO tests -- see
// Application::executeQuickLook()'s own comment for why only a
// Directory-ORIGIN Grep (matched rows == real directory entries) is
// covered; a File-origin Grep (matched LINES within one already-known
// file) deliberately is not.
TEST_CASE_FIXTURE(Fixture, "Space (Grep mode, Directory origin): runs `qlmanage -p` on the directory "
                           "entry the highlighted match came from") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);

  std::string receivedCommand;
  app.programMode().setForegroundLauncher([&](const std::string &command) {
    receivedCommand = command;
    return true;
  });
  CHECK(app.feedByte(' ', std::nullopt));
  CHECK(receivedCommand == "qlmanage -p '" + path() + "plain.txt'");
}

TEST_CASE_FIXTURE(Fixture, "Space (Grep mode, File origin): plain PageDown -- a match here is a LINE within "
                           "the one already-open file, not a distinct file of its own") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("hello");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);

  bool launcherCalled = false;
  app.programMode().setForegroundLauncher([&](const std::string &) {
    launcherCalled = true;
    return true;
  });
  app.feedByte(' ', std::nullopt);
  CHECK_FALSE(launcherCalled);
}

// ListBrowser is deliberately NOT covered at all, even for RecentFile/
// RecentDirectory (whose candidates ARE bare real paths) -- see
// Application::executeQuickLook()'s own comment: those two categories
// are the "live combobox" ones (feedRegexListByte()'s own routing,
// checked BEFORE this dispatch point is ever reached), where plain
// Space is already meaningfully "start editing, beginning with a
// literal space character". Both a path-shaped category (RecentFile)
// and a non-path one (ProgramCommand) are checked here, since the
// REASON differs (live-combobox routing vs. simply not a filesystem
// path) even though the observable result -- plain PageDown -- is the
// same for both.
TEST_CASE_FIXTURE(Fixture, "Space (ListBrowser, RecentFile): plain PageDown -- Space is already the "
                           "live-combobox \"start editing\" key there, not overridden") {
  history.pushRecent(RingCategory::RecentFile, (root / "target.txt").string());
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFileHistory));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  bool launcherCalled = false;
  app.programMode().setForegroundLauncher([&](const std::string &) {
    launcherCalled = true;
    return true;
  });
  app.feedByte(' ', std::nullopt);
  CHECK_FALSE(launcherCalled);
}

// 2026-09-11追加、直接のユーザー報告("CLAUDE.mdを選択→^X^F：戻り値
// 「.../CLAUDE.md」→^S：「.../git/usilic/」"): feedRegexListByte()は
// promptHistoryContext_(returnModeを保持)を、activatePrompt()(→
// drawPromptLabel())を呼ぶ「前に」クリアしていたため、
// PromptKind::RecentFile/RecentDirectory向けのdestinationMode計算が
// 常にnulloptを読んで無条件にApplicationMode::Directoryへ落ちていた
// ――起点がDirectory自身だった場合だけ偶然正しく見えていた。
// 2026-09-11再訂正: このテストは元々トリガーに^S(0x13)を使っていたが、
// 直後の項目442で^Sの意味がRecentFile/RecentDirectory限定で「その場で
// パスを編集開始する」から「in-place GREPを起動する」へ変わったため、
// 素直な印字可能文字('x')に差し替えた――このテスト自体が検証したい
// のはdestinationMode計算のバグであり、トリガーが具体的にどの文字かは
// 無関係。
TEST_CASE_FIXTURE(Fixture, "typing a plain character (ListBrowser, RecentFile, live-combobox \"start editing\"): "
                           "the mode line's destination/return-value field reflects the ACTUAL return mode "
                           "(File), not always the ApplicationMode::Directory fallback") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string filePath = app.fileMode().currentFile();

  REQUIRE(app.handleCommand(Command::StartFileHistory));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  app.feedByte('x', std::nullopt);  // not a nav key, starts live-editing the path
  CHECK(app.currentMode() == ApplicationMode::File);  // returnMode correctly restored, not left on Directory

  // modeLineText() recomputes fresh from the underlying view's own
  // modeLineFieldsProvider, which knows nothing about a still-open,
  // not-yet-submitted prompt -- drawPromptLabel()'s own destinationMode
  // text is written directly to the display, so read that row instead
  // (same technique the =gf.png/PTY-verification work in this session
  // used throughout).
  std::string row = display.rowText(display.rows() - 1);
  CHECK(row.find(filePath) != std::string::npos);
}

// 2026-09-11追加、直接のユーザー依頼("やりたいのはLOCATEと同じ。Resの
// 3文字が`...`にあるやつ"): ^X^F/^X^Dの候補一覧内でも^Sがin-place GREP
// (LOCATE/Directory/File等と同じ、専用の一覧画面へは切り替わらない
// マッチハイライト+^N/^P)を起動するようにした――以前は^Sもまさに
// 上のテストと同じ「その場でパスを編集開始する」経路(結局^Sという
// 制御バイト自体は`^Qクォート必須ルール`により何も挿入されず単なる
// ビープで終わる)にしか入れなかった。
TEST_CASE_FIXTURE(Fixture, "^S (ListBrowser, RecentFile): triggers in-place GREP within the candidate list "
                           "itself, staying in ListBrowser mode (not the old \"start editing\" path)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartFileHistory));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  REQUIRE(app.feedByte(0x13, std::nullopt));  // ^S -> in-place GREP, not "start editing"
  feedText("log");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // submit

  CHECK(app.currentMode() == ApplicationMode::ListBrowser);  // never leaves the candidate list
  const std::string &modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.find("GREP") != std::string::npos);
  CHECK(modeLine.find("(grep)") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "Space (ListBrowser, a non-path category like ProgramCommand): plain PageDown") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartProgram));
  feedText("echo hi");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  app.feedByte(0x0a, std::nullopt);  // ^J
  REQUIRE(app.feedByte(0x10, std::nullopt));  // ^P: ^J^P, StartProgramList -- opens the PROGRAM candidate list
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  bool launcherCalled = false;
  app.programMode().setForegroundLauncher([&](const std::string &) {
    launcherCalled = true;
    return true;
  });
  app.feedByte(' ', std::nullopt);
  CHECK_FALSE(launcherCalled);
}

// -- feedByte: ^Jl / ^J^L leader sequences for LOCATE --
//
// These genuinely exercise the real `locate`(1) command (matching
// this project's preference for real execution), but -- as
// LocateMode's own tests already document -- `locate` depends on a
// system database this project doesn't control the presence of, so
// they skip gracefully rather than asserting specific results exist,
// the same pattern test_locate_mode.cpp already uses.

// 2026-08-13 (docs/ideas/IDEAS.md's LOCATE section, CLAUDE.md history
// item 288): ^J l and ^J ^L are no longer the same command. ^J l now
// runs `updatedb` (LocateUpdateDb) and never opens a prompt; ^J ^L
// keeps the "open a prompt" role, now seeded with the previous query.

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^L starts a LOCATE prompt (StartLocate)") {
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

// 2026-08-13 (CLAUDE.md history item 291's mode-line audit):
// locateView_ had no ModeLineFieldsProvider at all before this round.
TEST_CASE_FIXTURE(Fixture, "LOCATE: mode line shows 'LOCATE' and a byte count once results are shown") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("/");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not installed or no database available -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  const std::string &modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.find("LOCATE") != std::string::npos);
  std::size_t expectedBytes = 0;
  for (const auto &line : app.activeContentView().lines()) expectedBytes += line.text.size() + 1;
  CHECK(modeLine.find(std::to_string(expectedBytes)) != std::string::npos);
}

// 2026-08-14 fix: a real report ("^J^L→'.txt'[return]→戻り値「LOCATE:
// .txt」が無い") -- the results screen's own trailing2 used to be
// entirely empty. 2026-09-19 re-reversal, a direct user report +
// comparison against GREP's own equivalent fix ("あらたな重複発見...
// 反映はGREPのケースと同じでいいんだが...発動元のファイルもしくは
// ディレクトリでお願いしたい"): showing "LOCATE: <query>" here read as
// the exact same "same word twice" duplication just fixed for GREP
// (name is already the literal "LOCATE"). Now shows the resolved
// return anchor (nearestFileOrDirectoryAnchor(), bare, no prefix) --
// this fixture starts and searches directly from Directory, so the
// anchor is the current directory.
TEST_CASE_FIXTURE(Fixture, "LOCATE: mode line's trailing2 shows the resolved return anchor (bare, no "
                           "\"LOCATE: \" prefix), padded with spaces to the row's right edge") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText(".txt");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not installed or no database available -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  std::string anchor = app.directoryMode().currentDirectory();
  std::string row = display.rowText(23);
  CHECK(row.find("LOCATE: .txt") == std::string::npos);  // the old, now-redundant convention
  std::size_t pos = row.find(anchor);
  REQUIRE(pos != std::string::npos);
  std::string afterAnchor = row.substr(pos + anchor.size());
  CHECK(afterAnchor.size() > 4);                                    // padded well past a single trailing space
  CHECK(afterAnchor.find_first_not_of(' ') == std::string::npos);   // and it's ALL spaces, reaching the row's edge
}

// 2026-09-20追加、直接のユーザー報告("...※※※※             "より
// "...※※※※※※※※※※※"でよろしく、理由も無いのになぜパディング
// するんだろう"): この値は以前trailing2に入っており、trailing2Kindの
// デフォルト(QueryEcho)経由でtruncateFieldByKind()が「実際の余裕に
// 関わらず16文字で頭打ち」にしてしまっていた――LOCATE自身のこの
// モードラインは(drawPromptLabel()のプロンプト行とは違い)文字入力
// エリアと幅を奪い合う理由が無いので、MAN/DU自身のtrailing3
// (withTruncatedTrailing3()、実際の利用可能幅をそのまま使い切る)
// と全く同じ形に揃えた。このテストは、恣意的な16文字上限を明確に
// 超える(かつ画面には十分な余裕がある)基点パスが、切り詰められず
// 全文表示されることを確認する。
TEST_CASE_FIXTURE(Fixture, "LOCATE: mode line's return-anchor value is NOT artificially capped at 16 chars "
                           "when more width is actually available (no competing text-input area exists here)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string filePath = app.fileMode().currentFile();
  REQUIRE(static_cast<int>(filePath.size()) > 16);  // must genuinely exceed the old artificial cap to be meaningful

  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("/");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not available -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  CHECK(app.activeContentView().modeLineText().find(filePath) != std::string::npos);
}

// 2026-08-18 fix: a real report ("^J^L'.txt'[return]=co→^S'.txt'[return]
// →モードライン不良かつ検索してない") -- FGREP started from Locate
// results (startGrepPrompt() has no mode restriction beyond blocking
// re-entry from Grep itself) used to hardcode
// "grepReturnMode_==File ? fileView_ : directoryView_" in executeGrep(),
// so a search launched from Locate silently read (and, on failure,
// redrew) directoryView_'s stale content instead of the LOCATE results
// actually on screen. Distinguishes the two buffers by searching for
// one of the LOCATE results' OWN full lines (a real absolute path from
// the system locate database) as a FIXED pattern: the fixture's
// directory listing only ever contains short bare filenames like
// "plain.txt", never a matching absolute path, so this only finds a
// match if the search actually ran against locateView_.
TEST_CASE_FIXTURE(Fixture, "FGREP started from LOCATE results (^S) searches the LOCATE results themselves, "
                           "not the stale directory listing behind them") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText(".txt");  // narrow query, matching the sibling LOCATE tests -- "/" alone can return
                      // millions of entries on a real machine, far too slow/unwieldy for a unit test
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not installed or no database available -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  const auto &locateLines = app.activeContentView().lines();
  if (locateLines.empty()) {
    MESSAGE("locate database returned no '.txt' results -- skipping");
    return;
  }
  // Longest line, not front(): a short path (e.g. a top-level directory)
  // risks being a substring of many OTHER locate lines too, making the
  // match count unpredictable across machines -- the longest result is
  // the most specific/least likely to collide, so a single exact match
  // is the realistic expectation.
  std::string knownLocateLine =
      std::max_element(locateLines.begin(), locateLines.end(),
                        [](const auto &a, const auto &b) { return a.text.size() < b.text.size(); })
          ->text;

  REQUIRE(app.handleCommand(Command::StartFgrep));
  CHECK(app.promptActive());
  feedText(knownLocateLine);
  REQUIRE(app.feedByte(0x0d, std::nullopt));

  CHECK(app.currentMode() == ApplicationMode::Grep);
  // Not garbled by a mismatched-view redraw: the results screen's own
  // provider (grepView_'s) is now live, showing "FGREP", not whatever
  // directoryView_'s "(ls)"-shaped provider would have left behind.
  CHECK(app.activeContentView().modeLineText().substr(0, 5) == "FGREP");
  const auto &matches = app.grepMode().matches();
  REQUIRE_FALSE(matches.empty());
  bool foundExactLine = false;
  for (const auto &match : matches) {
    if (match.text == knownLocateLine) foundExactLine = true;
  }
  CHECK(foundExactLine);
}

// 2026-08-13 (CLAUDE.md history item 293's mode-line fix): a real ^R
// with nothing recorded yet used to show ContentView's own DEFAULT
// empty-list placeholder ("--", CUT's own convention) here -- wrong
// for a plain history ring. Now shares GrepMode's own "No String"
// (underlined, non-reversed) convention instead, just without a
// pattern prefix. Exercised via LOCATE here, but the fix is on
// listBrowserView_ itself, shared by every ^R/^X^F/^X^D/^J^R history
// screen.
TEST_CASE_FIXTURE(Fixture, "LOCATE: real ^R with no history yet shows \"No String\", not CUT's own \"--\"") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  REQUIRE(app.feedByte(0x12, std::nullopt));  // ^R: real history recall
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK(app.listBrowserMode().items().empty());
  std::string row = display.rowText(0);
  CHECK(row.substr(0, 9) == "No String");
  CHECK(row.substr(0, 2) != "--");
}

TEST_CASE_FIXTURE(Fixture, "LOCATE: ^J ^L seeds the prompt with the previous query") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("/");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not installed or no database available -- skipping");
    return;
  }
  REQUIRE(app.handleCommand(Command::Back));  // ^G back to Directory
  REQUIRE(app.handleCommand(Command::StartLocate));
  CHECK(app.promptActive());
  CHECK(app.promptText() == "/");
}

TEST_CASE_FIXTURE(Fixture, "LOCATE: submitting the seeded query unchanged redisplays without re-running locate") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("/");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not installed or no database available -- skipping");
    return;
  }
  std::vector<std::string> firstResults = app.locateMode().results();
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.handleCommand(Command::StartLocate));
  REQUIRE(app.promptText() == "/");  // the seed, unedited
  CHECK(app.feedByte(0x0d, std::nullopt));  // submit unchanged
  CHECK(app.currentMode() == ApplicationMode::Locate);
  CHECK(app.locateMode().results() == firstResults);
}

// 2026-09-19再訂正、MAN自身の同種のテストの直上のコメントと同じ方針
// 転換――2026-09-15の「常に基点のDirectory」という判断を撤回する。
TEST_CASE_FIXTURE(Fixture, "LOCATE: ^G returns to the normal FILE it was invoked from (not the base "
                           "Directory)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string filePath = app.fileMode().currentFile();

  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("/");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not available -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  CHECK(app.activeContentView().modeLineText().find(filePath) != std::string::npos);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == filePath);
}

// 2026-09-19追加、MAN自身の同種のテストと対をなすLOCATE版――直接の
// ユーザー依頼("LOCATEも同じなので対応してほしい")。
// 2026-09-11更新、直接のユーザー依頼("^Sと^[iをFILE/DIR以外(現状GREP
// が稼働している全てのモード)へも反映してほしい")によりLOCATE起点の
// ^SもDirectory/Fileと同じin-place方式(専用の一覧Grepへは切り替えず、
// LOCATE自身の画面にマッチをハイライト表示したまま留まる)になった。
// これに伴い、以前あった「Grep結果画面からBackでLOCATEへ戻り、さらに
// Backでその手前(File)へ」という2段階の戻りは、LOCATE自身が最初から
// 一度も離れていない(currentMode_はLocateのまま)ため単純化される――
// マッチを1件も選ばずにBackすると、LOCATE自身の通常のBack先(File)へ
// 直接戻る。このテストの本来の関心事(「マッチを選ばずにBackしても、
// 起点ファイルのパスが正しく示される」)自体は変わらず引き続き検証
// できる。
TEST_CASE_FIXTURE(Fixture, "LOCATE started from File mode: backing out of an in-place grep search run "
                           "against the results WITHOUT selecting a match still correctly shows the "
                           "origin file's path (not the base Directory)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string filePath = app.fileMode().currentFile();

  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText(".md");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not installed or no database available -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  if (app.activeContentView().lines().empty()) {
    MESSAGE("locate found no '.md' results -- skipping");
    return;
  }

  REQUIRE(app.handleCommand(Command::StartGrep));
  feedText(".md");  // guaranteed to match every line, see the sibling GREP-from-Locate test's own comment
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  // in-place: currentMode_はLocateのまま、専用のGrep一覧へは切り替わらない。
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  REQUIRE_FALSE(app.locateMode().results().empty());

  // Back WITHOUT selecting a match -- 一度もLocateを離れていないので、
  // LOCATE自身の通常のBack先(File)へ直接戻る。
  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == filePath);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

// 2026-09-19の方針転換後も、この特定のシナリオの期待値自体は変わらない
// (このテストがまだ生きている理由): MANはDirectoryから素で発動されて
// おり(nearestFileOrDirectoryAnchor()はcurrentMode_自体が既に
// Directoryなのでそのまま返す)、manReturnMode_=Directory。その状態で
// LOCATEを発動すると、nearestFileOrDirectoryAnchor()はcurrentMode_
// (Man)→manReturnMode_(Directory、終端)と辿り、やはりDirectoryへ
// 行き着く――「MANを飛び越えてDirectoryへ」という2026-09-15の元々の
// 報告そのものが、新しい遡り方式でも(この特定の入れ子では)同じ結論
// になる好例。
TEST_CASE_FIXTURE(Fixture, "LOCATE: started from Man mode (itself started from Directory), ^G returns to "
                           "the base Directory mode, not back to Man") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartMan));
  feedText("ls");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("man not installed or 'ls' has no man page here -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Man);

  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("/");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not available -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Locate);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

// 2026-09-15追加、直接のユーザー方針("LOCATEおよびMAN等からDUへ移行
// できず...方針にそぐわない")への対応: runDuOnCurrentFolder()自身の
// 新設のApplicationMode::Locate分岐参照――ハイライト中の結果自身の
// フォルダー(ディレクトリならそれ自身、ファイルならその親)でduを
// 実行する。
TEST_CASE_FIXTURE(Fixture, "handleCommand(StartDu): works from LOCATE too, tallying the highlighted "
                           "result's own folder") {
  fs::path target = root / "subdir" / "sub.txt";
  fs::path fakeLocate = root / "fake_locate_du.sh";
  {
    std::ofstream script(fakeLocate);
    script << "#!/bin/sh\necho '" << target.string() << "'\n";
  }
  std::filesystem::permissions(fakeLocate, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
  configuration.setCommandForTest(ConfiguredRole::Locate, {{fakeLocate.string()}, std::nullopt});

  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("anything");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  REQUIRE(app.locateMode().results().size() == 1);

  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  CHECK(app.currentMode() == ApplicationMode::Du);
  CHECK(app.duMode().currentPath() == (root / "subdir").string() + "/");
}

// 2026-08-14 (CLAUDE.md history item 297): a direct user report
// ("ハイライト行で[return]が無反応") -- LocateMode's own class comment
// had explicitly deferred this ("Deliberately excluded (future work,
// not this slice): deciding what to do with a selected result").
// Mirrors DU's own identical Directory-vs-File dispatch (see
// openSelectedDuEntry()'s own tests) -- since `locate` searches the
// REAL system database, the actual TYPE (directory or file) of
// whatever it returns first isn't predictable from here, so this
// checks the real filesystem itself to decide which branch applies,
// same "test against real state, skip gracefully if unavailable"
// convention every other real-`locate` test in this file already
// uses.
TEST_CASE_FIXTURE(Fixture, "LOCATE: Enter on a result opens it (Directory or File, matching the real filesystem)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("/");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not available -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  if (app.locateMode().results().empty()) {
    MESSAGE("locate returned no results -- skipping");
    return;
  }
  std::string selected = app.locateMode().results()[0];
  REQUIRE(app.handleCommand(Command::Enter));
  std::error_code ec;
  if (std::filesystem::is_directory(selected, ec) && !ec) {
    CHECK(app.currentMode() == ApplicationMode::Directory);
    // 2026-08-14 fix: see openSelectedLocateResult()'s own comment --
    // landing here via a LOCATE match now leaves a trace of the search
    // that led here, mirroring EGREP/CUT's own setSearchAnnotation()
    // convention exactly.
    CHECK(app.activeContentView().modeLineText().find("LOCATE: /") != std::string::npos);
  } else {
    CHECK(app.currentMode() == ApplicationMode::File);
    CHECK(app.fileMode().currentFile() == selected);
    // Path (not the bare query), per openSelectedLocateResult()'s own
    // comment -- a real report ("...戻り値「LOCATE: ...s/Adobe/
    // HKSCS.txt」が無い（旧版と違うので注意）") expected the located
    // path itself, not the short query, since FileMode's name field is
    // already just the bare filename with nothing else showing where
    // it actually lives. Only checked when it plausibly fits the row
    // (real `locate` results can be arbitrarily long, and a too-wide
    // annotation is legitimately dropped by fitModeLineFields()'s own
    // width-fit logic rather than shown truncated) -- the fixed-string
    // "LOCATE: <path exactly>" test below covers the mechanism itself
    // without depending on a real filesystem's path lengths.
    if (selected.size() < 50) {
      CHECK(app.activeContentView().modeLineText().find("LOCATE: " + selected) != std::string::npos);
    }
  }
}

// Deterministic counterpart to the real-filesystem test just above
// (the "selected.size() < 50" guard there can legitimately skip the
// annotation check for a long real `locate` match): a fake ConfiguredRole::
// Locate command that always returns one fixed, short, known path, so
// the annotation AND the byte-count-not-dropped fix (fitModeLineFields()'s
// new fallback tier, see its own comment) can both be checked exactly,
// unconditionally.
// 2026-08-14 fix (two direct user corrections): the name field is
// "LOCATE" (not the bare filename) and the trailing2 field is the raw
// path with NO "LOCATE: " prefix (not "LOCATE: <path>") -- see
// FileMode::setSearchOrigin()'s own comment. Also: ^G from here
// returns to LOCATE's own results list, not straight to (ls)
// (fileReturnMode_, see its own declaration comment).
TEST_CASE_FIXTURE(Fixture, "LOCATE: Enter on a File result sets name=\"LOCATE\" + the raw path as trailing2, "
                           "still shows the byte count, and ^G returns to LOCATE results") {
  fs::path target = root / "found.txt";
  { std::ofstream(target) << "hello"; }  // 5 bytes
  fs::path fakeLocate = root / "fake_locate.sh";
  {
    std::ofstream script(fakeLocate);
    script << "#!/bin/sh\necho '" << target.string() << "'\n";
  }
  std::filesystem::permissions(fakeLocate, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
  configuration.setCommandForTest(ConfiguredRole::Locate, {{fakeLocate.string()}, std::nullopt});

  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("anything");  // ignored by the fake script -- always returns target
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  REQUIRE(app.locateMode().results() == std::vector<std::string>{target.string()});

  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.fileMode().currentFile() == target.string());
  const std::string &modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.substr(0, 6) == "LOCATE");  // the name field itself, not the bare filename
  CHECK(modeLine.find(target.string()) != std::string::npos);  // the raw path, no "LOCATE: " prefix
  CHECK(modeLine.find("5") != std::string::npos);  // the byte count, no longer dropped alongside the long path

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Locate);
}

// 2026-08-24 addition (docs/CHANGES.md item 418, a direct user
// request: "LOCATE、FGREP等「.mp4」マッチ一覧上でも可能で"): mirrors
// the Directory-mode EXEC tests below (same harmless "sh -c echo >>
// marker" trick) -- a suffix-mapped LOCATE result should run EXEC
// directly instead of always falling into plain FileMode viewing.
TEST_CASE_FIXTURE(Fixture, "LOCATE: Enter on a result for an EXEC-mapped suffix runs EXEC, staying in "
                           "Locate mode (not File mode)") {
  configuration.loadRcText("EXEC\n .marker sh -c 'echo EXECRAN >> \"$0\"'\n", true);
  fs::path target = root / "target.marker";
  { std::ofstream(target) << "original\n"; }
  fs::path fakeLocate = root / "fake_locate.sh";
  {
    std::ofstream script(fakeLocate);
    script << "#!/bin/sh\necho '" << target.string() << "'\n";
  }
  std::filesystem::permissions(fakeLocate, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
  configuration.setCommandForTest(ConfiguredRole::Locate, {{fakeLocate.string()}, std::nullopt});

  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("anything");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  REQUIRE(app.locateMode().results() == std::vector<std::string>{target.string()});

  CHECK(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Locate);  // stayed here, unlike a plain (non-EXEC) file result

  std::ifstream in(target);
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(content.find("EXECRAN") != std::string::npos);
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

// 2026-09-20追加、直接のユーザー報告(実際の再現手順):
// `usilic -c ~/git/usilic`→`^J^L.txt^M^M`(結果を直接Fileとして開く)
// →`^J^L^U.md^M^M`(そのFileから「もう一度」LOCATEを発動し、また
// 結果を直接Fileとして開く)→`^G`連打で行き来ループ、加えて
// "LOCATE中のLOCATE発動も最初のLOCATEまで履歴抹消でよろしく"という
// 明示的な依頼。startLocatePrompt()自身のstillInLocateRealm分岐の
// コメント参照――openSelectedLocateEntry()相当の処理でLOCATE結果を
// 直接Fileとして開くと(fileReturnMode_==Locate)、そのFileから
// 「もう一度」LOCATEを発動した際、nearestFileOrDirectoryAnchor()が
// currentMode_==Fileの時点で即座にFile自身を返してしまうため、以前は
// このFile自体を「新しい」基点として再計算し、その際に古いLOCATE
// セッションへの参照(fileReturnMode_==Locate)をうっかり凍結して
// しまっていた――結果、後でこのFileへ戻ってきた時にfileReturnMode_が
// Locateへ復元され続け、^Gを連打してもFile/LOCATEの間を永遠に往復
// するだけでDirectoryへ辿り着けなかった。
TEST_CASE_FIXTURE(Fixture, "LOCATE started from a File that was ITSELF reached via a previous LOCATE result: "
                           "re-invoking LOCATE skips the intermediate File entirely, converging straight back "
                           "to the ORIGINAL anchor without bouncing between File and Locate forever") {
  fs::path target1 = root / "located1.txt";
  { std::ofstream(target1) << "one"; }
  fs::path target2 = root / "located2.txt";
  { std::ofstream(target2) << "two"; }
  fs::path fakeLocate1 = root / "fake_locate1.sh";
  { std::ofstream(fakeLocate1) << "#!/bin/sh\necho '" << target1.string() << "'\n"; }
  fs::path fakeLocate2 = root / "fake_locate2.sh";
  { std::ofstream(fakeLocate2) << "#!/bin/sh\necho '" << target2.string() << "'\n"; }
  std::filesystem::permissions(fakeLocate1, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
  std::filesystem::permissions(fakeLocate2, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);

  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string originalFile = app.fileMode().currentFile();

  configuration.setCommandForTest(ConfiguredRole::Locate, {{fakeLocate1.string()}, std::nullopt});
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("anything");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  REQUIRE(app.locateMode().results() == std::vector<std::string>{target1.string()});

  REQUIRE(app.handleCommand(Command::Enter));  // opens target1 directly as File
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.fileMode().currentFile() == target1.string());

  // Nested invocation: LOCATE again, from this SAME File (itself
  // reached via the first LOCATE).
  configuration.setCommandForTest(ConfiguredRole::Locate, {{fakeLocate2.string()}, std::nullopt});
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("anything");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  REQUIRE(app.locateMode().results() == std::vector<std::string>{target2.string()});

  REQUIRE(app.handleCommand(Command::Enter));  // opens target2 directly as File
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.fileMode().currentFile() == target2.string());

  // Mash Back: File(target2) -> Locate (2nd session's own results) ->
  // straight to the ORIGINAL file (target1 is skipped entirely, never
  // revisited) -> Directory. Must NOT loop between File and Locate.
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Locate);
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == originalFile);  // NOT target1
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
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

// 2026-08-26 fix, a real report ("内容一覧上の選択ファイルがアンダー
// ライン表示にならない"): ArchiveMode's own constructor was missing
// setMarkedRowUsesUnderline(true), the exact same pairing
// DirectoryMode's own constructor makes right alongside
// setMarkedPredicate() (see its own comment) -- a marked entry's own
// Standout was indistinguishable from the cursor row's ordinary
// Standout, same class of bug the '[' marking test above already
// covers for DirectoryMode.
TEST_CASE_FIXTURE(Fixture, "ARC: a marked entry shows Underline, not just Standout, distinct from the "
                           "cursor row's own plain Standout") {
  REQUIRE(app.start(path()));
  int dirIdx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != dirIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Archive);

  int markIdx = archiveIndexOf(app, "inner.txt");
  REQUIRE(markIdx >= 0);
  while (app.archiveMode().selectedEntry() != nullptr &&
         app.archiveMode().selectedEntry()->name.find("inner.txt") == std::string::npos) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.handleCommand(Command::ToggleMark));
  REQUIRE(app.archiveMode().isMarked(markIdx));
  // 2026-08-28 note: this archive fixture has only 2 entries, and
  // markIdx is the LAST one -- MoveDown() here can't actually advance
  // (ContentView::moveDown()'s own "already at bottom" case just beeps,
  // see its own comment), so the cursor stays ON markIdx. Still a valid
  // check: it confirms marking wins outright over the cursor row's own
  // plain Standout even when they're the SAME row (see redraw()'s own
  // "cursorRow && !markedRow" comment).
  REQUIRE(app.handleCommand(Command::MoveDown));

  int nameCol = app.activeContentView().lines()[markIdx].highlightStartColumn.value_or(0);
  CHECK(display.underlineAt(markIdx, nameCol));
  CHECK_FALSE(display.standoutAt(markIdx, nameCol));
}

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

  // 2026-08-13 (CLAUDE.md history item 291's mode-line audit):
  // archiveContentView_ had no ModeLineFieldsProvider at all before
  // this round -- mirrors fileView_'s own shape (bare entry name +
  // byte size + the archive's own path), not the bare byte-count-only
  // shape MAN/LOCATE/DU use just above (see application.cpp's own
  // comment on why).
  const std::string &modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.find("inner.txt") != std::string::npos);
  CHECK(modeLine.find("archive.tar") != std::string::npos);
  std::size_t expectedBytes = 0;
  for (const auto &line : app.activeContentView().lines()) expectedBytes += line.text.size() + 1;
  REQUIRE(expectedBytes > 0);
  CHECK(modeLine.find(std::to_string(expectedBytes)) != std::string::npos);
}

// 2026-08-26 fix, a real report ("一覧から選んだファイル内容がハイ
// ライト表示（←不要）かつ80桁分しか表示されない"): openSelectedArchiveEntry()
// was missing setCursorHighlightEnabled(false) (every other plain-
// content view -- FileMode/ManMode/CutMode's own -- turns this off,
// see file_mode.cpp's own constructor comment) and never wrapped
// content wider than the terminal at all (a raw std::getline() split,
// unlike FileMode's own readAndWrap() via PipeBuffer).
TEST_CASE_FIXTURE(Fixture, "ARC: entry content view has cursor highlight disabled and wraps long lines, "
                           "matching FileMode's own plain-pager convention") {
  fs::path wideDir = root / "widearc";
  fs::create_directory(wideDir);
  std::string longLine(200, 'x');
  { std::ofstream(wideDir / "wide.txt") << longLine << "\n"; }
  std::string tarCmd = "cd '" + root.string() + "' && tar cf widearc.tar widearc";
  REQUIRE(std::system(tarCmd.c_str()) == 0);

  REQUIRE(app.start(path()));
  int idx = indexOf("widearc.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));

  while (app.archiveMode().selectedEntry() != nullptr &&
         app.archiveMode().selectedEntry()->name.find("wide.txt") == std::string::npos) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.archiveMode().selectedEntry() != nullptr);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::ArchiveContent);

  CHECK_FALSE(app.activeContentView().cursorHighlightEnabled());
  // The 200 'x' content line must be wrapped across multiple display
  // lines, not silently cut off at the terminal's own width -- every
  // 'x' from the original content must still be present somewhere.
  std::size_t totalX = 0;
  for (const auto &line : app.activeContentView().lines())
    totalX += static_cast<std::size_t>(std::count(line.text.begin(), line.text.end(), 'x'));
  CHECK(totalX == 200);
  CHECK(app.activeContentView().lines().size() > 1);
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

// -- ARC extraction to a real destination (K-58, ^Oa -- K-57/^X^Ka
// "with SHELL window" REMOVED 2026-08-23, a direct user request once
// executeArchiveExtract() was unified to always run via shell_'s own
// real SHELL session, same redesign as CP/MV/RM's, see its own
// comment) --

TEST_CASE_FIXTURE(Fixture, "ARC extract (^Oa, K-58, Directory mode): extracts the WHOLE archive to the destination") {
  REQUIRE(app.start(path()));
  int idx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  CHECK(app.promptActive());
  feedText((root / "arcdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpArchiveExtractToCompletion(app));

  CHECK(fs::exists(root / "arcdest" / "arcsrc" / "inner.txt"));
  CHECK(fs::exists(root / "archive.tar"));  // untouched by extraction
}

// 2026-09-08 addition, a direct user request ("CP/MV/ARCモードで複写/
// 移動/展開先ディレクトリへ移動したあと、対象ファイルおよびディレク
// トリを^[Pへも渡せるようアンダーライン表示して欲しい"): unlike CP/MV
// (whose targets are known ahead of time from collectTargetPaths()),
// ARC determines its "target" via a before/after directory listing
// diff (PendingArchiveExtractShellRun::entriesBeforeExtract) -- this
// test seeds an unrelated pre-existing file in the destination to
// confirm it's correctly excluded (it was already there, not newly
// extracted), while the archive's own top-level entry (arcsrc/) is
// included.
TEST_CASE_FIXTURE(Fixture, "ARC extract: only the newly-extracted top-level entry is shown Underlined, "
                           "a pre-existing unrelated file in the destination is not") {
  { std::ofstream(root / "arcdest" / "preexisting.txt") << "already here\n"; }
  REQUIRE(app.start(path()));
  int idx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  feedText((root / "arcdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpArchiveExtractToCompletion(app));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);

  int newIdx = indexOf("arcsrc/");
  REQUIRE(newIdx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != newIdx) app.handleCommand(Command::MoveDown);
  int newNameCol = app.activeContentView().lines()[newIdx].highlightStartColumn.value_or(0);
  CHECK(display.underlineAt(newIdx, newNameCol));

  int oldIdx = indexOf("preexisting.txt");
  REQUIRE(oldIdx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != oldIdx) app.handleCommand(Command::MoveDown);
  int oldNameCol = app.activeContentView().lines()[oldIdx].highlightStartColumn.value_or(0);
  CHECK_FALSE(display.underlineAt(oldIdx, oldNameCol));
}

// 2026-09-09 addition, a direct user report ("~/git/usilic/にある.json
// ファイル群を/tmp/にCP...このあとARCを実行後、展開先のファイル・
// ディレクトリにはアンダーラインが付かないので^Lでクリアされるで
// あろう何らかのフラグがONのままなのかな"): reproduces a CP landing
// in a directory (leaving transientMarkedNames_ non-empty there),
// followed by an ARC extraction into a DIFFERENT destination WITHOUT
// an intervening ^L -- checks whether the leftover marking from the
// first operation somehow blocks the second one's own Underline.
TEST_CASE_FIXTURE(Fixture, "ARC extract following a CP (no ^L in between): the ARC destination still shows "
                           "its own Underline, unaffected by the earlier CP's leftover marking") {
  fs::copy_file(root / "archive.tar", root / "cpmvdest" / "archive.tar");
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));
  REQUIRE(app.directoryMode().currentDirectory() == (root / "cpmvdest").string() + "/");

  // cpmv_a.txt is now shown Underlined here (the CP's own post-transfer
  // marking) -- confirm the precondition before moving on, deliberately
  // WITHOUT pressing ^L first (matching the user's exact report).
  int cpIdx = indexOf("cpmv_a.txt");
  REQUIRE(cpIdx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != cpIdx) app.handleCommand(Command::MoveDown);
  int cpNameCol = app.activeContentView().lines()[cpIdx].highlightStartColumn.value_or(0);
  REQUIRE(display.underlineAt(cpIdx, cpNameCol));

  // Now select archive.tar (also sitting in cpmvdest, copied there
  // above) and extract it to a DIFFERENT, empty destination. "archive.tar"
  // sorts BEFORE "cpmv_a.txt" alphabetically, so MoveDown() (forward-only)
  // can't reach it from the cursor's current position -- back to the top
  // first (see Command::Redraw's own non-destructive test for the same
  // MoveDown()-direction lesson).
  REQUIRE(app.handleCommand(Command::GoToTop));
  int arcSrcIdx = indexOf("archive.tar");
  REQUIRE(arcSrcIdx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != arcSrcIdx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  feedText((root / "arcdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpArchiveExtractToCompletion(app));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.directoryMode().currentDirectory() == (root / "arcdest").string() + "/");

  int newIdx = indexOf("arcsrc/");
  REQUIRE(newIdx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != newIdx) app.handleCommand(Command::MoveDown);
  int newNameCol = app.activeContentView().lines()[newIdx].highlightStartColumn.value_or(0);
  CHECK(display.underlineAt(newIdx, newNameCol));
}

// Variant of the test directly above: this time the ARC destination is
// the SAME directory the CP just landed in (not a different one) --
// closer to how the user's own "コピー先と同じ場所へ展開" workflow may
// have actually looked (typing the same path again rather than a fresh
// one).
TEST_CASE_FIXTURE(Fixture, "ARC extract following a CP, into the SAME directory (no ^L in between): the "
                           "newly-extracted entry is Underlined, the CP's own leftover marking is replaced") {
  fs::copy_file(root / "archive.tar", root / "cpmvdest" / "archive.tar");
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));
  REQUIRE(app.directoryMode().currentDirectory() == (root / "cpmvdest").string() + "/");

  REQUIRE(app.handleCommand(Command::GoToTop));
  int arcSrcIdx = indexOf("archive.tar");
  REQUIRE(arcSrcIdx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != arcSrcIdx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  feedText((root / "cpmvdest").string());  // SAME directory as the CP landed in
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpArchiveExtractToCompletion(app));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.directoryMode().currentDirectory() == (root / "cpmvdest").string() + "/");

  int newIdx = indexOf("arcsrc/");
  REQUIRE(newIdx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != newIdx) app.handleCommand(Command::MoveDown);
  int newNameCol = app.activeContentView().lines()[newIdx].highlightStartColumn.value_or(0);
  CHECK(display.underlineAt(newIdx, newNameCol));
}

// 2026-09-09 addition, a direct user report ("真因発見→ARC展開先を
// ~/.usilic/saved/utf8/（あるいは^Oa^M）とした場合、同ディレクトリが
// すでに存在しているとアンダーラインが付かない"): the original
// before/after directory-listing-diff design (2026-09-08) fundamentally
// couldn't handle re-extracting the SAME archive into a destination
// that already has its own previous output sitting there -- the target
// names aren't "new" by a plain diff, so nothing got marked. Fixed by
// computing the mark target directly from the archive's own member
// list (see PendingArchiveExtractShellRun::topLevelNames's own
// comment) instead of diffing the destination directory at all. This
// test extracts the SAME archive to the SAME destination TWICE in a
// row, with no intervening ^L, and checks the Underline appears both
// times.
TEST_CASE_FIXTURE(Fixture, "ARC extract: re-extracting the SAME archive into an ALREADY-populated destination "
                           "still shows the Underline (2026-09-09 fix, diff-based marking couldn't handle this)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  feedText((root / "arcdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpArchiveExtractToCompletion(app));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  // arcdest now already contains arcsrc/ from this first extraction.
  REQUIRE(fs::exists(root / "arcdest" / "arcsrc" / "inner.txt"));

  // Navigate straight back to the root directory (bypassing Back's own
  // "ARC: ..." annotation routing, which only applies when the
  // extraction was launched FROM Archive mode -- this test launches it
  // directly from Directory mode instead, same as the extraction just
  // above) and re-select archive.tar for a second, identical extraction.
  REQUIRE(app.directoryMode().enterDirectory(path()));
  int idx2 = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx2) app.handleCommand(Command::MoveDown);

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  feedText((root / "arcdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpArchiveExtractToCompletion(app));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.directoryMode().currentDirectory() == (root / "arcdest").string() + "/");

  int newIdx = indexOf("arcsrc/");
  REQUIRE(newIdx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != newIdx) app.handleCommand(Command::MoveDown);
  int newNameCol = app.activeContentView().lines()[newIdx].highlightStartColumn.value_or(0);
  CHECK(display.underlineAt(newIdx, newNameCol));  // still shown, even though arcsrc/ already existed here
}

// 2026-08-27 addition, a direct user proposal ("ディレクトリ（ls）の
// 戻り値に「ARC:」等モードへの帰還があるときは^J^Jルーチンへ移行
// じゃまずいの？"): a successful extraction lands in Directory mode
// showing "ARC: <archivePath>" as its own trailing3 -- ^G there used
// to just pop the ordinary ModeStack (landing on whichever plain
// directory Archive mode was entered FROM, not the archive itself,
// since a ModeStack frame can't represent "go back to Archive mode"
// at all). Now routes to goToRecentFile() (^J^J's own routine)
// instead whenever this annotation is showing, reaching the archive
// directly.
TEST_CASE_FIXTURE(Fixture, "handleCommand(Back): from Directory mode showing the \"ARC: ...\" annotation, "
                           "goes straight back to Archive mode instead of popping the ordinary ModeStack") {
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
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  feedText((root / "arcdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpArchiveExtractToCompletion(app));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.directoryMode().hasArcExtractSourceAnnotation());

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Archive);
  CHECK(app.archiveMode().currentArchive() == path() + "archive.tar");

  // 2026-08-27 follow-up, a real report + repro ("^G連打で行き来
  // ループとなる"): a SECOND Back (now from Archive mode) used to just
  // flip back to Directory mode with directoryMode_'s own state left
  // UNTOUCHED (still showing "arcdest", the extraction destination,
  // annotation and all) -- meaning THIS press landed right back on the
  // exact same "ARC: ..." screen the first Back left, so a THIRD Back
  // would just bounce to Archive again: an infinite ping-pong that
  // never reached archive.tar's own real containing directory
  // (path()). returnFromArchive() now follows the archive's real
  // parent directory the same way returnToDirectory() already does
  // for FileMode (see its own comment) -- this Back reaches path()
  // directly, with the annotation gone (a fresh enterDirectory() call
  // clears it, see its own comment), so a hypothetical third Back
  // would NOT bounce back to Archive again.
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == path());
  CHECK_FALSE(app.directoryMode().hasArcExtractSourceAnnotation());
}

// 2026-08-27 fix, a real report + repro ("^X^M[return]①→^G（=^J^J）
// →カーソル位置が「utf8/ＢＳ世界の〜」じゃない（←直前の選択が同ファ
// イルなので同じ位置でないと）"): extracting straight from Archive
// mode (^X a) never saved the archive's own cursor position before
// leaving for the destination shell run -- ArchiveMode::loadArchive()'s
// own position lookup then had nothing fresher than whatever (if
// anything) was saved from some earlier, unrelated visit, so a ^G back
// via goToRecentFile() (^J^J) landed on a stale/default row instead of
// the entry that was actually selected when extraction was invoked.
// Fixed in both openSelectedArchiveEntry() and executeArchiveExtract()
// -- see their own comments.
TEST_CASE_FIXTURE(Fixture, "ARC extract (^Oa from Archive mode): ^G back via the \"ARC: ...\" annotation "
                           "restores the cursor to the entry that was actually extracted, not row 0") {
  REQUIRE(app.start(path()));
  int idx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Archive);

  // Move OFF row 0 ("arcsrc/") onto "arcsrc/inner.txt" -- if the
  // position were never saved and defaulted back to row 0, this test
  // would fail to notice the bug at all.
  while (app.archiveMode().selectedEntry() != nullptr &&
         app.archiveMode().selectedEntry()->name.find("inner.txt") == std::string::npos) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.archiveMode().selectedEntry() != nullptr);
  std::string targetName = app.archiveMode().selectedEntry()->name;
  REQUIRE(app.handleCommand(Command::ToggleMark));

  fs::create_directory(root / "arcdest3");  // executeArchiveExtract() rejects a destination that doesn't exist yet
  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  feedText((root / "arcdest3").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpArchiveExtractToCompletion(app));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.directoryMode().hasArcExtractSourceAnnotation());

  CHECK(app.handleCommand(Command::Back));  // ^G -- routes via goToRecentFile() back to Archive
  CHECK(app.currentMode() == ApplicationMode::Archive);
  REQUIRE(app.archiveMode().selectedEntry() != nullptr);
  CHECK(app.archiveMode().selectedEntry()->name == targetName);
}

// 2026-08-27 fix, a real report + repro ("~/.usilic/saved/（ls）で
// 「ＢＳ世界の〜」を選択→^G連打で（ls）との行き来無限ループ。直しても
// らったはずなのに再発している") + the user's own proposed fix ("①の
// あと「ＢＳ世界の〜」を閲覧したり...した時点でARC履歴は抹消し、以後
// はそれぞれDIR/FILEの履歴に従う"): the "ARC: ..." annotation used to
// survive opening a FILE from the destination listing (only a fresh
// enterDirectory() call -- i.e. descending into a SUBDIRECTORY --
// cleared it), so the NEXT ^G back in that same directory still routed
// to goToRecentFile() (^J^J), which reopens that exact same file
// (RecentFile's own most-recent entry) -- which THEN ^G's back to the
// directory again, forever. Fixed via DirectoryMode::
// clearArcExtractSourceAnnotation(), called from
// Application::openSelectedEntry() the moment a file (not a
// subdirectory) is opened.
TEST_CASE_FIXTURE(Fixture, "ARC extract destination: opening a FILE there clears the \"ARC: ...\" annotation, "
                           "preventing a ^G ping-pong loop between it and the file just opened") {
  // A flat archive (a single top-level FILE, no subdirectory) so the
  // extraction destination's listing shows the file directly -- matching
  // the real report's own repro shape.
  fs::create_directory(root / "flatsrc");
  { std::ofstream(root / "flatsrc" / "flat.txt") << "flat content\n"; }
  REQUIRE(std::system(("cd '" + (root / "flatsrc").string() + "' && tar cf ../flat.tar flat.txt").c_str()) == 0);

  REQUIRE(app.start(path()));
  int idx = indexOf("flat.tar");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Archive);

  fs::create_directory(root / "flatdest");  // executeArchiveExtract() rejects a destination that doesn't exist yet
  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('a', std::nullopt));   // a -- nothing marked: extracts the WHOLE (single-file) archive
  feedText((root / "flatdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpArchiveExtractToCompletion(app));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.directoryMode().hasArcExtractSourceAnnotation());

  int fileIdx = indexOf("flat.txt");
  REQUIRE(fileIdx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != fileIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // open flat.txt -- must clear the annotation
  REQUIRE(app.currentMode() == ApplicationMode::File);
  CHECK_FALSE(app.directoryMode().hasArcExtractSourceAnnotation());

  REQUIRE(app.handleCommand(Command::Back));  // back to the destination directory (annotation already gone)
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == (root / "flatdest").string() + "/");

  // A SECOND Back must NOT bounce back into flat.txt again (the old
  // ping-pong bug: annotation still set -> goToRecentFile() -> reopens
  // flat.txt -> Back -> annotation still set -> ... forever).
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() != ApplicationMode::File);
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

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  feedText((root / "arcdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpArchiveExtractToCompletion(app));

  CHECK(fs::exists(root / "arcdest" / "arcsrc" / "inner.txt"));
  // 2026-08-26 change, a direct user spec ("①モードラインが...（ls）
  // へ移動"): a successful extraction now lands in Directory mode AT
  // the destination (matching CP/MV's own long-established
  // descendTo()-based landing, see pollPendingArchiveExtractShellRun()'s
  // own comment) instead of returning to the ARC TOC screen.
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == (root / "arcdest").string() + "/");
  // "ARC: " prefix only, not the full path -- 2026-08-27 fix (see
  // ContentView::withTruncatedTrailing3()'s own comment) now middle-
  // ellipsis-truncates an overlong trailing3 rather than showing it in
  // full or dropping it outright, so the full string isn't guaranteed
  // to appear verbatim once a real (fixture-length-dependent) tmp path
  // is involved.
  CHECK(app.activeContentView().modeLineText().find("ARC: ") != std::string::npos);
}

// 2026-09-08 note: the test that used to live here ("ARC extract: ^X^M
// (not just ^X a) also opens the destination prompt...") verified that
// the shared, independently-keyed ^X^M (K-71) destination command ALSO
// reached the archive prompt redundantly alongside ^X a/^O a. That
// redundant key itself (Command::StartDestinationPrompt) was deleted in
// the same keybinding redesign that moved ARC extraction to ^O a (a
// direct user request, "冗長分は全て削除で") -- there is no longer a
// second key that reaches this prompt, so the test's premise is gone.

// 2026-09-08 fix, a real user report ("選択マーク皆無のARC画面での
// ^Oa^Mではsaved/への全展開が起きない"): executeArchiveExtract()'s
// Archive-mode branch used to populate `members` straight from
// archiveMode().targetEntries() -- which, like CP/MV's own established
// convention, falls back to whatever single entry the cursor happens
// to be on when nothing is explicitly marked. That meant "no marks"
// in Archive mode could never reach the Directory-mode branch's own
// "leave members empty -> whole archive" convention (see its comment
// just below) -- extraction always targeted just the one cursor entry
// instead of everything. Now gated on markedCount() instead, so "no
// marks at all" means "the whole archive", matching Directory mode.
// This test opens the TOC (Archive mode), marks NOTHING, and confirms
// extraction reaches every file in the archive rather than just
// whatever the cursor landed on after opening it.
TEST_CASE_FIXTURE(Fixture, "ARC extract: from within Archive mode's own TOC with NO marks at all, still "
                           "extracts the WHOLE archive instead of just the cursor's single entry") {
  REQUIRE(app.start(path()));
  int idx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Archive);
  REQUIRE(app.archiveMode().markedCount() == 0);  // nothing explicitly marked

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  REQUIRE(app.promptActive());
  feedText((root / "arcdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpArchiveExtractToCompletion(app));

  CHECK(fs::exists(root / "arcdest" / "arcsrc" / "inner.txt"));
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

// 2026-08-27 addition, a direct user report (a callback to their own
// earlier HISTORY.md:9608 proposal): a real CJK filename long enough
// to crowd FileMode's own mode line -- name (the file's own basename)
// + trailing2 (its containing directory) together leaving no room for
// the byte-count trailing field -- used to just silently drop it
// entirely (fitModeLineFields()'s own ordinary all-or-nothing tiers),
// unlike ArchiveMode's content view, which already gets the "center
// position+bytes, split name/trailing2" splitLayoutWhenCrowded
// treatment for exactly this case (see file_mode.cpp's own provider
// comment).
TEST_CASE_FIXTURE(Fixture, "FILE: a real CJK filename long enough to crowd the mode line still shows the "
                           "byte count, via the same splitLayoutWhenCrowded treatment ArchiveMode's content "
                           "view already gets") {
  fs::path longDir = root / "longnamedir";
  fs::create_directory(longDir);
  std::string longName =
      "\xE3\x81\x93\xE3\x82\x8C\xE3\x81\xAF\xE9\x9D\x9E\xE5\xB8\xB8\xE3\x81\xAB\xE9\x95\xB7\xE3"
      "\x81\x84\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB\xE5\x90\x8D\xE3\x81\xA7\xE3\x81\x99"
      "\xE3\x81\x93\xE3\x82\x8C\xE3\x81\xAF\xE9\x9D\x9E\xE5\xB8\xB8\xE3\x81\xAB\xE9\x95\xB7\xE3\x81"
      "\x84\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB\xE5\x90\x8D\xE3\x81\xA7\xE3\x81\x99.txt";
  { std::ofstream(longDir / longName) << "content\n"; }

  REQUIRE(app.start(path()));
  int idx = indexOf("longnamedir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.directoryMode().selectedEntry() != nullptr);
  while (app.directoryMode().selectedEntry()->name.find(".txt") == std::string::npos)
    REQUIRE(app.handleCommand(Command::MoveDown));
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  std::string row = display.rowText(23);
  std::string byteCount = std::to_string(app.activeContentView().totalByteCount());
  CHECK(row.find(byteCount) != std::string::npos);
}

// 2026-08-26 addition, a direct (provisional) user spec covering the
// OTHER two destination shapes ^X^M/^X a can receive besides a real,
// existing directory (see executeArchiveExtract()'s own comment) --
// both were previously silent no-ops ("何も起きない").

TEST_CASE_FIXTURE(Fixture, "ARC extract: an empty destination defaults to <homeDirectory>/.usilic/saved/, "
                           "auto-created if missing") {
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
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE_FALSE(fs::exists(root / ".usilic" / "saved"));  // doesn't exist yet -- must be auto-created
  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  REQUIRE(app.promptActive());
  CHECK(app.feedByte(0x0d, std::nullopt));  // ^M alone -- no destination typed at all
  REQUIRE(pumpArchiveExtractToCompletion(app));

  CHECK(fs::exists(root / ".usilic" / "saved" / "arcsrc" / "inner.txt"));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  // "ARC: " prefix only -- see the ^O a test's own identical 2026-08-27
  // comment above.
  CHECK(app.activeContentView().modeLineText().find("ARC: ") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "ARC extract: a destination that doesn't exist reports \"<path>: No such "
                           "directory\" in the mode line instead of silently doing nothing") {
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
  REQUIRE(app.handleCommand(Command::ToggleMark));

  // A short, fixed, definitely-nonexistent absolute path -- not one of
  // this fixture's own tmp paths -- deliberately kept SHORT so the
  // resulting message still fits renderModeLine()'s ordinary sequential
  // layout (see "Invalid mode"/"Permission denied"'s own tests, same
  // convention); a REAL full tmp-root path here would be long enough to
  // trip splitLayoutWhenCrowded()'s "too crowded" fallback layout
  // instead, which elides/repositions the message unpredictably --
  // besides being irrelevant to what this test is actually checking.
  std::string typo = "/usilic-test-no-such-dir";
  REQUIRE_FALSE(fs::exists(typo));
  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('a', std::nullopt));   // a
  REQUIRE(app.promptActive());
  feedText(typo);
  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));  // rejected -- no shell command launched

  CHECK(app.currentMode() == ApplicationMode::Archive);  // stays put, doesn't even reach Shell
  std::string expected = typo + ": No such directory";
  CHECK(display.rowText(23).substr(0, expected.size()) == expected);
  CHECK(display.underlineAt(23, 0));
  CHECK_FALSE(fs::exists(typo));  // definitely not auto-created (unlike the empty-destination default above)
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

// -- ERROR/LOG buffers (K-67/K-68, ^J^Oe/^J^Ol -- moved from ^X^Ke/^X^Kl 2026-09-08) --

TEST_CASE_FIXTURE(Fixture, "ERROR (^J^Oe, K-67): refused when nothing has been logged this session yet") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.handleCommand(Command::ShowErrorLog));
}

// 2026-08-22 correction: CP/MV/RM's K-52 ("without SHELL window") used
// to run via CpMvRmMode::copyTo() -- a captured, non-interactive
// ChildProcess::run() whose stderr was redirected to errorLogPath_
// (this test's own original subject). A real user report ("処理過程
// はSHELLに残してもらわないと困る...もしも-iオプション付きだったら
// その場でフリーズするんじゃない？") pointed out that this captured
// path has no real terminal attached at all, so `-i` (src/config.sh's
// own default `cp -vRi`/`rm -vri`) would block forever on a
// confirmation nothing could ever answer -- a genuine freeze risk, not
// hypothetical. K-52 now always runs via copyToInteractive() (the same
// real-terminal execution K-51 already used), matching legacy's own
// M_shell-routed command() for both variants (see Application::
// executeCopy()'s own comment) -- so a failed CP's own diagnostic goes
// straight to the real terminal (and stays there for the user to
// scroll back to) instead of being captured into errorLogPath_ at all.
// ^J^Oe now correctly reports nothing logged for a CP/MV/RM failure --
// this is intentional, not a regression left unnoticed.
TEST_CASE_FIXTURE(Fixture, "ERROR (^J^Oe, K-67): a failed CP does NOT land in errorLogPath_ anymore -- "
                           "it runs via shell_'s own real SHELL session, same as K-51") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('c', std::nullopt));   // c -- ^Oc: shows the destination prompt immediately (K-52)
  // A destination whose PARENT doesn't exist either -- cp fails and
  // writes a diagnostic to stderr (unlike a merely-missing leaf name,
  // which cp happily creates).
  feedText((root / "no_such_parent" / "dest").string());
  // Submitting now only INJECTS the command into shell_ and switches to
  // Shell mode -- it doesn't run cp synchronously, so this returns true
  // (successfully injected) regardless of whether cp itself later
  // succeeds or fails; pumpCpMvRmToCompletion() below waits for the
  // actual result.
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

  CHECK_FALSE(app.handleCommand(Command::ShowErrorLog));  // nothing was ever captured to errorLogPath_
}

TEST_CASE_FIXTURE(Fixture, "LOG (^J^Ol, K-68): refused when Shell was never started") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.handleCommand(Command::ShowShellLog));
}

// ESC l (2026-08-18 addition, see executeStartLog()'s own comment):
// refused when Shell was never started, same as K-68 above -- nothing
// to snapshot into logFilePath_ yet.
TEST_CASE_FIXTURE(Fixture, "LOG (ESC l): refused when Shell was never started") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.handleCommand(Command::StartLog));
}

// The real end-to-end flow: run a real command in Shell (so a real
// "usilic-1.5b:" prompt line gets frozen into scrollback, see item
// 335's own bold-prompt change), leave Shell via ^G (returnFromShell()
// is pure navigation -- scrollback survives, unlike re-entering
// StartShell after `exit`, which starts a genuinely fresh empty
// session), then ESC l from Directory.
TEST_CASE_FIXTURE(Fixture, "LOG (ESC l): splits the current SHELL log on \"usilic-1.5b:\", labeled LOG "
                           "(not CUT) in the mode line -- the actual feature the reverted 2026-08-05 "
                           "Command::StartShellLog attempt (CLAUDE.md history item 176) was aiming for") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("printf 'LOGTEST123\\n'")) app.feedShellByte(c, std::nullopt);
  app.feedShellByte(0x0d, std::nullopt);
  REQUIRE(pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("LOGTEST123") != std::string::npos) return true;
    return false;
  }));

  REQUIRE(app.feedByte(0x07, std::nullopt));  // ^G -- returnFromShell(), scrollback survives
  REQUIRE(app.currentMode() == ApplicationMode::Directory);

  REQUIRE(app.feedByte(0x1b, std::nullopt));  // ESC
  REQUIRE(app.feedByte('l', std::nullopt));   // l
  CHECK(app.currentMode() == ApplicationMode::Cut);
  CHECK(app.activeContentView().modeLineText().substr(0, 3) == "LOG");  // NOT "CUT" -- see cutView_'s own provider

  // At least one segment's own label line contains the bold prompt
  // marker the split anchored on -- and selecting it (entering
  // CutContent) shows the run's own output still intact underneath.
  bool sawPromptLabel = false;
  int matchingSegment = -1;
  const auto &segments = app.cutMode().segments();
  for (std::size_t i = 0; i < segments.size(); i++) {
    if (segments[i].label.find("usilic-1.5b:") != std::string::npos) {
      sawPromptLabel = true;
      matchingSegment = static_cast<int>(i);
    }
  }
  CHECK(sawPromptLabel);
  REQUIRE(matchingSegment >= 0);

  app.activeContentView().setCursorIndex(matchingSegment);
  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::CutContent);
  bool sawOutput = false;
  for (const auto &line : app.activeContentView().lines()) {
    if (line.text.find("LOGTEST123") != std::string::npos) sawOutput = true;
  }
  CHECK(sawOutput);
}

// 2026-08-18 redesign (screenshot =cm): the OLD layout (bare "usilic
// v1.5b" heading, "Startup file"/"History file"/"User name"/"ERROR
// log"/"SHELL log") always pointed at one shared <homeDirectory>/
// .usilic location regardless of how many instances were running --
// this test now checks the NEW field set (Boot options/Startup file/
// ERROR/SHELL/WORK) and confirms the two removed fields (the "usilic
// v1.5b" heading text and the history file path) are genuinely gone,
// not just renamed.
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): shows a status screen with the new Boot options/Startup "
                           "file/ERROR/SHELL/WORK field layout") {
  REQUIRE(app.start(path()));
  CHECK(app.feedByte(0x09, std::nullopt));  // ^I
  CHECK(app.currentMode() == ApplicationMode::File);

  std::string joined;
  for (const auto &line : app.activeContentView().lines()) joined += line.text + "\n";
  CHECK(joined.find("usilic v1.5b") == std::string::npos);  // removed field
  CHECK(joined.find((root / ".usilic" / "history").string()) == std::string::npos);  // removed field
  CHECK(joined.find("Boot options") != std::string::npos);
  CHECK(joined.find("Startup file") != std::string::npos);
  CHECK(joined.find((root / ".usilic" / "usilicrc").string()) != std::string::npos);
  CHECK(joined.find("ERROR") != std::string::npos);
  CHECK(joined.find((root / ".usilic" / "error").string()) != std::string::npos);
  CHECK(joined.find("SHELL") != std::string::npos);
  CHECK(joined.find((root / ".usilic" / "shell").string()) != std::string::npos);
  CHECK(joined.find("WORK") != std::string::npos);

  // The Fixture's own app{} construction leaves tempRootDirectory/
  // bootOptions at their default ("") -- see the constructor's own
  // comment -- so this also exercises the no-argument-launch fallback
  // (temp root == <homeDirectory>/.usilic) and the empty-Boot-options
  // case in the same pass.

  // "INFO" (2026-08-27 rename, a direct user report: the previous
  // "usilic" name -- FileMode's own default, the loaded file's bare
  // basename -- read as though a real ordinary file literally named
  // "~/.usilic/usilic" were being viewed; setSearchOrigin("INFO", ...)
  // overrides it, see executeShowUsilicBanner()'s own comment).
  CHECK(app.activeContentView().modeLineText().substr(0, 4) == "INFO");
}

// A real work file (K-17) and a real nested-archive descent (K-57/K-58
// style, via descendToNestedArchive()) both allocate into the SAME
// tempRootDirectory_ -- this confirms ^T's WORK/arc.N lists actually
// grow to reflect them, not just that the labels exist.
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): WORK and arc.N lists grow as w.N/arc.N are actually used") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte(0x17, std::nullopt));  // ^W -- allocates w.1

  CHECK(app.feedByte(0x09, std::nullopt));  // ^I
  std::string joined;
  for (const auto &line : app.activeContentView().lines()) joined += line.text + "\n";
  CHECK(joined.find((root / ".usilic" / "w.1").string()) != std::string::npos);
}

// 2026-08-18: the original mockup's own "[↑ ハイライト文字列選択後は]
// [戻り値「usilic」画面へ]" annotation on the WORK/arc.N lines --
// flagged as deferred in item 338, implemented now via
// Application::openSelectedBannerEntry().
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): Enter on the WORK line opens w.1 as a FILE; ^G returns to "
                           "the banner itself (not the original directory)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte(0x17, std::nullopt));  // ^W -- allocates w.1
  REQUIRE(app.currentMode() == ApplicationMode::Directory);

  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string bannerFile = app.fileMode().currentFile();

  for (int i = 0; i < 4; i++) REQUIRE(app.handleCommand(Command::MoveDown));  // Boot/Startup/ERROR/SHELL/WORK
  int workRow = app.activeContentView().cursorIndex();
  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / ".usilic" / "w.1").string());
  // 2026-08-18 fix, a direct user report: "戻り値が不良" -- viewing
  // w.1 (or ERROR/SHELL/Startup file) FROM the banner used to show its
  // own raw containing directory (FileMode's own default trailing2) as
  // the "戻り値" field, which is misleading here: ^G doesn't lead to a
  // directory listing of that path at all, it goes straight back to
  // this banner -- setSearchOrigin("INFO", ...) (2026-08-27: renamed
  // from "usilic") now reflects that.
  CHECK(app.activeContentView().modeLineText().find("INFO") != std::string::npos);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == bannerFile);  // back on the banner, not returnToDirectory()
  // 2026-08-18 fix, a direct user report: "^Gで同じ行に戻らない" --
  // openSelectedBannerEntry() never called fileMode_.saveCursorPosition()
  // before overwriting currentFile_/the view's lines, so loadFile()'s
  // own History::positionFor() lookup (keyed on this fixed bannerFilePath())
  // had nothing to restore and always landed back on row 0.
  CHECK(app.activeContentView().cursorIndex() == workRow);
}

TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): Enter on an arc.N line opens it as a DIRECTORY; ^G falls "
                           "back to goToRecentDirectory() since no stack frame was pushed to open it") {
  REQUIRE(app.start(path()));
  // Allocate arc.1 the same way the sibling "ARC nested" tests do:
  // descend one level into the Fixture's own outer.tar/level1.tar chain.
  int idx = indexOf("outer.tar");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // into outer.tar
  while (app.archiveMode().selectedEntry() != nullptr &&
         app.archiveMode().selectedEntry()->name.find("level1.tar") == std::string::npos) {
    REQUIRE(app.handleCommand(Command::MoveDown));
  }
  REQUIRE(app.handleCommand(Command::Enter));  // descends into level1.tar -- allocates arc.1

  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  const auto &lines = app.activeContentView().lines();
  int arcRow = -1;
  for (std::size_t i = 0; i < lines.size(); i++) {
    if (lines[i].text.find("arc.1") != std::string::npos) arcRow = static_cast<int>(i);
  }
  REQUIRE(arcRow >= 0);
  app.activeContentView().setCursorIndex(arcRow);
  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory().find("arc.1") != std::string::npos);

  // 2026-08-27 change, a direct user proposal: Back with an empty
  // ModeStack no longer dead-ends -- it now falls back to
  // goToRecentDirectory() (RingCategory::RecentDirectory's own most
  // recently visited DIFFERENT directory), so this succeeds and lands
  // somewhere real instead of staying put as a no-op.
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);  // no crash/garbling
  CHECK(app.directoryMode().currentDirectory().find("arc.1") == std::string::npos);  // actually moved
}

// 2026-08-18 fix (a real report, =cq): the WORK/arc.N lines were
// impossible to actually pick out on screen -- FileMode's own
// constructor disables cursorHighlightEnabled_ by default (correct
// for plain pager reading), which openSelectedBannerEntry() relies on
// being visible to be usable at all. Also confirms the flag doesn't
// leak into whatever gets loaded next (a plain file here; the sibling
// WORK-selection test above already covers "a w.N reopened FROM the
// banner" specifically).
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): the WORK/arc.N candidate list is actually highlighted (was "
                           "invisible before this fix), and loading an ordinary file afterward turns it back off") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));
  CHECK(app.activeContentView().cursorHighlightEnabled());
  REQUIRE(app.handleCommand(Command::Back));

  int idx = indexOf("plain.txt");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  CHECK_FALSE(app.activeContentView().cursorHighlightEnabled());
}

// 2026-08-18 fix (a real report: "usilicは/tmp/.usilic.PID/には作ら
// なくていい。戻り値も...^Tを起動したモードのはず"): the banner's own
// backing file used to live under tempRootDirectory_ (which for an
// argument-launched instance is a PID-specific /tmp/.usilic.<pid>/
// directory), leaking that path into the banner's own File-mode line
// as a "byte size + containing directory" field that -- unlike a REAL
// file's real parent directory -- looked like it was announcing where
// ^G would land, when actually returnToDirectory()'s own exclusion
// (see its comment) already left directoryMode_ untouched regardless.
// bannerFilePath() now always resolves under usilicrcPath_'s own
// directory (<homeDirectory>/.usilic), matching how usilicrcPath_/
// historyFilePath_ were themselves already exempted from the
// tempRootDirectory_ reorg in item 338 -- the banner was never
// actually part of the multi-instance conflict that reorg was fixing
// (unlike ERROR/SHELL, it's never written to in the background).
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): the banner's own backing file lives under "
                           "<homeDirectory>/.usilic, not tempRootDirectory_") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));
  CHECK(app.fileMode().currentFile() == (root / ".usilic" / "usilic").string());
}

// 2026-08-18 fix, a direct user report + screenshot (=??): "一覧表示の
// ハイライトを「: 」の後からにしてほしい" -- every line used to get a
// whole-row reverse-video highlight (label included), unlike
// DirectoryMode's own `ls -l` rows (nameStartColumn). Every line's
// Line::highlightStartColumn should now land exactly where the value
// text starts, matching DirectoryMode's own established convention.
// 2026-08-28 follow-up: highlightStartColumn is now the SAME "front of
// the value" column (15) on EVERY line, openable or not -- Line::
// neverHighlighted (not this column) is what actually suppresses the
// SGR highlight for "Boot options"/an empty "WORK :" now, so the real
// terminal caret (syncVisibleCursor() uses this same column) lands at
// a consistent position regardless of a non-openable row's own
// (possibly much longer, e.g. bootOptions_) text -- a direct user
// report: "Boot optionsは...ブロックカーソル位置は「WORK : 」と同じ
// でよろしく" caught the OLDER "point past this row's own text end"
// technique moving the caret inconsistently far right specifically for
// "Boot options" (which, unlike WORK, usually has real text after its
// own label).
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): highlightStartColumn lands at the SAME \"front of the "
                           "value\" column on every line; neverHighlighted (not the column) is what actually "
                           "suppresses \"Boot options\"/an empty \"WORK :\"'s own SGR highlight") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));
  const auto &lines = app.activeContentView().lines();
  // Boot options / Startup file / ERROR / SHELL / WORK (fresh fixture: no
  // w.N ever allocated, so WORK's own line has nothing after it) -- see
  // executeShowUsilicBanner()'s own `openable` comment for why Boot options
  // and an empty WORK are the only two non-openable lines here.
  REQUIRE(lines.size() >= 5);
  for (std::size_t i = 0; i < 5; i++) {
    REQUIRE(lines[i].highlightStartColumn.has_value());
    CHECK(*lines[i].highlightStartColumn == 15);  // kLabelWidth(12) + " : "(3) -- every line, uniformly
  }
  CHECK(lines[0].neverHighlighted);         // "Boot options" -- never openable
  CHECK(lines[4].neverHighlighted);         // "WORK :" with nothing after it
  for (std::size_t i : {1, 2, 3}) CHECK_FALSE(lines[i].neverHighlighted);  // Startup file / ERROR / SHELL
}

// 2026-08-28 addition, same report: confirms the ACTUAL RENDERED
// result, not just the Line data -- "Boot options" (the cursor's own
// initial row, right when INFO first opens) shows no Standout ANYWHERE
// on the row (neverHighlighted suppresses it even though it IS the
// cursor row), while the real terminal caret still lands at column 15
// (the SAME column WORK/ERROR/etc. use), not off at the end of
// whatever bootOptions_ happens to contain.
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): \"Boot options\" (the cursor's own initial row) shows no "
                           "Standout at all when rendered, but the real caret still lands at column 15") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));
  REQUIRE(app.activeContentView().cursorIndex() == 0);  // "Boot options" -- the cursor's own initial row
  for (int col = 0; col < 80; col++) CHECK_FALSE(display.standoutAt(0, col));
  CHECK(display.cursorRow() == 0);
  CHECK(display.cursorCol() == 15);  // matches WORK/ERROR/etc.'s own front-of-value column, not bootOptions_'s end
}

// 2026-08-18 fix, a direct user report: "ハイライト上の[return]が反応
// しない" -- only WORK/arc.N were ever wired up to Enter; a user
// landing on "ERROR"/"SHELL"/"Startup file" (highlighted identically,
// per the fix just above) reasonably expected Enter to open THOSE
// too.
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): Enter on the ERROR/SHELL/Startup file lines opens each as a "
                           "FILE too (previously only WORK/arc.N responded to Enter at all)") {
  { std::ofstream(root / ".usilic" / "error") << "boom\n"; }
  { std::ofstream(root / ".usilic" / "shell") << "$ echo hi\n"; }
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));

  const auto &lines = app.activeContentView().lines();
  auto rowEndingWith = [&](const std::string &suffix) {
    for (std::size_t i = 0; i < lines.size(); i++) {
      if (lines[i].text.size() >= suffix.size() &&
          lines[i].text.compare(lines[i].text.size() - suffix.size(), suffix.size(), suffix) == 0)
        return static_cast<int>(i);
    }
    return -1;
  };

  int errorRow = rowEndingWith((root / ".usilic" / "error").string());
  REQUIRE(errorRow >= 0);
  app.activeContentView().setCursorIndex(errorRow);
  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / ".usilic" / "error").string());
  REQUIRE(app.handleCommand(Command::Back));  // back to the banner itself

  int shellRow = rowEndingWith((root / ".usilic" / "shell").string());
  REQUIRE(shellRow >= 0);
  app.activeContentView().setCursorIndex(shellRow);
  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / ".usilic" / "shell").string());
}

// 2026-08-27 fix, a real report ("^T→「/tmp/.usilic.NNNNN/shell」
// [return]→反応なし（ファイルコピーがSHELLで行われたのに）"): unlike
// the test just above (which pre-creates the shell log file directly
// via ofstream, sidestepping the actual bug), a REAL shell session's
// own output was never written to logFilePath_ on disk at all unless
// refreshShellLogFile() had separately been called first (^J^Ol/ESC l)
// -- Enter on the SHELL line used to just fail silently in that case.
// openSelectedBannerEntry() now calls refreshShellLogFile() itself
// first, so it always reflects the session's current scrollback.
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): Enter on the SHELL line opens the real shell log even if "
                           "it was never explicitly dumped first (^J^Ol/ESC l)") {
  REQUIRE(app.start(path()));
  bool shellOk = app.feedByte(0x0f, std::nullopt);       // ^O
  shellOk = shellOk && app.feedByte('s', std::nullopt);  // s -- RestartShell (K-60)
  if (!shellOk || app.currentMode() != ApplicationMode::Shell) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  app.shell().commandLine().setText("echo hi-from-shell-log-test");
  app.shell().submitLine();
  REQUIRE(pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("hi-from-shell-log-test") != std::string::npos) return true;
    return false;
  }));
  REQUIRE_FALSE(fs::exists(root / ".usilic" / "shell"));  // never explicitly dumped -- doesn't exist on disk yet

  REQUIRE(app.handleCommand(Command::Back));  // back to Directory, same as the sibling test's own setup shape
  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));

  const auto &lines = app.activeContentView().lines();
  int shellRow = -1;
  for (std::size_t i = 0; i < lines.size(); i++) {
    const std::string &suffix = "shell";
    if (lines[i].text.size() >= suffix.size() &&
        lines[i].text.compare(lines[i].text.size() - suffix.size(), suffix.size(), suffix) == 0) {
      shellRow = static_cast<int>(i);
      break;
    }
  }
  REQUIRE(shellRow >= 0);
  app.activeContentView().setCursorIndex(shellRow);
  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / ".usilic" / "shell").string());
  CHECK(fs::exists(root / ".usilic" / "shell"));  // refreshShellLogFile() created it on demand

  bool foundContent = false;
  for (const auto &line : app.activeContentView().lines())
    if (line.text.find("hi-from-shell-log-test") != std::string::npos) foundContent = true;
  CHECK(foundContent);
}

// 2026-08-28 addition, a direct user proposal ("最初にそれら（/tmp/
// .usilic.NNNNN/shell等）をtouchしておき、実際にアクセスしてアン
// ダーライン「No String」を表示する仕様に変更してほしい"): ERROR/
// SHELL before either has any REAL content yet (nothing logged/no
// shell session started) are still shown highlighted/openable-looking
// (touched empty by executeShowUsilicBanner(), see its own comment) --
// selecting one now succeeds (unlike before this fix, which failed
// silently) and shows "No String" (underlined), not a blank screen.
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): Enter on ERROR/SHELL before either has real content shows "
                           "underlined \"No String\", not a blank screen or a silent no-op") {
  REQUIRE(app.start(path()));
  REQUIRE_FALSE(fs::exists(root / ".usilic" / "error"));  // nothing logged yet
  REQUIRE_FALSE(fs::exists(root / ".usilic" / "shell"));  // no shell session started yet
  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));
  CHECK(fs::exists(root / ".usilic" / "error"));  // touched immediately, see executeShowUsilicBanner()'s own comment
  CHECK(fs::exists(root / ".usilic" / "shell"));

  const auto &lines = app.activeContentView().lines();
  auto rowEndingWith = [&](const std::string &suffix) {
    for (std::size_t i = 0; i < lines.size(); i++) {
      if (lines[i].text.size() >= suffix.size() &&
          lines[i].text.compare(lines[i].text.size() - suffix.size(), suffix.size(), suffix) == 0)
        return static_cast<int>(i);
    }
    return -1;
  };

  int errorRow = rowEndingWith((root / ".usilic" / "error").string());
  REQUIRE(errorRow >= 0);
  app.activeContentView().setCursorIndex(errorRow);
  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(fs::exists(root / ".usilic" / "error"));  // touched by executeShowUsilicBanner(), now real (if empty)
  REQUIRE(app.activeContentView().lines().size() == 1);
  CHECK(app.activeContentView().lines()[0].text == "No String");
  REQUIRE_FALSE(app.activeContentView().lines()[0].attributes.empty());
  CHECK(app.activeContentView().lines()[0].attributes[0].attribute == fm::Display::Attribute::Underline);
  REQUIRE(app.handleCommand(Command::Back));  // back to the banner itself

  int shellRow = rowEndingWith((root / ".usilic" / "shell").string());
  REQUIRE(shellRow >= 0);
  app.activeContentView().setCursorIndex(shellRow);
  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.activeContentView().lines().size() == 1);
  CHECK(app.activeContentView().lines()[0].text == "No String");
}

// 2026-08-18 fix, a direct user report: launched via "-c ~/git/usilic"
// then ^T showed "/Users/haa15030/.usilic" (the banner's OWN synthetic
// file's containing directory) as the trailing "戻り値" field instead
// of the real directory ^G actually returns to.
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): the trailing \"戻り値\" field shows the REAL directory ^G "
                           "returns to, not the banner file's own containing directory") {
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // descend into subdir/
  REQUIRE(app.directoryMode().currentDirectory() == (root / "subdir").string() + "/");

  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));
  std::string modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.find((root / "subdir").string()) != std::string::npos);
  CHECK(modeLine.find(".usilic") == std::string::npos);
}

// 2026-08-28 fix, a real report + repro ("usilic -c ~/git/usilic→^I→
// 名前欄が「INFO」ではなく「usilic」、戻り値が「~/.usilic/」のまま"):
// loadFile() (inside executeShowUsilicBanner(), before setSearchOrigin()
// ever runs) already redraws ONCE internally using FileMode's own
// DEFAULT mode line -- searchOriginLabel_/searchOriginDetail_ are still
// empty at that exact point (loadFile() itself clears them), so that
// stale first frame falls back to the loaded temp file's own raw
// basename ("usilic") and containing directory ("~/.usilic/"). Nothing
// redrew again afterward to overwrite it with the corrected values now
// that setSearchOrigin() has actually run -- openSelectedBannerEntry()
// was already fixed for the identical gap on 2026-08-18 (see its own
// comment); executeShowUsilicBanner() itself never got the same fix.
// Deliberately checks the REAL RENDERED row (display.rowText(), what a
// real terminal actually shows), NOT modeLineText() (a pull-based
// computation that's always fresh regardless of whether anything ever
// redrew) -- the sibling test just above, using modeLineText(), could
// never have caught this class of bug at all.
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): the REAL RENDERED mode line shows \"INFO\"/the actual "
                           "start directory immediately, not a stale first frame") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));
  std::string row = display.rowText(23);
  CHECK(row.substr(0, 4) == "INFO");
  CHECK(row.find(path()) != std::string::npos);
  CHECK(row.find(".usilic") == std::string::npos);
}

// 2026-08-28 follow-up, a direct user report ("ケース１が直ってない
// （２はOK）"): the sibling test just above (cursor left at its
// DirectoryMode default, row 0) passed once fileView_.redraw() was
// added after setSearchOrigin() -- but selecting a REAL entry first
// (moving the cursor off row 0) before pressing ^I still showed the
// stale first frame in the user's own real testing. Reproduces that
// exact shape to find out whether it's a genuinely different bug or
// the same one from an angle the sibling test didn't cover.
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): the REAL RENDERED mode line is still correct after "
                           "selecting a real entry FIRST, not just from Directory mode's own default row 0") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.directoryMode().selectedEntry()->name == "plain.txt");

  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));
  std::string row = display.rowText(23);
  CHECK(row.substr(0, 4) == "INFO");
  CHECK(row.find(path()) != std::string::npos);
  CHECK(row.find(".usilic") == std::string::npos);
}

// 2026-08-28 fix, a real report + repro ("「~/.usilic/（ls）」で
// usilicrcを選択→^I→^G→usilicrcへ戻らない"): executeShowUsilicBanner()
// never saved DirectoryMode's own cursor position first, unlike every
// other Directory-to-File transition (see its own comment).
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): ^G restores the cursor to whichever Directory row it was "
                           "on before ^I, not a stale/default position") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().selectedEntry()->name == "plain.txt");
}

// 2026-08-28 fix, a real report ("CLAUDE.mdで^Iしてる以上CLAUDE.mdに
// 戻らないと"): unlike the test just above (cursor merely highlighting
// plain.txt in the Directory listing, never opened), this covers ^I
// pressed while a real file is ACTUALLY OPEN in File mode -- ^G from the
// banner must reload and return to THAT file, not just the directory
// with it highlighted. Before this fix, fileReturnMode_ was hardcoded
// to Directory regardless of what was active before ^I, so ^G always
// fell through to returnToDirectory() (Directory mode, cursor restored
// to plain.txt's row -- indistinguishable from the test above, but NOT
// what the user actually wanted: back INSIDE plain.txt's own content).
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): ^G returns to the REAL FILE that was actually open (not just "
                           "Directory with it highlighted) when ^I was pressed from File mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string openedFile = app.fileMode().currentFile();
  REQUIRE(app.activeContentView().cursorIndex() >= 0);
  app.handleCommand(Command::MoveDown);  // move the cursor within the file so restoration is actually checkable
  int cursorBeforeInfo = app.activeContentView().cursorIndex();

  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() != openedFile);  // now viewing the banner itself, not plain.txt

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == openedFile);
  CHECK(app.activeContentView().cursorIndex() == cursorBeforeInfo);
}

// 2026-08-28 fix, a real report + repro ("INFO画面で^J.→SHELLの戻り値
// が「~/.usilic/usilic」...→^G→INFO（←戻らなくていい）") + the user's
// own general policy ("基本的にINFOへは戻らなくていい。例外は
// usilicrc、ERROR、SHELL、WORKの対象ファイルを選んだ場合またはINFOで
// FGREP/EGREP/CUTを実行した場合のみ"): a real SHELL excursion (^J.,
// ^J^W, ^[s, ^Xs, ...) isn't one of those exceptions -- ensureShellActive()
// now skips INFO entirely and falls back to Directory (what INFO itself
// would have returned to), matching pagerReturnsToShell()'s own
// "don't record an ephemeral screen as a return target" precedent.
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): a real SHELL excursion from here (^J.) does NOT return to "
                           "INFO on ^G -- lands in Directory instead") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  REQUIRE(app.feedByte('.', std::nullopt));   // . -- ^J. : SHELL, change current directory
  REQUIRE(app.currentMode() == ApplicationMode::Shell);

  REQUIRE(app.handleCommand(Command::Back));  // ^G
  CHECK(app.currentMode() == ApplicationMode::Directory);  // NOT back to INFO/File
}

// 2026-08-18: ^T used to get a dedicated feedShellByte() special-case
// so it could reach the banner even from SHELL mode ("SHELLで^Tが利か
// ない"). 2026-08-27: rebound to ^I -- a direct user request, since the
// mode's own "usilic" name (FileMode's default, the loaded file's bare
// basename) read as though a real ordinary file literally named
// "~/.usilic/usilic" were being viewed; renamed to "INFO" and moved off
// ^T. That old special-case is deliberately NOT carried over to ^I:
// Tab already has real, expected meaning inside an active SHELL session
// (shell_.onTab(), real bash completion) that must not be hijacked, so
// INFO is simply unreachable from Shell mode now -- this locks in that
// Tab still does completion there, not a mode switch.
TEST_CASE_FIXTURE(Fixture, "INFO banner (^I, K-0): NOT reachable from SHELL mode -- ^I/Tab there still means "
                           "real shell completion, not a mode switch") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartShell));
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));
  REQUIRE(app.currentMode() == ApplicationMode::Shell);

  CHECK(app.feedShellByte(0x09, std::nullopt));  // bare ^I/Tab
  CHECK(app.currentMode() == ApplicationMode::Shell);  // stayed in Shell -- did NOT open the banner
}

// 2026-09-08、直接のユーザー依頼によるキー再編で^Oaから^Jaへ移行
// (ARCが^Xaから^Oaへ移るための地ならし)。
TEST_CASE_FIXTURE(Fixture, "feedByte: ^J a (K-27) toggles the HELP/KEY display language, and back again") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  const auto &linesJa = app.activeContentView().lines();
  bool sawJapaneseFirstRow = linesJa[0].text.find("j : ") != std::string::npos &&
                              linesJa[0].text.find("Move window down") == std::string::npos;
  CHECK(sawJapaneseFirstRow);

  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  CHECK(app.feedByte('a', std::nullopt));
  const auto &linesEn = app.activeContentView().lines();
  CHECK(linesEn[0].text.find("Move window down") != std::string::npos);
  CHECK(app.currentMode() == ApplicationMode::Key);  // has no mode restriction, but didn't leave Key either

  REQUIRE(app.feedByte(0x0a, std::nullopt));
  CHECK(app.feedByte('a', std::nullopt));
  const auto &linesJaAgain = app.activeContentView().lines();
  CHECK(linesJaAgain[0].text.find("Move window down") == std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(ToggleHelpLanguage): refreshes an open HELP session in place, "
                           "without pushing a nested Cut level (^G still returns to Directory in one press)") {
  REQUIRE(app.loadHelpText(fm::expandHelpMarkup(fm::kHelpTextJa)));
  REQUIRE(app.loadGuiHelpText(fm::expandHelpMarkup(fm::kHelpTextGuiJa)));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartHelp));
  CHECK(app.currentMode() == ApplicationMode::Cut);
  std::size_t japaneseSegmentCount = app.cutMode().segments().size();

  CHECK(app.handleCommand(Command::ToggleHelpLanguage));
  CHECK(app.currentMode() == ApplicationMode::Cut);  // still Cut, not pushed into a deeper level
  // kHelpTextEn's own section structure differs from kHelpTextJa's (HELP.jp-derived) one --
  // switching languages while HELP is open genuinely reloads different content, not just relabels it.
  CHECK(app.cutMode().segments().size() != japaneseSegmentCount);

  CHECK(app.handleCommand(Command::Back));  // ^G: one press back to wherever HELP was opened from
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(ToggleHelpLanguage): has no mode restriction, and is a harmless "
                           "no-op (beyond flipping the flag) when HELP/KEY isn't open") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK(app.handleCommand(Command::ToggleHelpLanguage));
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

TEST_CASE_FIXTURE(Fixture, "saveBufferToFile (^O^W, K-17): writes the active view's lines to the "
                           "current work file (w.1 on first use)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  CHECK(app.feedByte(0x17, std::nullopt));    // ^W

  std::ifstream in(root / ".usilic" / "w.1");
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
  // FULL path, not the bare entry name (2026-08-18 fix, a direct user
  // report: SHELL's own current directory isn't guaranteed to match
  // DirectoryMode's -- see shellInsertName()'s own comment).
  CHECK(app.shell().commandLine().text() == "echo " + (root / "plain.txt").string());  // NOT submitted
}

TEST_CASE_FIXTURE(Fixture, "shellInsertBufferContent (^J^W, K-64): writes the buffer then PRE-FILLS cat <file> "
                           "(2026-08-18 REDESIGN: saves under <homeDirectory>/.usilic/<mode name> instead of "
                           "a numbered w.N -- a direct user request to eliminate w.N usage from ^J^W entirely, "
                           "K-17/saveBufferToFile() remains the one remaining w.N writer)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  bool ok = app.feedByte(0x17, std::nullopt);  // ^W
  if (!ok) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(app.currentMode() == ApplicationMode::Shell);
  // Directory mode's own current directory's basename (the Fixture's
  // own temp root dir's name here) -- see shellInsertBufferContent()'s
  // own comment for why "usilic"/"CLAUDE.md"-style basenames, not w.N.
  std::string savePath = (root / ".usilic" / "saved" / root.filename()).string();
  // Single-quoted -- see the ^J^S candidate-list test above's own
  // 2026-08-27 comment.
  CHECK(app.shell().commandLine().text() == "cat '" + savePath + "'");

  std::ifstream in(savePath);
  REQUIRE(in.good());
  std::string firstLine;
  std::getline(in, firstLine);
  CHECK(firstLine.find("./") != std::string::npos);  // the directory listing's own "./" entry line
}

// 2026-09-09 addition, a direct user report ("^J^W^Mでshellの中で
// ＢＳ世界の...の内容が表示される→^X^D→履歴リストに/Users/haa15030/
// .usilic/saved/が無い。SHELLでcatする際、同ディレクトリにアクセス
// したのは事実なので、それもまた履歴への登録条件に加えてほしい"):
// <homeDirectory>/.usilic/saved/ is genuinely accessed here (it's
// where the cat'd file lives) even though the user never navigated
// there via DirectoryMode::enterDirectory() -- register it into
// RingCategory::RecentDirectory here too, matching enterDirectory()'s
// own existing pushRecent() convention (trailing "/").
TEST_CASE_FIXTURE(Fixture, "shellInsertBufferContent (^J^W) registers <homeDirectory>/.usilic/saved/ into "
                           "RingCategory::RecentDirectory, even though DirectoryMode::enterDirectory() was "
                           "never called for it") {
  REQUIRE(app.start(path()));
  bool ok = app.feedByte(0x0a, std::nullopt) && app.feedByte(0x17, std::nullopt);  // ^J ^W
  if (!ok) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(app.currentMode() == ApplicationMode::Shell);

  std::string savedDir = (root / ".usilic" / "saved").string() + "/";
  const auto &ring = history.recentEntries(RingCategory::RecentDirectory);
  CHECK(std::find(ring.begin(), ring.end(), savedDir) != ring.end());
}

TEST_CASE_FIXTURE(Fixture, "shellInsertBufferContent (^J^W) also works from WITHIN the ^J^S/^J^C candidate "
                           "list (2026-08-11 fix: a real report + reference screenshot comparison against "
                           "legacy found ^J^W became literal \"JW\" text in the edited pattern instead, "
                           "since feedRegexListByte() didn't know leader sequences exist -- matches key.c's "
                           "own T_input-vs-list-navigation distinction: leader bytes are commands everywhere "
                           "except while actually editing text). Still uses the w.N fallback (2026-08-18 "
                           "REDESIGN, see shellInsertBufferContent()'s own comment) -- ListBrowser has no "
                           "established \"mode name\" concept the way Directory/File now do, so this scenario "
                           "keeps the old behavior rather than becoming a silent no-op and breaking the "
                           "ensureShellActive() regression test right above, which relies on ^J^W reaching "
                           "Shell from exactly this same context") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentEgrepPattern, "one");
  history.pushRecent(RingCategory::RecentEgrepPattern, "two");
  REQUIRE(app.handleCommand(Command::StartEgrep));  // ^J^S: candidate list
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  bool ok = app.feedByte(0x17, std::nullopt);  // ^W
  if (!ok) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(app.currentMode() == ApplicationMode::Shell);
  std::string wPath = (root / ".usilic" / "w.1").string();
  // Single-quoted -- see the ^J^S candidate-list test above's own
  // 2026-08-27 comment.
  CHECK(app.shell().commandLine().text() == "cat '" + wPath + "'");

  // The written buffer is the candidate LIST's own content (one
  // pattern per line), not the directory listing underneath it.
  std::ifstream in(wPath);
  REQUIRE(in.good());
  std::string firstLine;
  std::getline(in, firstLine);
  CHECK((firstLine == "one" || firstLine == "two"));
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

// 2026-08-14 addition, per direct user instruction ("旧版のKEYには収録
// し忘れだが「1ページ戻る」は^[wではなくて、emacs同様^[vで反映してほしい"):
// legacy's own old/src/key.c binds bare 'w' page-back and ESC-only to
// M_head/M_tail's own escPending handler above -- ESC-v/ESC-w aren't
// in legacy's own table at all (a real, acknowledged omission there,
// per the user's own framing), so this is a deliberate NEW binding,
// not a literal port. Reuses the exact same Command::PageUp already
// bound to plain 'b'/^B (see resolveCommand()'s own switch) rather
// than inventing a new distinct command -- legacy's M_kback/M_back
// both reach the same underlying m_page() page-back behavior.
TEST_CASE_FIXTURE(Fixture, "feedByte: ESC v pages back one page (Command::PageUp), matching Emacs' M-v") {
  REQUIRE(app.start(path()));
  fm::ContentView &view = app.activeContentView();
  int last = static_cast<int>(view.lines().size()) - 1;
  REQUIRE(last > 0);
  view.setCursorIndex(last);

  REQUIRE(app.feedByte(0x1b, std::nullopt));
  CHECK(app.feedByte('v', std::nullopt));
  int afterEscV = view.cursorIndex();

  view.setCursorIndex(last);
  app.handleCommand(Command::PageUp);
  CHECK(view.cursorIndex() == afterEscV);
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

// 2026-09-08、直接のユーザー依頼("^Ol⇒^Oo（←ファイル名以外は
// omission）")によるキー再編で'l'から'o'へ移行。
TEST_CASE_FIXTURE(Fixture, "feedByte: ^O o toggles DirectoryMode::filenameOnlyMode() and persists it to History") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  REQUIRE(idx >= 0);
  CHECK_FALSE(app.directoryMode().filenameOnlyMode());
  CHECK(history.lsMode() == 0);
  CHECK(app.activeContentView().lines()[idx].text != "plain.txt");

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  CHECK(app.feedByte('o', std::nullopt));
  CHECK(app.directoryMode().filenameOnlyMode());
  CHECK(history.lsMode() != 0);
  CHECK(app.activeContentView().lines()[idx].text == "plain.txt");

  REQUIRE(app.feedByte(0x0f, std::nullopt));
  CHECK(app.feedByte('o', std::nullopt));
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

// 2026-08-20 extension: GoToParentDirectory (^O^K, K-19) used to be
// refused outside Directory mode entirely -- a direct user report
// ("新：^O^KはFILE、ARC、等で無反応") expected it to work "everywhere",
// same shape as K-18/^J^K's own 2026-08-16 generalization
// (goToFileSelector()). From File mode this now goes ONE level further
// than ^J^K would -- the PARENT of the file's own containing directory
// -- mirroring Directory mode's own goToParent() semantics exactly.
TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToParentDirectory): from File mode, goes to the PARENT of the "
                           "file's own containing directory") {
  fs::path subdir = root / "subdir";
  fs::create_directories(subdir);
  { std::ofstream(subdir / "target.txt") << "x"; }

  REQUIRE(app.start(path()));
  int subdirIdx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != subdirIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // descend into subdir/
  int fileIdx = indexOf("target.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != fileIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // open target.txt
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK(app.handleCommand(Command::GoToParentDirectory));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == path());  // one level above subdir/ -- root itself
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToParentDirectory): refused from a mode with no current "
                           "file/archive to compute a parent from") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  REQUIRE(app.currentMode() == ApplicationMode::Key);

  CHECK_FALSE(app.handleCommand(Command::GoToParentDirectory));
}

// 2026-08-14 addition, K-18 (^J^K, "Go to file-selector") -- a direct
// user request ("検索ファイル所在地へ降りるためのキーアサインも実は
// あるので反映してほしい（^J^K）"), previously unimplemented in this
// port. See goToFileSelector()'s own comment on scope.
TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToFileSelector): from File mode, jumps to a real `ls` listing "
                           "of the file's own containing directory") {
  fs::path subdir = root / "subdir";
  fs::create_directories(subdir);
  { std::ofstream(subdir / "target.txt") << "x"; }

  REQUIRE(app.start(path()));
  int subdirIdx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != subdirIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // descend into subdir/
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.directoryMode().currentDirectory() == subdir.string() + "/");
  int fileIdx = indexOf("target.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != fileIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // open target.txt
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.fileMode().currentFile() == (subdir / "target.txt").string());

  CHECK(app.handleCommand(Command::GoToFileSelector));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == subdir.string() + "/");
}

// 2026-08-27 addition, a direct user report ("^J^K: ARCで利かない"):
// goToFileSelector()'s own sourceFile dispatch never had an Archive/
// ArchiveContent branch at all, unlike every other excursion mode
// (File/Cut/CutContent/ListBrowser/Locate, all covered above). Now
// reads archiveMode_.currentArchive(), same shape as the File-mode
// test just above.
TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToFileSelector): also works from Archive mode, jumping to the "
                           "archive's own containing directory") {
  REQUIRE(app.start(path()));
  int idx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Archive);
  REQUIRE(app.archiveMode().currentArchive() == path() + "archive.tar");

  CHECK(app.handleCommand(Command::GoToFileSelector));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == path());
}

// 2026-08-15 addition: a direct user report ("^Gで一つ前のディレクト
// リ...へ戻らない（(ls)での^Gは一つ前と行き来）", screenshots =bd/
// =bf/=bg) -- ^X^D/^R's own RecentDirectory branch already establishes
// this "toggle back and forth" relationship (resetNavigationToFloor())
// for every OTHER way of landing in a new directory; goToFileSelector()
// was the one entry point that hadn't been given the same treatment.
TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToFileSelector): ^G toggles back to the directory the "
                           "original file's own session started from (resetNavigationToFloor())") {
  // Mirrors the real user scenario exactly: a file opened via LOCATE
  // (directoryMode_.currentDirectory() is NEVER touched by that path --
  // openSelectedLocateResult() goes straight to fileMode_.loadFile(),
  // see its own comment), so goToFileSelector()'s own captured
  // "previous directory" is genuinely DIFFERENT from the file's own
  // containing directory. A version of this test that instead opened
  // the file via a normal Directory descent into the SAME subdir first
  // made this a degenerate no-op (previousDirectory == the new
  // directory itself, since DirectoryMode's currentDirectory_ was
  // already sitting there) -- not what the real report was about.
  fs::path subdir = root / "subdir";
  fs::create_directories(subdir);
  fs::path target = subdir / "target.txt";
  { std::ofstream(target) << "x"; }
  fs::path fakeLocate = root / "fake_locate_floor.sh";
  {
    std::ofstream script(fakeLocate);
    script << "#!/bin/sh\necho '" << target.string() << "'\n";
  }
  std::filesystem::permissions(fakeLocate, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
  configuration.setCommandForTest(ConfiguredRole::Locate, {{fakeLocate.string()}, std::nullopt});

  REQUIRE(app.start(path()));
  REQUIRE(app.directoryMode().currentDirectory() == path());
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("anything");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.handleCommand(Command::Enter));  // open the (only) LOCATE result
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.fileMode().currentFile() == target.string());
  REQUIRE(app.directoryMode().currentDirectory() == path());  // untouched by the LOCATE path

  REQUIRE(app.handleCommand(Command::GoToFileSelector));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.directoryMode().currentDirectory() == subdir.string() + "/");

  CHECK(app.handleCommand(Command::Back));  // ^G
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == path());  // toggled back to the original root directory
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^K dispatches GoToFileSelector") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  app.feedByte(0x0a, std::nullopt);  // ^J
  CHECK(app.feedByte(0x0b, std::nullopt));  // ^K
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == path());
}

// 2026-08-27 addition, a direct user request ("^J^J: 履歴直近ファイル
// へ戻る...^J^K同様どこでもキーなので反映よろしく"): unlike ^J^K
// (goToFileSelector(), which needs per-mode branching to even know
// what its OWN target is), ^J^J reads History::recentEntries(
// RingCategory::RecentFile) directly, so it's meaningful from
// literally anywhere -- these tests cover the exact motivating
// scenario: opening an archive, descending several plain directories
// away from it (where ^G alone can no longer reach the archive, only
// the immediate parent directory), then using ^J^J to jump straight
// back.
TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToRecentFile): jumps back to an archive after descending several "
                           "PLAIN directories away from it, where ^G alone can no longer reach it") {
  REQUIRE(app.start(path()));
  int idx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Archive);
  REQUIRE(app.archiveMode().currentArchive() == path() + "archive.tar");

  // Leave the archive and descend into two ORDINARY directories --
  // ^G from the second one only reaches the first (the ordinary
  // ModeStack/directory-stack mechanism), never back to the archive.
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.directoryMode().descendTo(path() + "subdir"));
  REQUIRE(app.directoryMode().currentDirectory() == path() + "subdir/");

  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  CHECK(app.feedByte(0x0a, std::nullopt));    // ^J again
  CHECK(app.currentMode() == ApplicationMode::Archive);
  CHECK(app.archiveMode().currentArchive() == path() + "archive.tar");
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToRecentFile): also reaches an ordinary (non-archive) file, "
                           "matching openSelectedListEntry()'s own RecentFile handling") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);

  CHECK(app.handleCommand(Command::GoToRecentFile));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == path() + "plain.txt");
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToRecentFile): false when nothing has ever been opened this "
                           "session yet") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.handleCommand(Command::GoToRecentFile));
}

// 2026-08-27 addition, a direct user request ("^G同様、今と直近を
// 行き来して欲しい"): this used to always jump to RecentFile's own
// literal last entry, even if that's the EXACT file/archive already
// being viewed -- pressing ^J^J a SECOND time (right after the first
// hop landed on it) just reopened the same thing again instead of
// toggling back to whatever was open before it, unlike
// goToRecentDirectory()'s own pre-existing skip-current walk. Now
// mirrors that walk here too.
TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToRecentFile): a SECOND press while already viewing the first "
                           "hop's target toggles back, instead of reopening the same file again") {
  REQUIRE(app.start(path()));
  int idxA = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idxA) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // open plain.txt (A)
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.handleCommand(Command::Back));

  int idxB = indexOf("log.txt");
  // GoToTop first: "log.txt" sorts BEFORE "plain.txt" in `ls`'s own
  // alphabetical order, and the cursor rests on plain.txt's own row
  // after the Back above -- MoveDown alone can only search forward, so
  // without this the loop below would never terminate.
  REQUIRE(app.handleCommand(Command::GoToTop));
  while (indexOf(app.directoryMode().selectedEntry()->name) != idxB) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // open log.txt (B) -- RecentFile ring: [..., plain.txt, log.txt]
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.fileMode().currentFile() == path() + "log.txt");

  REQUIRE(app.handleCommand(Command::GoToRecentFile));  // first hop: B -> A (plain.txt)
  REQUIRE(app.fileMode().currentFile() == path() + "plain.txt");

  CHECK(app.handleCommand(Command::GoToRecentFile));  // second hop, while viewing A -- must toggle to B, not reopen A
  CHECK(app.fileMode().currentFile() == path() + "log.txt");
}

// 2026-08-27 addition, a direct user proposal ("「スタックが空」で
// ^Gの時は、DIR直近履歴へ戻るルーチンへ移行ってのは？（以後は
// ループ）"): once the ModeStack is exhausted, Back no longer dead-
// ends -- it falls back to goToRecentDirectory() (RingCategory::
// RecentDirectory's own most recently visited DIFFERENT directory),
// which -- since entering a directory always re-pushes it onto that
// same ring -- naturally keeps alternating between the last two
// distinct directories on each further Back press, with no dedicated
// "how far back" state needed.
TEST_CASE_FIXTURE(Fixture, "handleCommand(Back): once the ModeStack is empty, falls back to "
                           "goToRecentDirectory() and keeps toggling between the last two directories") {
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // descendTo(subdir/) -- pushes ONE ModeStack frame
  REQUIRE(app.directoryMode().currentDirectory() == path() + "subdir/");

  REQUIRE(app.handleCommand(Command::Back));  // pops that one frame -- ordinary goBack(), stack now empty
  REQUIRE(app.directoryMode().currentDirectory() == path());

  CHECK(app.handleCommand(Command::Back));  // stack empty -- falls back to goToRecentDirectory()
  CHECK(app.directoryMode().currentDirectory() == path() + "subdir/");
  CHECK(app.handleCommand(Command::Back));  // toggles back again
  CHECK(app.directoryMode().currentDirectory() == path());
}

// 2026-08-27 fix, a real report ("^Gだが無反応な時がある。状況は覚え
// てないがDIR履歴が複数あるのは確認"): goToRecentDirectory() used to
// RETURN on the very first ring entry different from currentDirectory(),
// whether or not enterDirectory() on it actually succeeded -- a STALE
// entry (a directory since deleted) made this a dead end ("無反応")
// even with a perfectly good, still-existing, genuinely different
// entry sitting right behind it in the ring. Engineered here: the ring
// ends up as [root/, subdir/, root/, deleteme/, root/] with deleteme/
// removed from disk before the final Back -- the walk's first non-
// current candidate (deleteme/) now fails and must be skipped in favor
// of subdir/ two entries further back, instead of giving up.
TEST_CASE_FIXTURE(Fixture, "handleCommand(Back): goToRecentDirectory() skips a ring entry that no longer "
                           "exists on disk, instead of giving up on the first one tried") {
  REQUIRE(app.start(path()));  // ring: [root/]
  REQUIRE(app.directoryMode().descendTo((root / "subdir").string()));  // ring: [root/, subdir/]
  REQUIRE(app.handleCommand(Command::Back));                           // ring: [root/, subdir/, root/]
  REQUIRE(app.directoryMode().currentDirectory() == path());

  fs::create_directory(root / "deleteme");
  REQUIRE(app.directoryMode().descendTo((root / "deleteme").string()));  // ring: [..., root/, deleteme/]
  REQUIRE(app.handleCommand(Command::Back));                             // ring: [..., deleteme/, root/]; stack empty
  REQUIRE(app.directoryMode().currentDirectory() == path());

  fs::remove(root / "deleteme");  // now a dangling ring entry

  CHECK(app.handleCommand(Command::Back));  // stack empty -- goToRecentDirectory() must skip deleteme/ and reach subdir/
  CHECK(app.directoryMode().currentDirectory() == (root / "subdir").string() + "/");
}

// 2026-08-23 fix, a direct user report: already being IN the (ls)
// listing this key would otherwise land you on used to make it a
// hard no-op -- legacy's own equivalent doubles as a re-list even
// when already there (e.g. right after overwriting a file in place
// via CP, to refresh the stale size/mtime column without navigating
// away and back).
TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToFileSelector): from Directory mode, re-runs `ls` on the SAME "
                           "directory instead of being a no-op") {
  REQUIRE(app.start(path()));
  CHECK(app.handleCommand(Command::GoToFileSelector));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == path());
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToFileSelector): refused outside File/Directory/etc modes") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  REQUIRE(app.currentMode() == ApplicationMode::Key);
  CHECK_FALSE(app.handleCommand(Command::GoToFileSelector));
}

// 2026-08-27 addition, a real report: pressing ^J^K FROM Shell mode
// used to land on shellReturnMode_ (whatever mode Shell was entered
// from) instead of doing what ^J^K actually promises -- feedShellByte()'s
// own blanket pre-switch (see its own comment) ran BEFORE
// goToFileSelector() ever saw currentMode_==Shell, so its per-mode
// branches (none of which included Shell) silently did nothing while
// that clobbered currentMode_ stuck around as a side effect. Both the
// pre-switch exclusion (isShellSafeGoToFileSelector) and
// goToFileSelector()'s own new Shell branch are needed together --
// this test only passes with both in place.
TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToFileSelector) / ^J^K: from Shell mode, goes to a real `ls` "
                           "listing of the SHELL's own actual working directory, not shellReturnMode_") {
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // descend into subdir/ first
  REQUIRE(app.directoryMode().currentDirectory() == path() + "subdir/");

  bool shellOk = app.feedByte(0x0f, std::nullopt);  // ^O
  shellOk = shellOk && app.feedByte('s', std::nullopt);  // s -- RestartShell (K-60), works from any mode
  if (!shellOk || app.currentMode() != ApplicationMode::Shell) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  // cd to a directory DIFFERENT from subdir/ (shellReturnMode_'s own
  // directory) -- the whole point of this test is distinguishing "the
  // SHELL's real cwd" from "wherever Shell happened to be entered
  // from", which the pre-2026-08-27 bug conflated.
  app.shell().commandLine().setText("cd '" + root.string() + "'");
  app.shell().submitLine();
  REQUIRE(pumpUntil(app, [&] { return app.shell().currentDirectory() == root.string(); }));

  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  CHECK(app.feedByte(0x0b, std::nullopt));    // ^K
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == root.string() + "/");
}

// 2026-08-27 addition, a real report ("^J^K→カーソル位置が
// 「usilicrc」じゃない"): opening a file (Enter, NOT descendTo()/
// goBack()) never saved the cursor's own row for this directory, so a
// later goToFileSelector() call landing back here via plain
// enterDirectory() had nothing but a stale/missing saved position to
// restore.
TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToFileSelector): from File mode, restores the cursor to the SAME "
                           "row the file was opened from, even though opening a file isn't descendTo()/"
                           "goBack() navigation") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.directoryMode().selectedEntry()->name == "plain.txt");
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::GoToFileSelector));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().selectedEntry() != nullptr);
  CHECK(app.directoryMode().selectedEntry()->name == "plain.txt");
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToFileSelector): also works from Cut mode (2026-08-16 fix: a "
                           "real report -- \"CUTでは？と思い試そうとしたら^J^Kが不能だった...本来どこでも "
                           "使えるキーアサインなので\" -- usilicrc's own K-? section groups K-18 with "
                           "plainly-universal bindings, not just File)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("hello");  // matches plain.txt's own first line ("hello\nworld\n")
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);

  CHECK(app.handleCommand(Command::GoToFileSelector));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == path());
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToFileSelector): also works straight from LOCATE's own results "
                           "list, without opening a result first (2026-08-16 fix: a real report -- \"新^J^K: "
                           "どこでも使えない。特にLOCATE一覧=buでの^J^Kを試したい\")") {
  fs::path subdir = root / "subdir";
  fs::create_directories(subdir);
  fs::path target = subdir / "found.txt";
  { std::ofstream(target) << "x"; }
  fs::path fakeLocate = root / "fake_locate3.sh";
  {
    std::ofstream script(fakeLocate);
    script << "#!/bin/sh\necho '" << target.string() << "'\n";
  }
  std::filesystem::permissions(fakeLocate, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
  configuration.setCommandForTest(ConfiguredRole::Locate, {{fakeLocate.string()}, std::nullopt});

  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("anything");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Locate);

  CHECK(app.handleCommand(Command::GoToFileSelector));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == subdir.string() + "/");
}

// 2026-08-20 fix, root-caused via a real USILICGUIDEBUG grid-dump
// capture (^X^D in -g): promptActive() alone doesn't cover this screen
// (startPromptHistoryRecall() deliberately clears promptKind_ back to
// None -- see that function's own comment), which let runGuiPhase()'s
// hover-redraw gate (`!app.promptActive()`) fall through to a bare
// ContentView::redraw() on stray mouse motion, visibly flickering a
// stale "戻り先" trailing field in and out (listBrowserView_'s OLD
// ModeLineFieldsProvider, bypassed by the real keystroke-driven
// refreshDirectEntryModeLine() path but not by an incidental hover
// redraw). redrawSensitivePromptOrRecallActive() is the fix: same as
// promptActive() PLUS this recall state, used at every hover/
// incidental-redraw suppression call site instead of promptActive()
// alone.
TEST_CASE_FIXTURE(Fixture, "redrawSensitivePromptOrRecallActive: true during ^X^F/^X^D's own candidate-recall "
                           "screen, even though promptActive() itself is deliberately false there") {
  history.pushRecent(RingCategory::RecentFile, (root / "target.txt").string());

  REQUIRE(app.start(path()));
  CHECK_FALSE(app.redrawSensitivePromptOrRecallActive());

  REQUIRE(app.handleCommand(Command::StartFileHistory));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK_FALSE(app.promptActive());                       // unchanged: still not a live text prompt
  CHECK(app.redrawSensitivePromptOrRecallActive());       // but IS redraw-sensitive now

  CHECK(app.handleCommand(Command::Back));  // ^G cancels back out
  CHECK_FALSE(app.redrawSensitivePromptOrRecallActive());
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(GoToFileSelector): also works straight from ^J^F's own RecentFile "
                           "candidate list (ListBrowser), without opening it first (2026-08-16 fix: a real "
                           "report -- \"同画面で^J^Kが利かない=bo\" -- =bo being that exact candidate-list "
                           "screen)") {
  fs::path subdir = root / "subdir";
  fs::create_directories(subdir);
  fs::path target = subdir / "target.txt";
  { std::ofstream(target) << "x"; }
  history.pushRecent(RingCategory::RecentFile, target.string());

  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFileHistory));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  CHECK(app.handleCommand(Command::GoToFileSelector));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == subdir.string() + "/");
}

// 2026-08-20 fix, a real report + screenshot (=dq): opening an archive
// (utf8.tar) straight from ^X^F's own candidate list showed the raw
// binary tar header bytes as if it were plain text (no "(tar)" badge)
// -- unlike openSelectedEntry() (Directory mode's own Enter key), the
// RecentFile branch of openSelectedListEntry() called
// fileMode_.loadFile() unconditionally, never trying
// archiveMode_.loadArchive() first.
TEST_CASE_FIXTURE(Fixture, "handleCommand(Enter) from ^J^F's own RecentFile candidate list opens an archive "
                           "path as an actual archive (Archive mode), not raw file content") {
  history.pushRecent(RingCategory::RecentFile, (root / "archive.tar").string());

  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFileHistory));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  CHECK(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Archive);
  CHECK(app.archiveMode().currentArchive() == (root / "archive.tar").string());
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(Back) from File mode reached via ^J^F (RecentFile) lands on the "
                           "file's OWN containing directory, not whatever directory happened to be showing "
                           "before ^J^F was pressed (2026-08-16 fix: a real report + own diagnosis -- "
                           "\"=bo→[return]=bq→^G...結果は起動直後の（ls）なので「それ/BDF_Driver...」を表示 "
                           "しようとしてんじゃないの？\" -- openSelectedListEntry()'s RecentFile branch never "
                           "calls directoryMode_.enterDirectory(), so returnToDirectory() used to just redraw "
                           "whatever directoryMode_ already had, confirmed against legacy's own correct "
                           "behavior here)") {
  fs::path subdir = root / "subdir";
  fs::create_directories(subdir);
  fs::path target = subdir / "target.txt";
  { std::ofstream(target) << "x"; }
  history.pushRecent(RingCategory::RecentFile, target.string());

  REQUIRE(app.start(path()));
  REQUIRE(app.directoryMode().currentDirectory() == path());  // still the startup directory, unrelated to target

  REQUIRE(app.handleCommand(Command::StartFileHistory));
  REQUIRE(app.handleCommand(Command::Enter));  // opens target.txt via the RecentFile branch
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.fileMode().currentFile() == target.string());

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == subdir.string() + "/");  // NOT path() (the stale startup dir)
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

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X f with a nonexistent path fails cleanly, staying in the current mode") {
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

// 2026-09-15修正、直接のユーザー報告+依頼("最長共通接頭辞まで補完")
// を受けた訂正: "cpmv_"はcpmv_a.txt/cpmv_b.txt両方に共通する接頭辞
// そのもの(食い違うのは"cpmv_"より後の1文字だけ)なので、これは「常に
// 曖昧なら無視」ではなく「既にLCPまで打ち終わっている」ケースだった
// ――結果自体(何も変わらない)は以前と同じまま。すぐ下の新規テストが、
// LCPより「手前」で止めた場合に実際にLCPまで補完されることを検証する。
TEST_CASE_FIXTURE(Fixture, "feedByte: ^X f prompt, ^I (K-65) already at the longest common prefix "
                           "leaves the text unchanged (no further progress possible)") {
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

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X f prompt, ^I (K-65) with an ambiguous prefix completes up to "
                           "the longest common prefix (bash-style Tab, not full auto-complete)") {
  REQUIRE(app.start(path()));
  // Dedicated files with a clean 2-way shared prefix and no third
  // interloper -- root's own pre-existing "cpmv*" trio (cpmv_a.txt/
  // cpmv_b.txt/cpmvdest/) doesn't work for this: cpmvdest's own 'd'
  // diverges one character earlier than the '_'/'_' shared by the two
  // files, so the REAL common prefix across all three is just "cpmv"
  // -- already fully typed below, leaving no room for this test to
  // observe any further completion.
  { std::ofstream(root / "tabtest_alpha.txt") << "a"; }
  { std::ofstream(root / "tabtest_beta.txt") << "b"; }

  REQUIRE(app.feedByte(0x18, std::nullopt));
  REQUIRE(app.feedByte('f', std::nullopt));

  // "tabtest" matches both new files, but their common prefix
  // "tabtest_" is longer than what's typed so far -- Tab should
  // complete that far, even though the match itself is still
  // ambiguous beyond it.
  for (char c : (root / "tabtest").string()) app.feedByte(static_cast<unsigned char>(c), std::nullopt);
  CHECK(app.feedByte(0x09, std::nullopt));
  // The completed text (up to the shared "tabtest_") is actually
  // visible in the painted prompt row, not just inferred from
  // non-resolution -- checked as a short suffix rather than the full
  // (possibly long, possibly horizontally-scrolled-off) absolute root
  // path.
  CHECK(display.rowText(display.rows() - 1).find("tabtest_") != std::string::npos);
  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));  // still ambiguous -- doesn't resolve to a real file
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

TEST_CASE_FIXTURE(Fixture, "SHELL: ^A/^E/^B/^F/^D edit the command line, not insert as literal control bytes "
                           "(2026-08-16 fix: a real report -- \"^A^DDDD'cat'...を打とうとしたらハイライトの "
                           "ADDDDだった\" -- the 2026-08-12 fix wired these into feedPromptByte()'s own "
                           "promptEditor_ but never into feedShellByte()'s separate commandLine() editor)") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("xbc")) app.feedShellByte(c, std::nullopt);
  REQUIRE(app.shell().commandLine().text() == "xbc");

  CHECK(app.feedShellByte(0x01, std::nullopt));  // ^A: home
  CHECK(app.feedShellByte(0x04, std::nullopt));  // ^D: delete forward -- removes the leading 'x'
  CHECK(app.shell().commandLine().text() == "bc");

  app.feedShellByte('a', std::nullopt);
  CHECK(app.shell().commandLine().text() == "abc");  // inserted at the still-home cursor, not appended

  CHECK(app.feedShellByte(0x05, std::nullopt));  // ^E: end
  app.feedShellByte('!', std::nullopt);
  CHECK(app.shell().commandLine().text() == "abc!");  // now appends, confirming ^E actually moved to the end
}

// 2026-09-10 addition, same shape as the ^A/^E/^B/^F/^D gap just above
// (a real user report: "SHELLでgrep '^V^I' *.[ch]とやっても^Iが入ら
// ない。^Q^Iでは^Qだけが渡る") -- ^Q (quoted insert) was wired into
// feedPromptByteInner()'s shared promptEditor_ back on 2026-08-11 but,
// like ^A/^E/^B/^F/^D/^U/^K/^Y/ESC before it, was never ported to
// feedShellByte()'s separate commandLine() editor. Without it, Tab
// (0x09) always triggers Shell::onTab()'s own completion with no way
// to insert it literally into the LOCAL command-line buffer.
//
// Getting a literal Tab all the way into a command that actually
// EXECUTES against the real child shell needs one more step this test
// doesn't cover: the local buffer holding a literal Tab byte is only
// half the story, because Shell mode forwards the assembled line to a
// genuinely interactive child shell (zsh/bash) with its OWN line
// editor (ZLE/readline) reading it as if a human were typing it live
// -- so that receiving shell ALSO intercepts a raw Tab as ITS OWN
// completion trigger unless the line also embeds THAT shell's own
// quoted-insert prefix (^V, zsh's and bash's default) right before it.
// Verified interactively via PTY: building "grep '<^V><Tab>' *.txt"
// (using this new ^Q for the Tab; ^V itself needs no quoting since
// feedShellByte() doesn't intercept it) actually finds a real tab
// character once submitted, matching typing "^V^I" at a genuine
// interactive shell prompt. This test only verifies the (necessary,
// but not by itself sufficient) local-buffer half.
TEST_CASE_FIXTURE(Fixture, "SHELL: ^Q (quoted insert) inserts the NEXT byte literally into the command "
                           "line instead of triggering its usual meaning (e.g. Tab's Shell::onTab())") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("ab")) app.feedShellByte(c, std::nullopt);
  CHECK(app.feedShellByte(0x11, std::nullopt));  // ^Q: arms quoted-insert
  // The very next byte would normally trigger Shell::onTab()'s
  // filename completion -- quoted, it's inserted literally instead.
  CHECK(app.feedShellByte(0x09, std::nullopt));
  CHECK(app.shell().commandLine().text() == std::string("ab\x09"));

  app.feedShellByte('c', std::nullopt);
  CHECK(app.shell().commandLine().text() == std::string("ab\x09" "c"));
}

TEST_CASE_FIXTURE(Fixture, "SHELL: typing a command and pressing Enter runs it, output reaches scrollback") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("printf 'APPSHELLTEST123\\n'")) app.feedShellByte(c, std::nullopt);
  CHECK(app.feedShellByte(0x0d, std::nullopt));  // ^M submits

  bool sawOutput = pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("APPSHELLTEST123") != std::string::npos) return true;
    return false;
  });
  CHECK(sawOutput);
}

// 2026-08-17 fix: Shell::setProgramLauncher() (ported from
// input_shell()'s do_prog() hand-off) was declared but never wired --
// a real report ("'less CLA'^I→[return]→less画面でなく「cat
// CLAUDE.md」のような画面...[return]連打で一行ずつ進む") found a
// recognized PROGRAM command typed AT THE SHELL PROMPT going to the
// real, pty-attached bash instead, whose raw full-screen output then
// got mangled by Shell's own line-oriented scrollback pipeline. "echo"
// is the Fixture's own configured PROGRAM entry -- a real recognized
// command reaching this handoff, not `less`/`vi` (this test suite
// never drives a genuinely interactive full-screen program, matching
// every other PROGRAM-prompt test's own "echo ... > outPath" real
// side-effect pattern instead of asserting terminal escape sequences).
TEST_CASE_FIXTURE(Fixture, "SHELL: a recognized PROGRAM command (e.g. \"echo\", the Fixture's own configured "
                           "entry) is intercepted and run via ProgramMode -- NOT sent to the real pty-attached "
                           "bash -- when typed at the live SHELL prompt") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  std::string outPath = (root / "shell_program_out.txt").string();
  feedText("echo hello > '" + outPath + "'");
  CHECK(app.feedShellByte(0x0d, std::nullopt));

  // Real side effect proves it actually ran (via EITHER path -- this
  // alone doesn't distinguish bash from the launcher).
  std::ifstream in(outPath);
  std::string content;
  std::getline(in, content);
  CHECK(content == "hello");

  // The real bash child never saw this command at all -- its own
  // output-detection/echo pipeline (flushNewScrollbackLines(), prompt
  // re-display, etc.) never ran for it, so scrollback shows only the
  // frozen prompt-echo line submitLine() always adds regardless of
  // handoff, never a SEPARATE output line (there's no output to see:
  // the redirection sent "hello" to outPath, not to any terminal).
  int outputLines = 0;
  for (const auto &line : app.shell().scrollback().lines())
    if (!line.shellCommand && line.text.find("hello") != std::string::npos) outputLines++;
  CHECK(outputLines == 0);

  // recordedCommand (ProgramMode::run()'s own mechanism) still records
  // the CLEAN typed command, not some internal cd-wrapped variant.
  const auto &entries = history.recentEntries(RingCategory::ShellCommand);
  bool recorded =
      std::find(entries.begin(), entries.end(), "echo hello > '" + outPath + "'") != entries.end();
  CHECK(recorded);
}

TEST_CASE_FIXTURE(Fixture, "SHELL: a real submitted command is actually recorded into RingCategory::ShellCommand "
                           "(2026-08-02: setHistoryRecorder() was declared but never wired to anything)") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("printf 'RECORDTEST789\\n'")) app.feedShellByte(c, std::nullopt);
  app.feedShellByte(0x0d, std::nullopt);

  const auto &entries = history.recentEntries(RingCategory::ShellCommand);
  bool recorded = std::find(entries.begin(), entries.end(), "printf 'RECORDTEST789\\n'") != entries.end();
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

  for (unsigned char c : std::string("printf 'MARKERPN001\\n'")) app.feedShellByte(c, std::nullopt);
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

  // 2026-08-25 update (a real, carefully-reproduced report: "③を飛び
  // 越えて②へ" -- see Shell::historyBrowsing()'s own declaration
  // comment): the FIRST ^P from rest now lands ON the current --
  // already correct -- newest entry (cursorIndex_ UNCHANGED, only the
  // independent historyBrowsing() flag flips true) instead of
  // immediately skipping past it; only a SECOND press actually moves.
  int bottom = app.shell().scrollback().cursorIndex();
  CHECK(app.feedShellByte(0x10, std::nullopt));  // ^P (1st): enters browsing, no move
  CHECK(app.shell().scrollback().cursorIndex() == bottom);
  CHECK(app.shell().historyBrowsing());
  CHECK(app.currentMode() == ApplicationMode::Shell);  // navigating history does NOT leave Shell mode

  CHECK(app.feedShellByte(0x10, std::nullopt));  // ^P (2nd): now actually moves up
  CHECK(app.shell().scrollback().cursorIndex() == bottom - 1);

  CHECK(app.feedShellByte(0x0e, std::nullopt));  // ^N
  CHECK(app.shell().scrollback().cursorIndex() == bottom);
}

TEST_CASE_FIXTURE(Fixture, "SHELL: ^P/^N show a plain text cursor while browsing, not a reverse-video "
                           "highlight bar (2026-08-04 user report: legacy's stand_str() never highlights SHELL "
                           "scrollback_ at all, see Shell's own constructor comment)") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("printf 'MARKERPN003\\n'")) app.feedShellByte(c, std::nullopt);
  app.feedShellByte(0x0d, std::nullopt);
  REQUIRE(pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (!line.shellCommand && line.text.find("MARKERPN003") != std::string::npos) return true;
    return false;
  }));

  CHECK(app.feedShellByte(0x10, std::nullopt));  // ^P
  // No line anywhere in scrollback_'s own CONTENT viewport (rows
  // 0..viewportRows_-3, see Shell's own constructor comment) is
  // reverse-video -- the whole-line highlight legacy never draws for
  // SHELL is off. Deliberately excludes the last two rows (the live
  // prompt row and the mode-line row): the mode-line legitimately
  // draws its own position indicator in standout (see
  // ContentView::renderModeLine()'s drawSegment()), which is
  // unrelated to this fix.
  for (int row = 0; row < display.rows() - 2; row++)
    for (int col = 0; col < display.columns(); col++) CHECK_FALSE(display.standoutAt(row, col));
  // The display's own real cursor moved to the browsed-to line instead
  // (a plain text cursor, matching how the live prompt's own editing
  // cursor already works -- see LineEditor::redraw()'s own moveCursor()
  // call).
  CHECK(display.cursorRow() == app.shell().scrollback().cursorDisplayRow());
}

TEST_CASE_FIXTURE(Fixture, "SHELL: Enter on a past prompt line resubmits its command, matching legacy shell.c's "
                           "own '\\r' case (the user's own words: \"^Pでそこへ戻ってEnterでlsが再実行される\")") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("printf 'MARKERRERUN002\\n'")) app.feedShellByte(c, std::nullopt);
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
      if (lines[i].shellCommand && *lines[i].shellCommand == "printf 'MARKERRERUN002\\n'")
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

  for (unsigned char c : std::string("printf 'MARKERPLAIN003\\n'")) app.feedShellByte(c, std::nullopt);
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

  for (unsigned char c : std::string("printf 'MARKERTYPE004\\n'")) app.feedShellByte(c, std::nullopt);
  app.feedShellByte(0x0d, std::nullopt);
  // !line.shellCommand: wait for the shell's REAL echoed output line
  // specifically (see the ^P/^N test's own comment above) so ^P below
  // has somewhere to actually move to.
  REQUIRE(pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (!line.shellCommand && line.text.find("MARKERTYPE004") != std::string::npos) return true;
    return false;
  }));

  // 2026-08-25 update (see the ^P/^N test's own comment above): the
  // FIRST ^P now only enters browsing mode without moving cursorIndex_
  // -- a SECOND press is needed here to actually move off the bottom.
  app.feedShellByte(0x10, std::nullopt);  // ^P (1st): enters browsing, no move
  app.feedShellByte(0x10, std::nullopt);  // ^P (2nd): now browsing, not at the bottom
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

  for (unsigned char c : std::string("printf 'PERSISTMARKER456\\n'")) app.feedShellByte(c, std::nullopt);
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

TEST_CASE_FIXTURE(Fixture, "SHELL: `exit` ends the child on its own, automatically leaves Shell mode "
                           "(matching legacy shell.c's own kill(spid,0) check -- no ^G needed), and "
                           "re-entering spawns a genuinely fresh session (2026-08-02: \"このほうがSHELLの"
                           "存在意義がはっきりしていい\")") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));

  for (unsigned char c : std::string("printf 'BEFOREEXIT789\\n'")) app.feedShellByte(c, std::nullopt);
  app.feedShellByte(0x0d, std::nullopt);
  REQUIRE(pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("BEFOREEXIT789") != std::string::npos) return true;
    return false;
  }));

  int oldPid = app.shell().childPid();

  for (unsigned char c : std::string("exit")) app.feedShellByte(c, std::nullopt);
  app.feedShellByte(0x0d, std::nullopt);
  REQUIRE(pumpUntil(app, [&] { return app.currentMode() != ApplicationMode::Shell; }));
  CHECK(app.currentMode() == ApplicationMode::Directory);  // back where SHELL was entered from, no ^G needed
  CHECK_FALSE(app.shell().alive());

  REQUIRE(app.handleCommand(Command::StartShell));
  CHECK(app.currentMode() == ApplicationMode::Shell);
  CHECK(app.shell().scrollback().lines().empty());  // fresh PP chain, not the old dead session's leftovers
  CHECK(app.shell().commandLine().text().empty());

  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));
  CHECK(app.shell().childPid() != oldPid);  // a genuinely new process, not the dead one reused
}

// -- Global leader-key access from within Shell mode (^J/^X/^O, NOT
// bare ESC) -- a real user report (2026-08-02) comparing side by side
// against the legacy build found ^J^F/^X^C etc. work directly from
// within SHELL there, silently doing nothing here before this existed
// (currentMode_==Shell routed every byte to feedShellByte() with no
// leader-key access at all). See feedShellByte()'s own comment.
//
// 2026-09-08、直接のユーザー依頼によるキー再編でFILE historyは
// ^X^Fから^J^Fへ一度移行したが、2026-09-09、同じ依頼元により
// (emacsの^Xf(set-fill-column)は使用頻度が極めて低いと判明したため)
// ^X^Fへ差し戻された。EXITも一度^X^Cから^Oxへ移行した後、同じ依頼元
// により^X^Cへ差し戻された(kKeyTable自身の"^X^C"エントリのコメント
// 参照)。--

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
  // 2026-09-09redesign: DU no longer prompts at all -- it runs `du`
  // immediately on whatever's current (runDuOnCurrentFolder()'s own
  // comment), landing straight in Du mode.
  waitForDu(app);
  CHECK(app.currentMode() == ApplicationMode::Du);
  CHECK_FALSE(app.promptActive());
}

// 2026-08-26 correction (a real report: running `df`/`ls` then ESC-<
// unexpectedly landed in Directory's own (ls) instead of staying in
// SHELL): the ORIGINAL version of this test (2026-08-17) explicitly
// validated "ESC-< leaves Shell, matching ^J/^X's own precedent" as
// the deliberately-designed behavior -- a direct user follow-up
// confirmed that precedent should NOT extend to GoToTop/GoToBottom/
// PageUp specifically (plain activeView()-generic scroll commands with
// no Directory-specific meaning at all, unlike ^X^F/^X^D's own
// genuine need for Directory state), so these three now stay in Shell,
// operating on scrollback_ itself instead.
TEST_CASE_FIXTURE(Fixture, "SHELL leader keys: ESC-< (GoToTop) stays in Shell mode, scrolling scrollback_ "
                           "itself, unlike ^J/^X which still leave Shell") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));
  feedText("echo hi");
  REQUIRE(app.feedShellByte(0x0d, std::nullopt));
  REQUIRE(pumpUntil(app, [&] { return !app.shell().scrollback().lines().empty(); }));

  CHECK(app.feedShellByte(0x1b, std::nullopt));  // ESC
  CHECK(app.feedShellByte('<', std::nullopt));   // < : GoToTop (M_head)
  CHECK(app.currentMode() == ApplicationMode::Shell);  // stays in Shell now
  CHECK(app.shell().scrollback().cursorIndex() == 0);
}

TEST_CASE_FIXTURE(Fixture, "SHELL leader keys: an unrecognized ESC-sequence still leaves Shell as a side "
                           "effect, matching the same accepted edge case ^J/^X already have") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  CHECK(app.feedShellByte(0x1b, std::nullopt));        // ESC : arms the sequence
  CHECK_FALSE(app.feedShellByte('z', std::nullopt));   // z : not a recognized ESC-sequence
  CHECK(app.currentMode() == ApplicationMode::Directory);
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

// 2026-08-24 addition, a direct user request + confirmed legacy
// source read (disp.c's print_line(), the ONLY caller that ever
// passes a negative len is disp_shell()): SHELL's own position field
// is a fixed-width zero-pad ("00002/00002 END"), independent of the
// total's own digit width, unlike every other mode's ModeLine::format()
// (which only pads current to match total's own width, e.g. "03/15").
// Width is 5 -- legacy's own C source actually zero-pads to 6, but a
// same-day follow-up request ("行数6桁は多過ぎなので5桁でよろしく")
// asked for 5 instead; see ModeLine::formatFixedWidth()'s own comment.
TEST_CASE_FIXTURE(Fixture, "SHELL mode-line: position is a fixed 5-digit zero-padded counter, matching "
                           "legacy's own disp_shell()-specific print_line() branch (width narrowed to 5 "
                           "per a direct user request, legacy itself uses 6)") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));
  // scrollback_ itself stays genuinely empty (0 lines) until a real
  // command actually completes -- the initial prompt handshake alone
  // doesn't add anything to it (see currentDirectory() above, a
  // SEPARATE signal used by every other "handshake done" test in this
  // file).
  feedText("echo hi");
  REQUIRE(app.feedShellByte(0x0d, std::nullopt));
  REQUIRE(pumpUntil(app, [&] { return !app.shell().scrollback().lines().empty(); }));

  // 2026-08-26 update: the displayed total is now real lines().size()+1
  // (the live prompt row itself counts as its own line -- see
  // ContentView::setCountLivePromptAsLine()'s own declaration comment),
  // not lines().size() alone -- resting here (not browsing), current
  // reads the same "+1" total too.
  int total = static_cast<int>(app.shell().scrollback().lines().size()) + 1;
  std::string paddedTotal = std::to_string(total);
  if (paddedTotal.size() < 5) paddedTotal.insert(0, 5 - paddedTotal.size(), '0');
  std::string text = app.shell().scrollback().modeLineText();
  CHECK(text.find(paddedTotal + "/" + paddedTotal) != std::string::npos);
}

// Same request as the position-field test above: "現在行までのバイト数
// をサポートしてほしい" -- no legacy precedent for this one (a
// genuinely new capability). 2026-08-26 correction (a real, carefully-
// verified report: "④の「68」も...加算されていないのでバグ。正しい
// 表記は②③④とも「102」のはず"): this used to sum only through the
// CURSOR's own line, so browsing back through history via ^P made the
// displayed count silently shrink -- now unconditionally the total
// byte size of EVERY scrollback line, matching FileMode's own
// identically-named field's settled "actual bytes accumulated so far,
// never decreasing" spec (HISTORY.md:11323).
TEST_CASE_FIXTURE(Fixture, "SHELL mode-line: shows the total byte count of all scrollback lines, unaffected "
                           "by browsing history backward") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));
  feedText("echo hi");
  REQUIRE(app.feedShellByte(0x0d, std::nullopt));
  REQUIRE(pumpUntil(app, [&] { return !app.shell().scrollback().lines().empty(); }));

  std::size_t expectedBytes = 0;
  for (const auto &line : app.shell().scrollback().lines()) expectedBytes += line.text.size() + 1;
  REQUIRE(expectedBytes > 0);
  std::string text = app.shell().scrollback().modeLineText();
  CHECK(text.find(std::to_string(expectedBytes)) != std::string::npos);

  // Browsing backward via ^P must NOT shrink the displayed count.
  REQUIRE(app.feedShellByte(0x10, std::nullopt));
  std::string textAfterBrowsing = app.shell().scrollback().modeLineText();
  CHECK(textAfterBrowsing.find(std::to_string(expectedBytes)) != std::string::npos);
}

// 2026-08-24 fix, a real report: entering SHELL from Cut mode (^J^W)
// showed "usilic v1.5b" instead of "CUT" as the return-target badge --
// previousModeName() (shellReturnName_'s own source) had no `case
// Cut:`/`case CutContent:` at all, silently falling through to its
// `default: return "";` -- an EMPTY shellReturnName_ is what triggers
// the "usilic v1.5b" watermark fallback the provider already had for
// legacy's own "no previous mode" case.
TEST_CASE_FIXTURE(Fixture, "SHELL mode-line: entering from Cut mode shows \"CUT\" as the return-target "
                           "trailing field, not the \"usilic v1.5b\" no-previous-mode fallback") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("cpmv");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);

  if (!app.handleCommand(Command::ShellInsertBufferContent)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Shell);
  std::string text = app.shell().scrollback().modeLineText();
  CHECK(text.find("CUT") != std::string::npos);
  CHECK(text.find("usilic v1.5b") == std::string::npos);
}

// 2026-08-24 fix, a real 100%-reproducible report:
// "（ls）のCLAUDE.md上で^J^B^J^W^G" -- ^J^B opens the regex-CUT
// candidate list directly (startRegexPromptList(), currentMode_ ==
// ListBrowser, correctly labeled "CUT"); ^J^W (still WITHOUT ever
// typing/submitting a pattern) jumps straight to Shell; ^G back out
// showed the SAME candidate list mislabeled "EGREP" instead. Root
// cause: ensureShellActive() unconditionally clears
// promptHistoryRecallKind_ on entry (a real, different fix for a
// stale-flag bug, see its own comment) -- listBrowserView_'s own
// ModeLineFieldsProvider only shows the true "CUT" name when
// promptHistoryRecallKind_ is set; with it cleared, ^G's return trip
// fell back to ringCategoryLabel(listBrowserCategory_), which reads
// "EGREP" for BOTH plain EGREP and CUT's regex variant (they share
// RingCategory::RecentEgrepPattern -- see startRegexPromptList()'s own
// comment on why). Fixed by suspending (not discarding)
// promptHistoryRecallKind_/Text_/IsDirectEntry_ across the Shell
// excursion and restoring them in returnFromShell().
TEST_CASE_FIXTURE(Fixture, "CUT (regex, file, ^J^B): a quick ^J^W-then-^G round trip through Shell, with "
                           "NOTHING ever typed into the candidate list, restores it correctly labeled "
                           "\"CUT\" -- not mislabeled \"EGREP\" (which shares its own ring)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::StartCutFileRegex));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK(app.activeContentView().modeLineText().find("CUT") != std::string::npos);

  if (!app.handleCommand(Command::ShellInsertBufferContent)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Shell);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  std::string text = app.activeContentView().modeLineText();
  CHECK(text.find("CUT") != std::string::npos);
  CHECK(text.find("EGREP") == std::string::npos);
}

// 2026-09-08、直接のユーザー依頼によるキー再編で^X^Klから^J^Olへ移行
// (旧^X^Kサブリーダーが廃止され、ERROR/LOGは^J^O自体をネストした新設
// のサブリーダーへ引っ越した)。
TEST_CASE_FIXTURE(Fixture, "LOG (^J^Ol, K-68): snapshots Shell's scrollback to logFilePath_, viewable via File mode") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  app.shell().commandLine().setText("printf 'LOGTESTMARKER123\\n'");
  app.shell().submitLine();
  bool sawOutput = pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("LOGTESTMARKER123") != std::string::npos) return true;
    return false;
  });
  REQUIRE(sawOutput);

  // ^J^Ol is a leader-key sequence, only recognized by dispatchByte()'s
  // OWN state machine -- while actively in Shell mode, every byte
  // routes straight to feedShellByte() instead (see dispatchByte()'s
  // very first check), matching real usage: ^J while typing at a live
  // shell prompt is command-line input, not usilic's own leader key. ^G
  // back to Directory first, same as a real user would.
  REQUIRE(app.feedByte(0x07, std::nullopt));  // ^G
  REQUIRE(app.currentMode() == ApplicationMode::Directory);

  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  CHECK(app.feedByte('l', std::nullopt));     // l
  CHECK(app.currentMode() == ApplicationMode::File);

  bool found = false;
  for (const auto &line : app.activeContentView().lines())
    if (line.text.find("LOGTESTMARKER123") != std::string::npos) found = true;
  CHECK(found);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^O s restarts a fresh SHELL child, even from within Shell mode (K-60)") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
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
  app.shell().commandLine().setText("printf 'RESTARTSHELLTESTMARKER\\n'");
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

// -- CP/MV/RM: ^Space marking, ^Oc/^Om/^Or, real cp/mv/rm execution --

TEST_CASE("resolveCommand: 0x80 (^Space, Ctrl-Space's reported byte) resolves to ToggleMark") {
  CHECK(Application::resolveCommand(0x80, std::nullopt) == Command::ToggleMark);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(ToggleMark): toggles the mark at the cursor") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::MoveDown));  // off "./" (index 0, never markable -- see its own tests)
  REQUIRE(app.handleCommand(Command::MoveDown));  // off "../" too, onto a real entry (index 2)
  CHECK_FALSE(app.directoryMode().isMarked(2));
  REQUIRE(app.handleCommand(Command::ToggleMark));
  CHECK(app.directoryMode().isMarked(2));
  REQUIRE(app.handleCommand(Command::ToggleMark));
  CHECK_FALSE(app.directoryMode().isMarked(2));
}

// 2026-09-08、直接のユーザー依頼によるキー再編で^X^Y/^X^Nから^Jy/^Jn
// へ移行(両方とも既存の^O割り当てと衝突するため、揃えて^Jへ)。
TEST_CASE_FIXTURE(Fixture, "feedByte: ^J y marks all entries (K-72), ^J n clears all marks (K-73), "
                           "excluding \"./\"/\"../\"") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  CHECK(app.feedByte('y', std::nullopt));     // y
  CHECK(app.directoryMode().markedCount() == app.directoryMode().entries().size() - 2);
  CHECK_FALSE(app.directoryMode().isMarked(0));  // "./"
  CHECK_FALSE(app.directoryMode().isMarked(1));  // "../"

  REQUIRE(app.feedByte(0x0a, std::nullopt));
  CHECK(app.feedByte('n', std::nullopt));  // n
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

// 2026-08-27 redesign, a direct user report ("ファイル選択後の冗長性
// ...さらに^X^Mとやるのがめんどくさい") + their own proposed fix
// ("真っ新（ls）時は発動しない"): superseding the 2026-08-21 two-step
// arm-then-^X^M design these tests used to cover (^Xc/^Xm/^Xr used to
// ARM unconditionally, showing a "CP"/"MV"/"RM" Standout label and
// waiting for a separate ^X^M) -- with NOTHING marked, ^Xc/^Xm/^Xr are
// now a complete no-op: no arm, no label, nothing on screen changes,
// exactly as if the key had never been pressed. See
// Application::startCopyPrompt()'s own comment.
TEST_CASE_FIXTURE(Fixture, "^Oc/^Om/^Or with nothing marked: a complete no-op, no prompt, nothing armed") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::MoveDown));  // off "./" and "../" onto a real entry, but never marked
  REQUIRE(app.handleCommand(Command::MoveDown));
  std::string beforeRow = display.rowText(23);

  app.feedByte(0x0f, std::nullopt);
  CHECK_FALSE(app.feedByte('c', std::nullopt));
  CHECK_FALSE(app.promptActive());
  CHECK(display.rowText(23) == beforeRow);  // nothing changed on screen at all

  CHECK_FALSE(app.handleCommand(Command::StartMove));
  CHECK_FALSE(app.promptActive());
  CHECK_FALSE(app.handleCommand(Command::StartRemove));
  CHECK_FALSE(app.promptActive());
}

// 2026-08-27 redesign: marking now doubles as the activation gate --
// ANY marked entry makes ^Xc arm AND immediately show the destination
// prompt in the SAME keystroke, no separate ^X^M needed anymore (the
// "CP" Standout label is now part of the already-active prompt row
// itself, see startDestinationPrompt()'s own drawPromptLabel() call,
// rather than a separate, waiting-for-^X^M display).
TEST_CASE_FIXTURE(Fixture, "^Oc with one entry marked: shows the destination prompt immediately, no separate "
                           "confirmation step needed, and ^G cancels back to plain Directory mode unchanged") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  std::string beforeRow = display.rowText(23);

  REQUIRE(app.handleCommand(Command::ToggleMark));
  CHECK_FALSE(app.promptActive());  // marking alone doesn't show anything yet

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte('c', std::nullopt));   // c -- ^Oc: arms AND shows the prompt in one step
  CHECK(app.promptActive());
  CHECK(display.rowText(23).substr(0, 2) == "CP");  // "CP" is part of the now-active prompt row itself
  CHECK(display.standoutAt(23, 0));

  CHECK(app.feedByte(0x07, std::nullopt));  // ^G -- cancel back out (feedPromptByte()'s own path, not Command::Back)
  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == path());  // didn't navigate anywhere
  CHECK(display.rowText(23) == beforeRow);
}

// 2026-08-27 fix, a real report ("残留ハイライトを消去かつ全アンダー
// ラインを全ハイライトにしてみて（何もせずに^Gで全アンダーライン復
// 活）"): while the destination prompt is active, the cursor's own
// resting row must show NO Standout (unlike ordinary browsing), and a
// GENUINELY marked row must show Standout instead of its ordinary
// Underline -- the legacy-accurate convention startCopyPrompt()/
// startMovePrompt()/startRemovePrompt() restore via
// setCursorRowAlsoHighlighted(false)/setMarkedRowUsesUnderline(false),
// with both flipped back to true once the prompt is dismissed (either
// by cancelling, checked here, or by a successful completion, which
// enterDirectory() itself restores -- see its own comment).
TEST_CASE_FIXTURE(Fixture, "^Oc: while the destination prompt is active, marked rows show Standout (not "
                           "Underline) and the cursor's own row shows no Standout; ^G restores the ordinary "
                           "scheme") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::MoveDown));  // step onto the next entry, so '[' has somewhere to retreat to
  int markIdx = indexOf(app.directoryMode().selectedEntry()->name);
  REQUIRE(markIdx == idx + 1);

  CHECK(app.feedByte('[', std::nullopt));  // marks markIdx, cursor retreats back onto idx
  REQUIRE(app.directoryMode().isMarked(markIdx));
  REQUIRE(indexOf(app.directoryMode().selectedEntry()->name) == idx);

  int markNameCol = app.directoryMode().entries()[markIdx].nameStartColumn;
  int cursorNameCol = app.directoryMode().entries()[idx].nameStartColumn;
  // Before ^Oc: the ordinary browsing scheme (established precedent).
  CHECK(display.underlineAt(markIdx, markNameCol));
  CHECK(display.standoutAt(idx, cursorNameCol));

  REQUIRE(app.handleCommand(Command::StartCopy));
  CHECK(app.promptActive());
  CHECK(display.standoutAt(markIdx, markNameCol));       // marked row: Standout now, not Underline
  CHECK_FALSE(display.underlineAt(markIdx, markNameCol));
  CHECK_FALSE(display.standoutAt(idx, cursorNameCol));   // cursor's own row: no highlight at all while the prompt is active

  CHECK(app.feedByte(0x07, std::nullopt));  // ^G cancels the prompt
  // 2026-08-27 (same day) reversal, a direct user report: marks now
  // SURVIVE a cancelled CP/MV/RM (see cancelPrompt()'s own comment) --
  // only the display flags flip back, markedIndices_ is untouched.
  CHECK(app.directoryMode().isMarked(markIdx));
  // The ordinary scheme (Underline for the still-marked row, Standout
  // for the cursor's own row) is back.
  CHECK(display.underlineAt(markIdx, markNameCol));
  CHECK_FALSE(display.standoutAt(markIdx, markNameCol));
  CHECK(display.standoutAt(idx, cursorNameCol));
}

// Same one-step activation for MV/RM -- identical gate to CP's, see
// startMovePrompt()/startRemovePrompt()'s own comments.
TEST_CASE_FIXTURE(Fixture, "^Om/^Or with one entry marked: also show their own prompt immediately") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  REQUIRE(app.handleCommand(Command::ToggleMark));
  REQUIRE(app.handleCommand(Command::StartMove));
  CHECK(app.promptActive());
  CHECK(app.feedByte(0x07, std::nullopt));  // ^G cancel
  CHECK_FALSE(app.promptActive());
  CHECK(app.directoryMode().isMarked(idx));  // cancelling leaves it marked -- see the next test's own comment

  REQUIRE(app.handleCommand(Command::StartRemove));  // the SAME still-standing mark, not re-marked
  CHECK(app.promptActive());
  CHECK(display.rowText(23).find("Delete ? ") != std::string::npos);
}

// 2026-08-24 fix: cancelling an ARMED CP/MV/RM used to clear any marks
// made while armed too -- cancelPendingCpMvRm() called
// DirectoryMode::refresh() (a real `ls` re-run) specifically for this
// ("^Gのあとls再実行でいいんじゃない？"). The 2026-08-27 one-step
// redesign (see startCopyPrompt()'s own comment) broke this in a
// different way twice over: first by simply losing the restore
// (pendingCpMvRm_ is consumed atomically now, so cancelPendingCpMvRm()
// itself is unreachable), then -- after a same-day fix routed
// cancelPrompt() to call directoryMode_.refresh() directly, restoring
// the 2026-08-24 mark-CLEARING behavior -- a direct user report caught
// this as a NEW regression from THEIR perspective: "注意深く選んだ
// マークがうっかり^Gでパーみたいな切ない事態を避けるため。意図的には
// ^X^Nで全クリア可能なのでマークは残して欲しい". The 2026-08-24
// rationale doesn't actually transfer to this one-step design -- that
// fix was about a STALE ARM's marks lingering after navigating away
// from a separate, now-eliminated "waiting" sub-mode; here there's no
// such in-between state to leave stale, so cancelPrompt() now restores
// only the two display flags, leaving markedIndices_ untouched -- ^X^N
// (ClearAllMarks) is the one deliberate way to actually clear marks.
TEST_CASE_FIXTURE(Fixture, "^G-cancelling the CP prompt leaves marks made before arming intact, matching "
                           "the pre-redesign arm's own ^G behavior for STALE arms only -- not a live cancel") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));
  REQUIRE(app.directoryMode().isMarked(idx));

  REQUIRE(app.handleCommand(Command::StartCopy));  // mark FIRST, then arm -- shows the prompt immediately
  REQUIRE(app.feedByte(0x07, std::nullopt));       // ^G cancels the prompt

  CHECK(app.directoryMode().isMarked(idx));  // still marked -- see comment above
}

// 2026-08-21 addition, RELOCATED 2026-08-23 from '.'/',' to '['/']'
// (see Command::MarkAndAdvance's own comment on why -- Space took over
// the marking job in armed CP/MV/RM for a while, then handed it back
// here once Space turned out to cause more confusion than it saved):
// '['/']' as direct "mark, then move" macros -- a real user report +
// demonstrated reference (=ed-=eh): "コピーしたいディレクトリを選択
// しようにも[return]でそこへ移動してしまってはどうにもならない...
// まずはマクロとしての「,」「.」をサポートして欲しい". See
// Command::MarkAndAdvance's own comment for why this is a direct
// toggleMark()+move binding rather than a replay of legacy's own
// literal Enter-based M-4/M-5 macro.
TEST_CASE_FIXTURE(Fixture, "']': marks the current entry, then moves the cursor down") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  CHECK(app.feedByte(']', std::nullopt));
  CHECK(app.directoryMode().isMarked(idx));
  CHECK(app.directoryMode().selectedEntry()->name != "cpmv_a.txt");  // cursor advanced
}

TEST_CASE_FIXTURE(Fixture, "'[': marks the current entry, then moves the cursor up") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::MoveDown));  // step onto the NEXT entry, so '[' has somewhere to retreat to
  REQUIRE(indexOf(app.directoryMode().selectedEntry()->name) == idx + 1);

  CHECK(app.feedByte('[', std::nullopt));
  CHECK(app.directoryMode().isMarked(idx + 1));      // the entry '[' was pressed on got marked
  CHECK(indexOf(app.directoryMode().selectedEntry()->name) == idx);  // cursor retreated back onto cpmv_a.txt
}

// 2026-08-25, a direct user report: repeatedly pressing '[' to mark a
// run of entries leaves the cursor resting ONE ROW PAST the last entry
// actually marked (MarkAndRetreat marks, THEN moves) -- and since the
// plain (not CP/MV/RM-armed) ls-listing always shows the cursor row
// with Standout regardless of marking, that resting row used to LOOK
// marked (same reverse-video box) even though it is NOT in
// targetEntries() and would NOT be passed to CP/MV/RM/PROGRAM/Quick
// Look. Fixed by giving genuinely marked rows their own Underline
// channel (ContentView::setMarkedRowUsesUnderline(), enabled for this
// plain-browsing view specifically -- see DirectoryMode's own
// constructor comment) instead of folding them into the cursor's own
// Standout: the two are now simultaneously visible and distinguishable.
TEST_CASE_FIXTURE(Fixture, "'[' marking: the row the cursor ends up resting on (never actually marked) "
                           "shows Standout but NOT Underline, while the marked row(s) show Underline but "
                           "NOT Standout") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::MoveDown));  // step onto the NEXT entry, so '[' has somewhere to retreat to
  int markIdx = indexOf(app.directoryMode().selectedEntry()->name);
  REQUIRE(markIdx == idx + 1);

  CHECK(app.feedByte('[', std::nullopt));  // marks markIdx, cursor retreats back onto idx
  REQUIRE(app.directoryMode().isMarked(markIdx));
  REQUIRE_FALSE(app.directoryMode().isMarked(idx));
  REQUIRE(indexOf(app.directoryMode().selectedEntry()->name) == idx);

  int markNameCol = app.directoryMode().entries()[markIdx].nameStartColumn;
  int cursorNameCol = app.directoryMode().entries()[idx].nameStartColumn;

  CHECK(display.underlineAt(markIdx, markNameCol));       // genuinely marked -- Underline
  CHECK_FALSE(display.standoutAt(markIdx, markNameCol));  // NOT Standout (not the cursor row)

  CHECK(display.standoutAt(idx, cursorNameCol));         // cursor's own ordinary highlight -- Standout
  CHECK_FALSE(display.underlineAt(idx, cursorNameCol));  // NOT actually marked -- no Underline
}

// 2026-08-23: '.' itself (RELOCATED from ToggleMark's own ^Space) --
// a direct user request once Space stopped doing the marking job (see
// Command::QuickLook's own comment): "代わりに'.'を選択/解除のトグル
// （行の移動は無し）としたい". Unlike '['/']' above, '.' never moves
// the cursor -- pressing it twice on the SAME row (no MoveUp/MoveDown
// needed in between, unlike the old mark-then-advance macros) toggles
// the mark back off.
TEST_CASE_FIXTURE(Fixture, "'.': toggles the current entry's mark, with NO cursor movement") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  CHECK(app.feedByte('.', std::nullopt));
  CHECK(app.directoryMode().isMarked(idx));
  CHECK(app.directoryMode().selectedEntry()->name == "cpmv_a.txt");  // cursor did NOT move
  CHECK(app.feedByte('.', std::nullopt));
  CHECK_FALSE(app.directoryMode().isMarked(idx));  // toggled back off, still no movement
  CHECK(app.directoryMode().selectedEntry()->name == "cpmv_a.txt");
}

// '.'/'['/']' must be inert outside Directory/Archive (toggleMark()'s
// own mode gate) -- e.g. typing a literal '.' while viewing a file's
// content must not silently do nothing DIFFERENT from before this
// feature existed (still a no-op, just routed through a new Command).
TEST_CASE_FIXTURE(Fixture, "'.' is a no-op outside Directory/Archive mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK_FALSE(app.feedByte('.', std::nullopt));
}

// 2026-08-21 regression, a real report ("なんも変わってないんだが") +
// reference screenshot from an actual pre-refactor binary run (=ef):
// drawPromptLabel() never actually drew this message at all for CP/MV/
// RM (unlike ExitConfirm's own "Exit (y/n) ? ", which it always has) --
// ^X^M's destination/confirm prompt showed only the reversed "CP"/"MV"/
// "RM" label jumping straight to the right-aligned source-path field,
// easy to misread as "nothing happened" since there was no legible
// prompt text at all.
TEST_CASE_FIXTURE(Fixture, "CP's destination prompt shows plain (non-Standout) \"Destination ? \" text "
                           "after the reversed \"CP\"/position fields, matching legacy") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));
  REQUIRE(app.handleCommand(Command::StartCopy));  // shows the destination prompt immediately (2026-08-27 redesign)

  std::string row = display.rowText(23);
  std::size_t pos = row.find("Destination ? ");
  REQUIRE(pos != std::string::npos);
  CHECK_FALSE(display.standoutAt(23, static_cast<int>(pos)));  // plain, not reversed -- see drawPromptLabel()'s own comment
  CHECK(display.standoutAt(23, 0));                            // "CP" itself is still reversed
}

// Same fix, RM's own "Delete ? " wording (legacy key.c's T_rm branch).
TEST_CASE_FIXTURE(Fixture, "RM's confirm prompt shows \"Delete ? \" for RM, not \"Destination ? \"") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));
  REQUIRE(app.handleCommand(Command::StartRemove));  // shows the confirm prompt immediately (2026-08-27 redesign)

  CHECK(display.rowText(23).find("Delete ? ") != std::string::npos);
  CHECK(display.rowText(23).find("Destination ? ") == std::string::npos);
}

// docs/HISTORY.md's own real report continued: marking (^Space) itself
// had zero visual feedback either -- toggleMark() only ever mutated a
// std::set the render path never consulted. Marked rows render
// something visible independent of cursor position (see ContentView::
// setMarkedPredicate()'s own comment). UPDATED 2026-08-25 (see
// ContentView::setMarkedRowUsesUnderline()'s own comment): the plain
// (not CP/MV/RM-armed) ls-listing view now renders a genuinely marked
// row Underline rather than Standout, so it stays visually distinct
// from the cursor row's own ordinary Standout; FURTHER updated the
// same day (a direct follow-up report: Standout+Underline combined on
// the SAME row is hard to read, the reverse-video background makes the
// underline itself hard to see) so that marking takes priority
// outright on a row that's both -- Underline only, never both
// attributes on the same row.
TEST_CASE_FIXTURE(Fixture, "^Space marks a row Underline even when the cursor later moves off it, and "
                           "marking wins outright over Standout on the cursor's own row") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  // DirectoryMode's own highlightStartColumn (permissions/date prefix
  // before the filename, see its own comment) means Standout/Underline
  // start at the filename, not column 0 -- same convention the cursor
  // row's own highlight already follows.
  int nameCol = app.directoryMode().entries()[idx].nameStartColumn;
  REQUIRE(app.handleCommand(Command::ToggleMark));
  CHECK(display.underlineAt(idx, nameCol));       // marked -- Underline
  CHECK_FALSE(display.standoutAt(idx, nameCol));  // marking wins outright: no Standout even on the cursor row

  app.handleCommand(Command::MoveDown);
  CHECK(app.directoryMode().selectedEntry()->name != "cpmv_a.txt");  // cursor moved off it
  CHECK(display.underlineAt(idx, nameCol));       // still Underline -- marked, not just "was the cursor row"
  CHECK_FALSE(display.standoutAt(idx, nameCol));  // no longer the cursor row, so no Standout anymore
}

// 2026-09-08 note: the test that used to live here ("^X ^M with nothing
// armed is a no-op") verified Command::StartDestinationPrompt's own
// no-op guard when nothing was armed. That command's independent key
// binding (^X^M, K-71) was deleted in the same keybinding redesign that
// moved CP/MV/RM/ARC's own triggers to ^O (a direct user request,
// "冗長分は全て削除で") -- there is no longer a key that reaches it, so
// the test's premise is gone.

// 2026-08-27 note: the "arm in one directory, navigate away, THEN press
// ^X^M" scenario this test used to cover (a stale arm from before
// navigating) is no longer reachable via any single keystroke sequence
// under the one-step redesign -- pendingCpMvRm_ is now always armed AND
// consumed atomically within the same StartCopy/StartMove/StartRemove
// call (see Application::startCopyPrompt()'s own comment), with no
// window in between where the user could navigate away first. The
// underlying defensive check in startDestinationPrompt() (`pending.
// directory != directoryMode_.currentDirectory()`) still exists but is
// now unreachable through normal use, so this test was removed rather
// than adapted.

TEST_CASE_FIXTURE(Fixture, "handleCommand(StartCopy): refused outside Directory mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK_FALSE(app.handleCommand(Command::StartCopy));
  CHECK_FALSE(app.promptActive());
}

// 2026-08-23 addition, a direct user request: "旧版で多用していた
// キーアサイン" (a legacy key binding used constantly). Ported from
// key.c's read_m(): `case K_w: if (f_M==T_cp||f_M==T_mv||f_M==T_arc||
// f_M==T_arc3) { ...insert `old`... }` -- legacy's `lp->old` (menu.c
// sets it to `lp->dir` right before every directory change) is exactly
// what this port's RecentDirectory ring already tracks: the SECOND-
// TO-LAST entry, once the destination prompt is open, is whichever
// directory was current immediately before the one CP was armed from.
TEST_CASE_FIXTURE(Fixture, "^W (CopyDestination prompt): inserts the previous directory") {
  REQUIRE(app.start(path()));
  // Navigate into subdir/, then back -- RecentDirectory ring now ends
  // [..., subdir/, path()], matching legacy's lp->old/lp->dir pair.
  int idx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.directoryMode().currentDirectory() == path() + "subdir/");
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.directoryMode().currentDirectory() == path());

  REQUIRE(app.handleCommand(Command::ToggleMark));  // mark whatever the cursor rests on -- irrelevant to this test
  REQUIRE(app.handleCommand(Command::StartCopy));   // shows the destination prompt immediately (2026-08-27 redesign)
  feedText("garbage-to-be-cleared");

  CHECK(app.feedByte(0x17, std::nullopt));  // ^W
  CHECK(app.promptText() == path() + "subdir/");
}

TEST_CASE_FIXTURE(Fixture, "^W: a no-op (beeps, doesn't insert anything) outside CP/MV/ARC destination prompts") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartEgrep));
  feedText("pattern");
  CHECK(app.feedByte(0x17, std::nullopt));  // swallowed like any other unrecognized control byte, not inserted
  CHECK(app.promptText() == "pattern");
}

// 2026-08-23 addition, a direct user request: "'/Users/h'と打った
// ところで^I（tab）を打っても何も起きない。基本的にディレクトリ
// および ファイル名入力途中の^Iはマッチする対象があれば補完して
// 欲しい" -- CopyDestination is exactly as much "a path in full" as
// PathInput already was (see the ^I handling's own updated comment);
// MoveDestination/ArchiveDestination/DuPath share the identical `if`
// condition and completeFilesystemPath() call, so this one case is
// representative of all four.
TEST_CASE_FIXTURE(Fixture, "^I (CopyDestination prompt): completes a partial filesystem path") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));
  REQUIRE(app.handleCommand(Command::StartCopy));  // shows the destination prompt immediately (2026-08-27 redesign)
  feedText(path() + "subd");  // "subd" uniquely prefixes "subdir/" among root's own children

  CHECK(app.feedByte(0x09, std::nullopt));  // ^I
  CHECK(app.promptText() == path() + "subdir/");
}

// 2026-08-23 fix, a direct user report: "FGREPのモードラインで戻り値
// の位置が(fgrep)と逆". trailing2/trailing3 were swapped relative to
// every other mode sharing this exact "program badge + return-
// destination" shape (SHELL's own item-401 fix: trailing2="(zsh)"
// badge, trailing3=return-target badge, always right-edge-padded --
// see grepView_'s own updated provider comment).
TEST_CASE_FIXTURE(Fixture, "FGREP results: mode line shows the program badge \"(grep)\" right after the "
                           "byte count, and the return-directory path LAST") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);

  std::string row = display.rowText(23);
  // 2026-09-05変更(直接のユーザー依頼、"fgrepとegrepの代わりにgrep -F
  // とgrep -Eで代用"): バッジは実際に実行されるプログラム(cmd.argv.front())
  // をそのまま表示するので、FGREP/EGREP/GREPいずれも同じ"(grep)"になる
  // ――configuration.cpp自身のコメント参照。
  std::size_t badgePos = row.find("(grep)");
  std::size_t pathPos = row.find(path());
  REQUIRE(badgePos != std::string::npos);
  REQUIRE(pathPos != std::string::npos);
  CHECK(badgePos < pathPos);  // badge comes BEFORE the return-destination path, not after
  CHECK(row.substr(pathPos + path().size()).find_first_not_of(' ') == std::string::npos);  // right-edge padded
}

// 2026-08-27 update: marking now doubles as CP's own activation gate
// (see Application::startCopyPrompt()'s own comment), so both files
// must be marked BEFORE ^Xc now -- arming with nothing marked yet is a
// no-op under the redesign, unlike the pre-2026-08-27 "arm first,
// mark after" order this test used to exercise.
TEST_CASE_FIXTURE(Fixture, "CP: marking two files then copying them creates real copies at the destination") {
  REQUIRE(app.start(path()));

  int idxA = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idxA) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  int idxB = indexOf("cpmv_b.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idxB) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartCopy));  // shows the destination prompt immediately
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

  CHECK(fs::exists(root / "cpmvdest" / "cpmv_a.txt"));
  CHECK(fs::exists(root / "cpmvdest" / "cpmv_b.txt"));
  // Sources are untouched by a copy.
  CHECK(fs::exists(root / "cpmv_a.txt"));
  CHECK(fs::exists(root / "cpmv_b.txt"));
  // 2026-09-09 update: the reload clears the PRE-operation selection
  // (markedIndices_, DirectoryMode::enterDirectory()'s own documented
  // behavior) -- but markedCount()/targetEntries() now ALSO count the
  // post-transfer marking (transientMarkedNames_) set right after this
  // same reload, per the direct user request to let PROGRAM/CP/MV/RM
  // treat the just-copied files as a real selection too (see
  // DirectoryMode::isMarked()'s own 2026-09-09 comment). So the
  // combined count here is the 2 just-copied files, not 0.
  CHECK(app.directoryMode().markedCount() == 2);
  std::vector<fm::DirectoryEntry> targets = app.directoryMode().targetEntries();
  REQUIRE(targets.size() == 2);
  CHECK((targets[0].name == "cpmv_a.txt" || targets[0].name == "cpmv_b.txt"));
  CHECK((targets[1].name == "cpmv_a.txt" || targets[1].name == "cpmv_b.txt"));
}

// 2026-08-21 regression, a real report + screenshot (=eh): `ls -F`'s
// own trailing slash on a directory entry's name used to survive
// straight into collectTargetPaths()'s own source path, and cp/rsync-
// family tools treat "dir/" as "dir's CONTENTS", not "dir itself", as
// a recursive copy source -- a marked directory's contents were being
// dumped straight into the destination instead of creating a proper
// "dest/dirname/" copy (visible in cp -v's own output as a literal
// "dirname//nested/path" double-slash artifact too, the SAME untrimmed
// trailing slash showing up a second way).
TEST_CASE_FIXTURE(Fixture, "CP: copying a marked DIRECTORY creates the directory itself at the destination, "
                           "not just its contents") {
  REQUIRE(app.start(path()));
  int idx = indexOf("nested_dir/");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

  CHECK(fs::exists(root / "cpmvdest" / "nested_dir" / "nested.txt"));  // the directory itself, not dumped flat
  CHECK_FALSE(fs::exists(root / "cpmvdest" / "nested.txt"));         // the old (buggy) flattened location
  CHECK(fs::exists(root / "nested_dir" / "nested.txt"));               // source untouched by a copy
}

// 2026-08-21 addition, the same real report's own explicit expectation
// ("最後には/tmpの(ls)になる"): a successful CP/MV now lands the view in
// the DESTINATION directory (matching legacy's own command()/M_shell
// relaunch mechanism), not the unchanged source.
TEST_CASE_FIXTURE(Fixture, "CP: a successful copy navigates the view to the destination directory") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

  CHECK(app.directoryMode().currentDirectory() == (root / "cpmvdest").string() + "/");
}

// 2026-09-08 addition, a direct user request ("CP/MV/ARCモードで複写/
// 移動/展開先ディレクトリへ移動したあと、対象ファイルおよびディレク
// トリを^[Pへも渡せるようアンダーライン表示して欲しい"): the just-
// copied entry shows Underline in the destination listing, exactly
// like the existing pre-operation marking (ARC's own "shows Underline"
// test above), driven by the same setMarkedRowUsesUnderline(true)
// rendering path via DirectoryMode::isMarked()'s new OR against
// transientMarkedNames_.
TEST_CASE_FIXTURE(Fixture, "CP: after landing in the destination directory, the copied file is shown "
                           "Underlined (post-transfer marking, ^[P target)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

  int destIdx = indexOf("cpmv_a.txt");
  REQUIRE(destIdx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != destIdx) app.handleCommand(Command::MoveDown);
  int nameCol = app.activeContentView().lines()[destIdx].highlightStartColumn.value_or(0);
  CHECK(display.underlineAt(destIdx, nameCol));
}

// 2026-09-09 addition, a direct user report ("見た目だけで実際は選択済み
// 状態になっていないらしくPROGRAMもCP/MV/RMも反応しない...実際の選択
// としても使いたい"): the "統合する" decision (AskUserQuestion) means a
// FOLLOW-UP CP/MV/RM can now act on the post-transfer marking directly,
// with no need to manually re-mark the just-copied file first.
TEST_CASE_FIXTURE(Fixture, "CP: a follow-up CP acts on the post-transfer marking directly, with no manual "
                           "re-marking needed") {
  fs::create_directory(root / "cpmvdest2");
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));
  REQUIRE(app.directoryMode().currentDirectory() == (root / "cpmvdest").string() + "/");

  // A second CP, straight away, with nothing manually re-marked --
  // only the post-transfer marking from the FIRST copy is active.
  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText((root / "cpmvdest2").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

  CHECK(fs::exists(root / "cpmvdest2" / "cpmv_a.txt"));
}

// 2026-08-27 fix, a real report + repro ("'saved'[return]→「saved/
// （ls）」にならない（~/usilic/（ls）のままかつカーソル位置が
// 「usilic-h.h」でなく最上行）"): a RELATIVE destination (just "cpmvdest",
// not an absolute path) used to fail to land the view there at all --
// the shell's own `cp` succeeded (its real session cwd tracks the
// source directory), but pollPendingCpMvRmShellRun()'s own
// descendTo(run.destination) handed the literal relative text straight
// to enterDirectory(), which runs `ls` via ChildProcess::run() against
// the USILIC PROCESS's cwd, not the shell's -- almost always failing,
// silently falling back to re-listing the source directory. Now
// resolves a relative destination against sourceDirectory first.
TEST_CASE_FIXTURE(Fixture, "CP: a RELATIVE destination (not just an absolute one) also navigates the view "
                           "there, resolved against the source directory") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText("cpmvdest");  // relative -- cpmvdest already exists as a subdirectory of root, see Fixture's own setup
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

  CHECK(app.directoryMode().currentDirectory() == (root / "cpmvdest").string() + "/");
  CHECK(fs::exists(root / "cpmvdest" / "cpmv_a.txt"));
}

// 2026-08-23 fix, a real report: "CPで/tmpへのコピーを終えた後の
// （ls）では^Gで/tmpから変わらない" -- landing in the destination used
// to call DirectoryMode::enterDirectory() directly, which (by design)
// never touches ModeStack, so ^G had no frame to pop back to the
// SOURCE directory with. Now goes through descendTo() instead (same
// history-pushing path ordinary Enter-into-subdirectory navigation
// uses), so ^G afterward works exactly like any other descent.
TEST_CASE_FIXTURE(Fixture, "CP: after landing in the destination directory, ^G goes back to the source "
                           "directory (2026-08-23 fix)") {
  REQUIRE(app.start(path()));
  std::string sourceDir = path();
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));
  REQUIRE(app.directoryMode().currentDirectory() == (root / "cpmvdest").string() + "/");

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.directoryMode().currentDirectory() == sourceDir);
}

// 2026-08-22 regression, a real report + screenshot (=en): copying to a
// destination typed WITHOUT a trailing slash (e.g. "/tmp", exactly as
// a user would naturally type it) used to pass that raw string straight
// into `ls`'s own argv. BSD `ls` (macOS) treats a symlink-to-directory
// argument without a trailing slash as "describe the symlink itself"
// (matching `ls -d`), not "list its contents" -- `/tmp` itself is
// exactly such a symlink on macOS, so landing there after a copy showed
// only `/tmp`'s own one-line symlink entry ("1/1 END"), not its real
// contents. Reproduced here with a fixture-local symlink so this test
// doesn't depend on `/tmp` itself being a symlink on whatever machine
// runs it.
TEST_CASE_FIXTURE(Fixture, "CP: a destination typed without a trailing slash still lists the real "
                           "directory contents, even when it's a symlink") {
  fs::create_directory(root / "realdest");
  { std::ofstream(root / "realdest" / "inside.txt") << "content\n"; }
  fs::create_directory_symlink(root / "realdest", root / "linkdest");

  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText((root / "linkdest").string());  // no trailing slash -- exactly how a user would type it
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

  CHECK(indexOf("inside.txt") >= 0);       // the symlink's TARGET contents, not just the symlink's own line
  CHECK(indexOf("cpmv_a.txt") >= 0);       // the freshly-copied file too
}

// A copy that fails outright (bogus command, see Fixture's own PROGRAM
// override note near the top) must NOT attempt to navigate anywhere --
// the view should reflect the unchanged source, exactly as before this
// feature existed.
TEST_CASE_FIXTURE(Fixture, "CP: a failed copy leaves the view on the (unchanged) source directory") {
  REQUIRE(app.start(path()));
  configuration.loadRcText("CP\n false\n", true);  // always fails, regardless of arguments
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText((root / "cpmvdest").string());
  // Submitting only injects the command and switches to Shell mode --
  // it doesn't know cp/"false" will fail until the shell actually runs
  // it, so this returns true (successfully injected) even though the
  // command itself is doomed; pumpCpMvRmToCompletion() below waits for
  // the actual (failing) result via the exit-code marker.
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

  CHECK(app.directoryMode().currentDirectory() == path());
}

TEST_CASE_FIXTURE(Fixture, "MV: moving a marked file removes the source and creates the destination") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartMove));
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

  CHECK(fs::exists(root / "cpmvdest" / "cpmv_a.txt"));
  CHECK_FALSE(fs::exists(root / "cpmv_a.txt"));
}

// CP自身の同種テストと対の関係(2026-09-08追加、直接のユーザー要望)。
TEST_CASE_FIXTURE(Fixture, "MV: after landing in the destination directory, the moved file is shown "
                           "Underlined (post-transfer marking, ^[P target)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartMove));
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

  int destIdx = indexOf("cpmv_a.txt");
  REQUIRE(destIdx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != destIdx) app.handleCommand(Command::MoveDown);
  int nameCol = app.activeContentView().lines()[destIdx].highlightStartColumn.value_or(0);
  CHECK(display.underlineAt(destIdx, nameCol));
}

// 2026-09-08 addition: Command::Redraw (^L) clears the post-transfer
// marking (DirectoryMode::clearTransientMarks(), directory_mode.hpp's
// own comment on why this is a SEPARATE set from markedIndices_), but
// a markedIndices_ selection made AFTER landing (for a follow-up CP/MV)
// must survive it -- the two are independent by design.
TEST_CASE_FIXTURE(Fixture, "Command::Redraw (^L) clears the post-transfer Underline marking, but leaves an "
                           "unrelated markedIndices_ selection intact") {
  // A pre-existing file INSIDE the destination, unrelated to this CP --
  // cpmv_b.txt (the fixture's other source file) is never copied here,
  // so it can't be used as the "fresh, unrelated selection" target.
  { std::ofstream(root / "cpmvdest" / "unrelated.txt") << "x\n"; }
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

  int transientIdx = indexOf("cpmv_a.txt");
  REQUIRE(transientIdx >= 0);
  int otherIdx = indexOf("unrelated.txt");
  REQUIRE(otherIdx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != otherIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));  // a fresh selection for a follow-up CP/MV
  // 2026-09-09 update: markedCount() now counts BOTH the post-transfer
  // marking (cpmv_a.txt, still not cleared) and this fresh markedIndices_
  // selection (unrelated.txt) -- see DirectoryMode::isMarked()'s own
  // 2026-09-09 comment on the "integrate" decision.
  REQUIRE(app.directoryMode().markedCount() == 2);

  REQUIRE(app.handleCommand(Command::Redraw));  // ^L

  // MoveDown() only moves forward -- the cursor is currently at
  // otherIdx (after "cpmv_a.txt" alphabetically), so it must go back
  // to the top before searching forward for transientIdx again.
  REQUIRE(app.handleCommand(Command::GoToTop));
  while (indexOf(app.directoryMode().selectedEntry()->name) != transientIdx) app.handleCommand(Command::MoveDown);
  int transientNameCol = app.activeContentView().lines()[transientIdx].highlightStartColumn.value_or(0);
  CHECK_FALSE(display.underlineAt(transientIdx, transientNameCol));  // post-transfer marking is gone

  CHECK(app.directoryMode().markedCount() == 1);  // the fresh CP/MV-prep selection is untouched
  while (indexOf(app.directoryMode().selectedEntry()->name) != otherIdx) app.handleCommand(Command::MoveDown);
  int otherNameCol = app.activeContentView().lines()[otherIdx].highlightStartColumn.value_or(0);
  CHECK(display.underlineAt(otherIdx, otherNameCol));  // still shown Underlined, via markedIndices_ this time
}

// 2026-09-08 addition: ^[P (Command::PasteTransientTargets) pastes the
// post-transfer marking's full path(s), shell-quoted, into an active
// prompt -- requirements.md's "設計判断A" decision (full path over
// bare name, since a bare name becomes meaningless once you leave the
// destination directory).
TEST_CASE_FIXTURE(Fixture, "^[P pastes the post-transfer marking's full path, shell-quoted, into an active "
                           "prompt") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

  // FGREP (not PROGRAM) deliberately: startProgramPrompt() now ALSO
  // auto-seeds from targetEntries() (2026-09-09, the same "integrate"
  // change this test's own marking relies on) -- that seeding would
  // otherwise leave stray text in the prompt this test isn't testing.
  // startGrepPrompt() has no such seeding (activatePrompt(...,"")).
  REQUIRE(app.handleCommand(Command::StartFgrep));
  REQUIRE(app.promptActive());

  REQUIRE(app.feedByte(0x1b, std::nullopt));  // ESC
  REQUIRE(app.feedByte('P', std::nullopt));

  std::string expectedPath = (root / "cpmvdest" / "cpmv_a.txt").string();
  CHECK(app.promptText() == "'" + expectedPath + "'");
}

// 2026-09-08 addition: no active prompt / no post-transfer marking ->
// no-op, matching Command::PasteClipboard's own established convention
// for "this key doesn't mean anything in the current context".
TEST_CASE_FIXTURE(Fixture, "^[P is a no-op without an active prompt, and a no-op when there's no "
                           "post-transfer marking") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_a.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));
  REQUIRE(app.handleCommand(Command::StartCopy));
  feedText((root / "cpmvdest").string());
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

  // No active prompt yet -- ^[P must do nothing (and not crash).
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  CHECK_FALSE(app.feedByte('P', std::nullopt));

  // ^L clears the post-transfer marking -- ^[P inside a NOW-active
  // prompt must still do nothing, since there's nothing left to paste.
  REQUIRE(app.handleCommand(Command::Redraw));
  REQUIRE(app.handleCommand(Command::StartProgram));
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  CHECK_FALSE(app.feedByte('P', std::nullopt));
  CHECK(app.promptText().empty());
}

TEST_CASE_FIXTURE(Fixture, "RM: confirming with 'y' actually deletes the marked file") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_b.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));

  REQUIRE(app.handleCommand(Command::StartRemove));
  feedText("y");
  CHECK(app.feedByte(0x0d, std::nullopt));
  REQUIRE(pumpCpMvRmToCompletion(app));

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

// docs/HISTORY.md:1840 ("chown実行後のRM、MVではファイルが選べない...
// その旨何らかのメッセージ表示があれば親切"): a permission-denied
// RM/MV target now shows "Permission denied" underlined in the mode
// line's name slot (see Application::checkTargetsWritable()'s own
// comment), reusing the exact presentation docs/HISTORY.md:8912 already
// established for ^Je's "Invalid mode" -- instead of legacy's own
// silent mark-time beep (this port's marking is unconditional/
// operation-agnostic, see DirectoryMode::toggleMark()'s own comment, so
// the check happens at execution time instead). A real `chmod`/`chown`
// isn't reproducible here without root (CpMvRmMode::isWritable() treats
// any file the test process owns as writable regardless of its mode
// bits, and getuid()==0 always passes) -- removing the target out from
// under the operation exercises the exact same isWritable()==false
// codepath (Plain check, target doesn't exist) without needing that.
TEST_CASE_FIXTURE(Fixture, "RM: a target that's disappeared out from under the operation shows \"Permission "
                           "denied\" instead of silently doing nothing") {
  REQUIRE(app.start(path()));
  int idx = indexOf("cpmv_b.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));
  REQUIRE(app.handleCommand(Command::StartRemove));

  fs::remove(root / "cpmv_b.txt");

  feedText("y");
  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));

  std::string expected = "Permission denied";
  CHECK(display.rowText(23).substr(0, expected.size()) == expected);
  CHECK(display.underlineAt(23, 0));
}

// -- M_exit: ^X^C confirmation prompt, wantsExit() flag --

TEST_CASE_FIXTURE(Fixture, "wantsExit(): false before anything happens") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.wantsExit());
}

// 2026-09-08にキー再編で一度^X^Cから^Oxへ移行したが、同日中に直接の
// ユーザー依頼で^X^Cへ差し戻された(kKeyTable自身の"^X^C"エントリの
// コメント参照)。
TEST_CASE_FIXTURE(Fixture, "feedByte: ^X ^C starts the exit-confirmation prompt") {
  REQUIRE(app.start(path()));
  app.feedByte(0x18, std::nullopt);
  CHECK(app.feedByte(0x03, std::nullopt));
  CHECK(app.promptActive());
  CHECK_FALSE(app.wantsExit());
}

// 2026-09-09追加、直接のユーザー依頼("^[^[は-g起動のみ有効みたい
// ...元々^X^Cと同じ扱いなので反映して"): kKeyTable自身の見出し
// コメントは元々"^X^C or ^[^["と謳っていたが、実際には一度もESC ESC
// のdispatchLeaderByte()自身のescPending_switchにcaseが無く、配線
// されないまま放置されていた既知のギャップだった――ここで初めて
// 実際に結線したことを確認する。
TEST_CASE_FIXTURE(Fixture, "feedByte: ESC ESC also starts the exit-confirmation prompt") {
  REQUIRE(app.start(path()));
  app.feedByte(0x1b, std::nullopt);
  CHECK(app.feedByte(0x1b, std::nullopt));
  CHECK(app.promptActive());
  CHECK_FALSE(app.wantsExit());
}

// -- Prompt mode-line labels (activatePrompt()/promptLabelText()) --
// a real user report (2026-08-02) found NO prompt anywhere showed any
// visible indication of what it was asking for; every startXxxPrompt()
// drew an unlabeled blank edit field. --

TEST_CASE_FIXTURE(Fixture, "prompt labels: EXIT shows \"EXIT\" reversed, then the plain \"Exit (y/n) ? \" "
                           "message text (2026-08-05 fix, ported from disp.c's own M_exit case -- a real CUI "
                           "screenshot found this port had previously combined both into one fully-reversed "
                           "\"EXIT (y/n)?\" string, matching neither the wording nor the styling)") {
  REQUIRE(app.start(path()));
  int row = display.rows() - 1;
  REQUIRE(app.handleCommand(Command::StartExit));
  CHECK(display.rowText(row).find("EXIT Exit (y/n) ? ") != std::string::npos);
  CHECK(display.standoutAt(row, 0) == true);   // "EXIT" itself is reversed
  CHECK(display.standoutAt(row, 5) == false);  // "Exit (y/n) ? " is not
}

TEST_CASE_FIXTURE(Fixture, "prompt labels: EXIT also shows the previous mode's name (e.g. the open file) "
                           "as a reversed trailing field filled to the right edge -- ported from disp.c's "
                           "own \"prev\" field (mode-line[6])") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  int row = display.rows() - 1;
  REQUIRE(app.handleCommand(Command::StartExit));
  std::string text = display.rowText(row);
  CHECK(text.find(app.fileMode().currentFile()) != std::string::npos);
  CHECK(display.standoutAt(row, display.columns() - 1) == true);  // filled reversed to the right edge
}

TEST_CASE_FIXTURE(Fixture, "prompt labels: FGREP shows its own name in the (still blank-prompt) bottom "
                           "row; EGREP shows its own name as the candidate list's own badge (2026-08-05: "
                           "EGREP no longer opens a blank prompt at all -- see startGrepPrompt()'s own "
                           "comment)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  CHECK(display.rowText(display.rows() - 1).find("FGREP") != std::string::npos);

  REQUIRE(app.feedByte(0x07, std::nullopt));  // ^G cancel
  REQUIRE(app.handleCommand(Command::StartEgrep));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK(display.rowText(display.rows() - 1).find("EGREP") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "prompt labels: FILE vs DIR shows the right one depending on which key (^Xf/^Xd) "
                           "started the prompt, even though both share one execution path") {
  // Ported from legacy's own M_lfile2/M_ldir2: distinct mode-line
  // labels for ^Xf/^Xd even though startPathInputPrompt()'s own
  // execution is deliberately unified (PathResolver decides file-vs-
  // directory from the typed path itself, not from which key was
  // pressed) -- a real side-by-side screenshot comparison against
  // legacy (2026-08-03) found this port always showing the combined
  // "FILE/DIR" regardless of which key was actually pressed.
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFileInput));
  CHECK(display.rowText(display.rows() - 1).find("FILE") != std::string::npos);

  REQUIRE(app.feedByte(0x07, std::nullopt));  // ^G cancel
  REQUIRE(app.handleCommand(Command::StartDirectoryInput));
  CHECK(display.rowText(display.rows() - 1).find("DIR") != std::string::npos);
}

// 2026-09-09: DU's own destination prompt (and this test with it) was
// removed entirely in the same-day redesign that made ^Jd run
// immediately with no prompt at all -- see runDuOnCurrentFolder()'s
// own comment. DU's mode line is covered separately (see "DU: mode
// line shows 'DU' and a byte count once results are shown").

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

TEST_CASE_FIXTURE(Fixture, "exit confirmation: 'y' is pre-filled, so ^M alone sets wantsExit() "
                           "(2026-08-05 fix: a real CUI screenshot's \"y\" was already there next to "
                           "\"(y/n) ? \" before the user typed anything, not typed fresh each time)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartExit));
  CHECK(app.feedByte(0x0d, std::nullopt));  // Enter alone, nothing typed first

  CHECK(app.wantsExit());
  CHECK_FALSE(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "exit confirmation: backspacing away the pre-filled 'y' and typing 'n' "
                           "still cancels") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartExit));
  CHECK(app.feedByte(0x7f, std::nullopt));  // DEL: removes the pre-filled 'y'
  feedText("n");
  CHECK_FALSE(app.feedByte(0x0d, std::nullopt));

  CHECK_FALSE(app.wantsExit());
  CHECK(app.currentMode() == ApplicationMode::Directory);
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

// -- DU: ^Jd/^J^D, no prompt at all -- see runDuOnCurrentFolder()'s own
// comment for the 2026-09-09 redesign (direct user spec: "DUの仕様と
// しては常時カレントで、ファイル閲覧中なら在籍フォルダーでduを発動
// し、^J^D同様文字列入力エリア無しでよろしく") --

// 2026-09-09 redesign: ^Jd used to open a prompt pre-filled with the
// current directory, requiring ^M to accept before `du` actually ran.
// Per direct user spec (contrasting with LOCATE, which genuinely needs
// a typed query before it can show anything -- DU's results appear
// automatically the moment the tally finishes, so there was never
// really anything to "type"), ^Jd now runs `du` immediately on
// whatever's current, no prompt at all.
TEST_CASE_FIXTURE(Fixture, "handleCommand(StartDu): runs `du` immediately on the current directory, "
                           "no prompt") {
  REQUIRE(app.start(path()));
  CHECK(app.handleCommand(Command::StartDu));
  waitForDu(app);
  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Du);
  bool sawSubdir = false, sawFile = false;
  for (const auto &entry : app.duMode().entries()) {
    if (entry.path.find("subdir") != std::string::npos && entry.isDirectory) sawSubdir = true;
    if (entry.path.find("plain.txt") != std::string::npos && !entry.isDirectory) sawFile = true;
  }
  CHECK(sawSubdir);
  CHECK(sawFile);
}

// Direct user spec: "ファイル閲覧中なら在籍フォルダーでduを発動" --
// unlike before (Directory-mode only), ^Jd now also works from File
// mode, tallying the folder the viewed file actually lives in.
TEST_CASE_FIXTURE(Fixture, "handleCommand(StartDu): from File mode, tallies the file's own containing "
                           "folder") {
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // descend into subdir/
  idx = indexOf("sub.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // open sub.txt as a FILE
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK(app.handleCommand(Command::StartDu));
  waitForDu(app);
  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Du);
  bool sawSubTxt = false;
  for (const auto &entry : app.duMode().entries())
    if (entry.path.find("sub.txt") != std::string::npos) sawSubTxt = true;
  CHECK(sawSubTxt);  // tallied subdir/ (sub.txt's own folder), not path() itself
}

// 2026-09-19追加、MAN/LOCATE自身の同種のテストと対をなすDU版――直接の
// ユーザー方針の転換("MANを使ってみて思うのは、通常ファイルを閲覧中に
// MANを発動した場合、そこへ戻る方が自然な気がする")の対象にDUも
// 含まれる(nearestFileOrDirectoryAnchor()自身のコメント、"通常ファイル
// かディレクトリ"棲み分け)。DUが集計する対象フォルダ(sub.txtの親、
// すぐ上のテストと同じ)と、DU自身の戻り先(sub.txt自身、この
// テストの主眼)は別物であることに注意。
// 2026-09-19六訂正、直接のユーザー報告+実機の正解モードライン
// ("^Jd→戻り値不良。正しくは/Users/haa15030/git/usilic/CLAUDE.md"):
// duView_自身のtrailing3は、2026-09-10/09-14/09-15の3度にわたり
// 「集計対象のルート」を表示する仕様だったが、ユーザー自身がそれを
// 撤回し、manView_/locateView_と同じ「実際の^Gの戻り先」を表示する
// 仕様へ揃えた。
TEST_CASE_FIXTURE(Fixture, "DU: ^G returns to the normal FILE it was invoked from (not the base Directory), "
                           "and the mode line's own trailing3 already reflects that return value") {
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // descend into subdir/
  idx = indexOf("sub.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // open sub.txt as a FILE
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string filePath = app.fileMode().currentFile();

  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);
  CHECK(app.activeContentView().modeLineText().find(filePath) != std::string::npos);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == filePath);
}

// 2026-09-19追加、直接のユーザー依頼("あとはLOCATEと同じく^Gでduを
// 中断し、構築中断時点のDUリストへ移動、をよろしく"): 実際の`du`の
// 代わりに、1行出力→少し待つ→もう1行出力、という偽の`du`スクリプトを
// 使い、最初の行が届いた直後(まだ2行目を待っている、実際にまだ
// awaitingDu()な)タイミングで^Gを送る――ChildProcess::terminate()が
// 子プロセスを実際に止め、finishDuAsync()がその時点までに届いていた
// 分(1行目だけ)でDU一覧を構築することを確認する。
// python3 -c(明示的なsys.stdout.flush()付き)を使う――最初の版は
// /bin/shスクリプト+printfだったが、パイプ(tty以外)へのシェル自身の
// 標準出力がフルバッファリングされ、1行目がsleep中は一切パイプへ
// 出ないままSIGTERMで失われてしまい(entries().size()==0で失敗)、
// 実際に発覚した――実機の`du`自身は別途直接確認済みで、この問題は
// 一切無い(`du -ak /`を実際にパイプ越しに読むと、開始から数ミリ秒で
// 複数行が届く――本物のfeature自体は無関係、この偽スクリプトだけの
// 問題)。
TEST_CASE_FIXTURE(Fixture, "DU: ^G interrupts a still-running `du` and lands on Du mode with only the "
                           "entries that had arrived so far (not the full, eventually-complete listing)") {
  fs::path firstEntry = root / "interrupted_du_first.txt";
  { std::ofstream(firstEntry) << "x"; }
  fs::path secondEntry = root / "interrupted_du_second.txt";
  std::string pythonScript = "import sys, time\n"
                              "print('4\\t" + firstEntry.string() + "')\n"
                              "sys.stdout.flush()\n"
                              "time.sleep(5)\n"
                              "print('8\\t" + secondEntry.string() + "')\n";
  configuration.setCommandForTest(ConfiguredRole::Du, {{"python3", "-c", pythonScript}, std::nullopt});

  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  REQUIRE(app.awaitingDu());

  // Poll a handful of times with short sleeps -- plenty of time for the
  // first printf's line (written immediately, no delay) to arrive
  // through the pipe, but nowhere near the fake script's 5s sleep.
  for (int i = 0; i < 10; i++) {
    app.pollDuCompletion();
    usleep(20000);
  }
  REQUIRE(app.awaitingDu());  // the 5s sleep means it's genuinely still running

  REQUIRE(app.feedByte(0x07, std::nullopt));  // ^G
  CHECK_FALSE(app.awaitingDu());
  CHECK(app.currentMode() == ApplicationMode::Du);
  REQUIRE(app.duMode().entries().size() == 1);
  CHECK(app.duMode().entries()[0].path == firstEntry.string());
}

// 2026-09-19追加、直接のユーザー依頼("DUの修正と、戻り値の仕組みが
// 無いようなのでMAN/LOCATEと同じ仕様にしてほしい"): MAN/LOCATE自身の
// 同種のテストと対をなすDU版――GREPの結果からDU内のマッチへ着地した
// 場合、^Gはduに一段ずつ正しく戻る(duReturnStack_、application.hpp
// 自身の宣言コメント参照)。
// 2026-09-11更新、直接のユーザー依頼("^Sと^[iをFILE/DIR以外へも反映
// してほしい")によりDu起点の^S(Basic)もin-place方式になった――
// currentMode_はDuのまま変わらず、専用の一覧Grep画面を経由しなくなった
// ため、「Grep結果画面からEnter→Du、BackでGrepへ戻る」という以前の
// 中間段階は無くなった。今はDu自身のマッチへカーソルが直接乗った状態
// のまま留まり、Enterを押すとDu自身の通常のEnter処理(openSelectedDuEntry())
// がそのままそのエントリを開く――そこからのBackはDuへ、さらにBackで
// Directoryへ収束し、それ以上のBackはDirectoryに留まる。
TEST_CASE_FIXTURE(Fixture, "GREP started from Du mode: stays in-place (Du), Enter opens the matched entry, "
                           "and Back converges to Directory without bouncing forever") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);
  if (app.activeContentView().lines().empty()) {
    MESSAGE("du not available or produced no entries -- skipping");
    return;
  }
  std::string knownDuLine =
      std::max_element(app.activeContentView().lines().begin(), app.activeContentView().lines().end(),
                        [](const auto &a, const auto &b) { return a.text.size() < b.text.size(); })
          ->text;

  REQUIRE(app.handleCommand(Command::StartGrep));
  feedText(knownDuLine);
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Du);  // in-place: never left Du
  CHECK_FALSE(app.grepMode().matches().empty());

  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() != ApplicationMode::Du);  // opened the matched entry

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Du);
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  // Mashing further must NOT bounce back into Du -- it should settle
  // on Directory and stay there.
  for (int i = 0; i < 3; i++) app.handleCommand(Command::Back);
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(StartDu): refused outside Directory/File mode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));  // enter Du mode itself first
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);

  CHECK_FALSE(app.handleCommand(Command::StartDu));  // Du mode itself isn't Directory or File
}

// Direct user spec: navigating to a different folder first, then
// pressing ^Jd again, tallies THAT folder -- replacing the old
// "retype a path in a prompt" flow with ordinary navigation.
TEST_CASE_FIXTURE(Fixture, "handleCommand(StartDu): tallies whatever folder is current after navigating "
                           "elsewhere, not the original one") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);
  REQUIRE(app.handleCommand(Command::Back));

  int idx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.directoryMode().currentDirectory() != path());

  CHECK(app.handleCommand(Command::StartDu));
  waitForDu(app);
  CHECK(app.duMode().currentPath() != path());
}

// 2026-09-19 re-reversal, a direct user request ("最初のDUを^J^Dで
// 発動した場合、集計結果がまだ無いので、そのときに限り^Jdと同じく
// duを起動してほしい"): the 2026-09-09 "pure redisplay, no-op with
// nothing to redisplay" spec (see this test's own former title/comment)
// is overridden for this one specific case -- with NO DU ever run yet,
// ^J^D now falls back to running `du` itself (exactly like ^Jd), rather
// than staying a no-op. Once a real DU session exists, ^J^D goes back
// to being a pure redisplay (the sibling test right below, unchanged).
TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^D runs `du` (like ^Jd) when DU has never been run yet") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  if (!app.feedByte(0x04, std::nullopt)) {
    MESSAGE("du not available -- skipping");
    return;
  }
  waitForDu(app);
  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Du);
  CHECK(app.duMode().currentPath() == path());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^D redisplays the existing DU results without a prompt or "
                           "re-running `du`, once DU has been run at least once") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));  // runs `du` immediately now, no prompt
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);
  std::vector<DuEntry> firstEntries(app.duMode().entries().begin(), app.duMode().entries().end());

  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);

  app.feedByte(0x0a, std::nullopt);  // ^J
  CHECK(app.feedByte(0x04, std::nullopt));  // ^D
  CHECK_FALSE(app.promptActive());  // no prompt -- straight back to the results
  CHECK(app.currentMode() == ApplicationMode::Du);
  CHECK(app.duMode().entries().size() == firstEntries.size());
}

// 2026-08-13 (CLAUDE.md history item 291's mode-line audit): duView_
// had no ModeLineFieldsProvider at all before this round.
TEST_CASE_FIXTURE(Fixture, "DU: mode line shows 'DU' and a byte count once results are shown") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);
  const std::string &modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.find("DU") != std::string::npos);
  std::size_t expectedBytes = 0;
  for (const auto &line : app.activeContentView().lines()) expectedBytes += line.text.size() + 1;
  REQUIRE(expectedBytes > 0);
  CHECK(modeLine.find(std::to_string(expectedBytes)) != std::string::npos);
}

// 2026-09-10 fix, a direct user report ("^Jd直後の画面に戻り値が
// 無い"): duView_'s own ModeLineFieldsProvider used to show only the
// literal name "DU" -- unlike every other mode (Directory/File/Archive
// etc.), it never showed WHICH folder was actually tallied.
//
// 2026-09-14 correction (a real correct-mode-line comparison from the
// user): trailing3 now tracks the HIGHLIGHTED entry's own folder
// (duEntryFolder(), no "DU: " label -- the name field already says
// "DU"), falling back to duMode_.currentPath() only when nothing is
// selected -- at the top entry right after ^Jd that's the same string,
// which is what this substring check exercises.
TEST_CASE_FIXTURE(Fixture, "DU: mode line shows which folder was tallied") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);
  const std::string &modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.find(app.duMode().currentPath()) != std::string::npos);
}

// 2026-09-10 addition, a direct user request ("^J6ではハイライト
// 表示されているエントリのパスの(ls)となってほしい"): jumpToPathPrefix()
// now supports ApplicationMode::Du, mirroring the Locate/ListBrowser
// branches just above it -- it uses the HIGHLIGHTED entry's own path
// (not duMode_.currentPath(), the tallied root), matching the user's
// own example (highlighted "8K  .../commands/add-feature.md", ^J-digit
// truncates THAT path, not the tallied folder).
TEST_CASE_FIXTURE(Fixture, "DU: ^J-digit (JumpToPathPrefix) truncates the HIGHLIGHTED entry's own path, "
                           "not the tallied root folder") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  while (app.duMode().selectedEntry() &&
         !(app.duMode().selectedEntry()->isDirectory &&
           app.duMode().selectedEntry()->path.find("subdir") != std::string::npos)) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.duMode().selectedEntry() != nullptr);
  std::string selectedPath = app.duMode().selectedEntry()->path;
  REQUIRE(selectedPath != app.duMode().currentPath());  // the whole point of this test

  app.feedByte(0x0a, std::nullopt);  // ^J
  CHECK(app.feedByte('1', std::nullopt));  // ^J1
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(selectedPath.find(app.directoryMode().currentDirectory()) == 0);  // a real prefix of the selected entry's path
}

TEST_CASE_FIXTURE(Fixture, "DU: Enter on a directory entry drills into DirectoryMode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);

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

// 2026-09-09 fix, a real user report ("どれかを選ぶと戻り値がDUじゃ
// ないのでDU一覧へ戻らない"): openSelectedDuEntry()'s directory branch
// used to switch to plain Directory mode with no way back at all --
// ^G there just did ordinary directory-mode back-navigation, never DU.
// Now mirrors openSelectedLocateResult()'s own setSearchAnnotation()+
// lastDirectorySearchKind_ convention, so Back routes to
// returnToDuResults() instead.
TEST_CASE_FIXTURE(Fixture, "DU: Back from a directory opened via DU returns to the DU results, not "
                           "ordinary directory navigation") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);

  while (app.duMode().selectedEntry() &&
         !(app.duMode().selectedEntry()->isDirectory &&
           app.duMode().selectedEntry()->path.find("subdir") != std::string::npos)) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Du);
}

// 2026-09-15追加、直接のユーザー報告("^Jd→.huskyを選択→pre-commit
// を選択→^Gで行き来ループ、LOCATEでは起きない")、続く訂正("3.が惜しい。
// pre-commitを選択の時点でDUの履歴をクリアでよろしく（現状では引き続き
// ^Gの戻り値がDU: /Users/...）。お手本なら`.`（カレントフォルダー）を
// 選択、で確認可能"): DU経由でディレクトリへ降りた「後」、その中で
// (DU経由ではなく)普通にEnterで別のファイルを開くと、そのファイルから
// のBackはreturnToDirectory()の「既にそこにいる」早期リターン(親が
// 既にcurrentDirectory()と一致するのでenterDirectory()呼び出し自体を
// スキップする)を通るため、DUの注釈が生き残ったままDirectoryへ戻る。
// 最初の修正(returnFromDu()側でのクリア)は、Du結果画面を経由して1
// 往復した「後」でようやくクリアされる程度にとどまっていた――ユーザー
// 訂正の通り、ファイルを「選択した時点」(openSelectedEntry()の
// ファイル分岐、ディレクトリのエントリ("."含む)を開く方は
// enterDirectory()自身が既に内部でクリアするので「お手本」通り)で
// 即座にクリアするべきだった。openSelectedEntry()自身の
// clearArcExtractSourceAnnotation()すぐ下の新規呼び出し参照。
TEST_CASE_FIXTURE(Fixture, "DU: opening an ORDINARY file (not via DU) from within a DU-drilled-into "
                           "directory clears the DU annotation IMMEDIATELY, not just after one round trip "
                           "through the DU results") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);

  while (app.duMode().selectedEntry() &&
         !(app.duMode().selectedEntry()->isDirectory &&
           app.duMode().selectedEntry()->path.find("subdir") != std::string::npos)) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.directoryMode().hasSearchAnnotation());

  // DU経由ではなく、普通にsub.txtをEnterで開く(openSelectedEntry()、
  // openSelectedDuEntry()ではない)。
  int idx = indexOf("sub.txt");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  // 「選択した時点」で既にクリアされている(Backを待たない)。
  CHECK_FALSE(app.directoryMode().hasSearchAnnotation());

  REQUIRE(app.handleCommand(Command::Back));  // File -> Directory(subdir)、注釈は既に無い
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK_FALSE(app.directoryMode().hasSearchAnnotation());

  // Duへ一切跳ね返らず、通常の親ディレクトリ探索(subdir -> root)へ
  // 直接進む――以前のように結果画面を1往復する必要すら無い。
  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == path());
}

TEST_CASE_FIXTURE(Fixture, "DU: Enter on a file entry drills into FileMode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);

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

// Same fix as the directory case just above, for the fileReturnMode_
// side -- openSelectedDuEntry() used to hardcode fileReturnMode_ =
// Directory unconditionally.
TEST_CASE_FIXTURE(Fixture, "DU: Back from a file opened via DU returns to the DU results") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);

  while (app.duMode().selectedEntry() &&
         (app.duMode().selectedEntry()->isDirectory ||
          app.duMode().selectedEntry()->path.find("plain.txt") == std::string::npos)) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Du);
}

// 2026-09-09 fix, a real user report ("その一覧ではカレントディレク
// トリと配下の境界が//になっている"): DuMode::loadUsage() used to pass
// the (trailing-slash) path straight to `du`, and macOS's BSD `du`
// echoes child entries as "<path>//<child>" when given a trailing
// slash (verified directly: `du -ak /tmp/dutest/` vs `du -ak
// /tmp/dutest`). Now strips the trailing slash first.
TEST_CASE_FIXTURE(Fixture, "DU: no double slash in child entry paths when the tallied directory has a "
                           "trailing slash (as directoryMode_.currentDirectory() always does)") {
  REQUIRE(app.start(path()));
  REQUIRE(path().back() == '/');  // the tallied path IS trailing-slash, the whole point of this test
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);

  for (const auto &entry : app.duMode().entries()) {
    CHECK(entry.path.find("//") == std::string::npos);
  }
}

// 2026-09-10 addition, direct user request ("DU画面でのファイルサイズ
// エリアを切り離すならファイル名表示の開始位置を揃えて欲しい"):
// DuMode::formatEntryLine() now right-justifies the size column to the
// widest entry in the list, so every row's filename starts at the same
// screen column regardless of how many digits its own size has.
TEST_CASE_FIXTURE(Fixture, "DU: filenames start at the same column across entries with differing size "
                           "digit counts") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  REQUIRE(app.duMode().entries().size() > 1);

  const auto &lines = app.activeContentView().lines();
  REQUIRE(lines.size() == app.duMode().entries().size());
  std::optional<std::size_t> nameStartColumn;
  for (std::size_t i = 0; i < lines.size(); i++) {
    REQUIRE(lines[i].text.size() >= app.duMode().entries()[i].path.size());
    std::size_t thisStart = lines[i].text.size() - app.duMode().entries()[i].path.size();
    if (!nameStartColumn) {
      nameStartColumn = thisStart;
    } else {
      CHECK(thisStart == *nameStartColumn);
    }
  }
}

// 2026-09-10 fix, a real user report (HISTORY.md:13905's symptom
// investigated further): the DIRECTORY branch's "DU: ..." annotation
// DOES survive real mode-line crowding (verified here with a
// deliberately very long nested path) -- ContentView::modeLineText()
// itself is a red herring for this (it goes through
// fitModeLineFields()'s plain all-or-nothing path, NOT the actual
// crowded-layout renderer), so this checks the real painted row via
// RecordingDisplay instead. The FILE branch, however, had a genuine
// bug: it fixed fileReturnMode_ but never called
// fileMode_.setSearchAnnotation() at all (unlike the directory branch
// and unlike openSelectedGrepMatch()'s own File branch) -- the mode
// line showed no "DU: ..." whatsoever for files opened from DU, not
// just a truncated one. Both branches are covered here.
//
// 2026-09-15 correction (a real correct-mode-line comparison from the
// user, walking back an earlier HISTORY.md:13974 misreading): the FILE
// branch no longer uses the "DU: <label>" convention at all -- it now
// mirrors openSelectedLocateResult()'s own setSearchOrigin("LOCATE",
// path) precedent (setSearchOrigin("DU", entry->path) in
// openSelectedDuEntry()), where the `name` field itself becomes the
// literal "DU" and the trailing field carries the bare path with no
// "DU: " prefix -- the mode NAME is what declares "you're in DU", not
// a labeled return value. So the FILE-branch check below now looks for
// bare "DU" (the name field surviving crowding), not "DU: ".
TEST_CASE_FIXTURE(Fixture, "DU: 'DU: ...' annotation appears in the actual painted mode line for both "
                           "directory and file entries, even with a very long path") {
  fs::path deep = root;
  for (int i = 0; i < 6; i++) {
    deep /= "quite_a_long_subdirectory_name_" + std::to_string(i);
    fs::create_directory(deep);
  }
  { std::ofstream(deep / "leaf.txt") << "x\n"; }
  REQUIRE(app.start(deep.string() + "/"));

  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);
  int entryCount = static_cast<int>(app.duMode().entries().size());
  REQUIRE(entryCount > 0);
  for (int i = 0; i < entryCount &&
       !(app.duMode().selectedEntry() && app.duMode().selectedEntry()->isDirectory &&
         app.duMode().selectedEntry()->path.find("quite_a_long_subdirectory_name_1") != std::string::npos);
       i++) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.duMode().selectedEntry() != nullptr);
  REQUIRE(app.duMode().selectedEntry()->isDirectory);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  CHECK(display.rowText(display.rows() - 1).find("DU: ") != std::string::npos);
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::Du);

  // Back doesn't reset the cursor -- it may still be past "leaf.txt"
  // from the search above, so this can't assume forward-only movement
  // will reach it. MoveUp back to the top first, then search forward
  // from a known starting point (bounded by entryCount either way).
  for (int i = 0; i < entryCount; i++) app.handleCommand(Command::MoveUp);
  for (int i = 0; i < entryCount &&
       !(app.duMode().selectedEntry() && app.duMode().selectedEntry()->path.find("leaf.txt") != std::string::npos);
       i++) {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  REQUIRE(app.duMode().selectedEntry() != nullptr);
  REQUIRE(app.duMode().selectedEntry()->path.find("leaf.txt") != std::string::npos);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  CHECK(display.rowText(display.rows() - 1).find("DU") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "DU: Back returns to Directory mode") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.directoryMode().currentDirectory() == path());
}

// -- History/list browser: ^R (recent directories), ^J^R (shell
// command history) -- see class comment for why only these 2 of
// m_list()'s 9 categories are wired in this slice --

TEST_CASE_FIXTURE(Fixture, "resolveCommand: ^R resolves to ToggleHistoryView, not StartDirectoryHistory "
                           "(2026-08-04 correction: an earlier version of this port aliased plain ^R to "
                           "StartDirectoryHistory, going by a surface-level reading of usilicrc's own key "
                           "list; a direct user clarification plus a re-read of menu.c's m_list() showed ^R "
                           "is actually a context-sensitive toggle on whichever ListBrowser is already open, "
                           "unrelated to which RingCategory that happens to be -- see ToggleHistoryView's own "
                           "comment)") {
  CHECK(Application::resolveCommand(0x12, std::nullopt) == Command::ToggleHistoryView);
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^J ^R starts the command-history browser (StartCommandHistory)") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0a, std::nullopt);
  CHECK(app.feedByte(0x12, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(StartDirectoryHistory): reachable from OUTSIDE Directory mode too "
                           "(2026-08-12 fix -- an earlier version of startListBrowser() rejected this "
                           "outright unless currentMode_==Directory, contradicting the K-45..K-48 family's "
                           "own documented \"reachable from any mode\" design -- see startListBrowser()'s "
                           "own comment for the direct user report that caught this trying ^X^D from CUT)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK(app.handleCommand(Command::StartDirectoryHistory));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
}

// 2026-08-15 fix: a real report + screenshot (=bf/=bg) -- ^X^D's own
// mode line showed a garbled, unrelated fragment left over from an
// EARLIER, unrelated MAN/GREP/CUT direct-entry candidate-list session
// that never fully resolved. Root cause: startListBrowser() (the
// single shared entry point for every PLAIN FILE/DIR/MAN/PROGRAM/
// COMMAND picker) never cleared promptHistoryRecallKind_/
// promptHistoryRecallIsDirectEntry_ itself, so a stale value from that
// completely different feature made listBrowserView_'s own provider
// take the wrong branch (the direct-entry "live combobox" shape
// instead of the plain picker shape). Reproduced here by starting a
// MAN direct-entry list (which sets promptHistoryRecallKind_ even on
// an empty ring) and, WITHOUT resolving it via Enter/^G, switching
// straight to ^X^D.
TEST_CASE_FIXTURE(Fixture, "startListBrowser: clears a stale promptHistoryRecallKind_ left over from an "
                           "earlier, unresolved MAN/GREP/CUT direct-entry list") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartManList));  // sets promptHistoryRecallKind_, even with no history
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  history.pushRecent(RingCategory::RecentDirectory, "/some/directory");
  REQUIRE(app.handleCommand(Command::StartDirectoryHistory));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  std::string modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.substr(0, 3) == "DIR");  // the plain picker's own name, not "HISTORY"/promptLabelText()
  CHECK(modeLine.find("/some/directory") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "history browser: shows real pushRecent() entries sorted alphabetically, "
                           "de-duplicated (2026-08-04 correction: a direct user clarification -- \"FILE"
                           "（DIR）は今までアクセスしたファイル（ディレクトリ）をソートしたのち重複を排除"
                           "した一覧\" -- replaced this port's earlier reverse-chronological order, which "
                           "was never actually what the normal ^X^D screen shows; reverse-chronological "
                           "order is real, but belongs to the separate ^R \"HISTORY\" toggle instead -- see "
                           "the toggleListBrowserHistory tests below)") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentDirectory, "/one");
  history.pushRecent(RingCategory::RecentDirectory, "/two");
  history.pushRecent(RingCategory::RecentDirectory, "/three");
  history.pushRecent(RingCategory::RecentDirectory, "/one");  // de-dup: re-visiting doesn't add a second entry

  REQUIRE(app.handleCommand(Command::StartDirectoryHistory));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  const auto &items = app.listBrowserMode().items();
  REQUIRE(items.size() == 4);  // path() (auto-recorded by start()) + /one, /two, /three -- NOT 5, dedup held
  CHECK(std::is_sorted(items.begin(), items.end()));
  CHECK(std::find(items.begin(), items.end(), "/one") != items.end());
  CHECK(std::find(items.begin(), items.end(), "/two") != items.end());
  CHECK(std::find(items.begin(), items.end(), "/three") != items.end());
  CHECK(std::find(items.begin(), items.end(), path()) != items.end());
}

TEST_CASE_FIXTURE(Fixture, "history browser: Enter on a recent directory navigates DirectoryMode there") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentDirectory, (root / "subdir").string());

  REQUIRE(app.handleCommand(Command::StartDirectoryHistory));
  const auto &items = app.listBrowserMode().items();
  auto it = std::find(items.begin(), items.end(), (root / "subdir").string());
  REQUIRE(it != items.end());
  while (app.listBrowserMode().selectedItem() == nullptr ||
         *app.listBrowserMode().selectedItem() != (root / "subdir").string())
    app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));

  CHECK(app.currentMode() == ApplicationMode::Directory);
  bool sawSubTxt = false;
  for (const auto &entry : app.directoryMode().entries())
    if (entry.name.find("sub.txt") != std::string::npos) sawSubTxt = true;
  CHECK(sawSubTxt);
}

TEST_CASE_FIXTURE(Fixture, "^R (ToggleHistoryView): switches the open FILE list-browser from sorted to "
                           "chronological order, newest at the LAST index, and back (2026-08-04, direct "
                           "user clarification: \"HISTORYはアクセスした順（最下行が最新）\")") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.handleCommand(Command::Back));

  REQUIRE(app.handleCommand(Command::StartFileHistory));
  CHECK(std::is_sorted(app.listBrowserMode().items().begin(), app.listBrowserMode().items().end()));

  REQUIRE(app.handleCommand(Command::ToggleHistoryView));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);  // still the same screen, not a new one
  REQUIRE(app.listBrowserMode().items().size() == 1);
  CHECK(app.listBrowserMode().items().back() == (root / "plain.txt").string());  // newest at the bottom
  CHECK(app.activeContentView().cursorIndex() == static_cast<int>(app.listBrowserMode().items().size()) - 1);
  CHECK(app.activeContentView().modeLineText().rfind("HISTORY ", 0) == 0);  // "HISTORY", not "FILE"

  REQUIRE(app.handleCommand(Command::ToggleHistoryView));  // toggling again returns to sorted view
  CHECK(app.activeContentView().modeLineText().rfind("FILE ", 0) == 0);
}

TEST_CASE_FIXTURE(Fixture, "^R (ToggleHistoryView): a non-consecutive revisit shows ONCE in the sorted "
                           "FILE view but TWICE in the HISTORY view (2026-08-04 user correction: "
                           "\"アクセスした順なので重複もあり得る点。SORT後一覧を並び替えるだけでは不十分\" "
                           "-- History::pushRecent() only suppresses an IMMEDIATE repeat of the single "
                           "most-recent entry, so visiting A, then B, then A again leaves a genuine "
                           "duplicate in the raw chronological log)") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentFile, (root / "a.txt").string());
  history.pushRecent(RingCategory::RecentFile, (root / "b.txt").string());
  history.pushRecent(RingCategory::RecentFile, (root / "a.txt").string());  // revisit, NOT consecutive

  REQUIRE(app.handleCommand(Command::StartFileHistory));
  REQUIRE(app.listBrowserMode().items().size() == 2);  // sorted view: a.txt, b.txt -- de-duplicated

  REQUIRE(app.handleCommand(Command::ToggleHistoryView));
  REQUIRE(app.listBrowserMode().items().size() == 3);  // chronological view: a.txt, b.txt, a.txt again
  CHECK(app.listBrowserMode().items()[0] == (root / "a.txt").string());
  CHECK(app.listBrowserMode().items()[1] == (root / "b.txt").string());
  CHECK(app.listBrowserMode().items()[2] == (root / "a.txt").string());
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(ToggleHistoryView): refused outside ListBrowser mode, matching "
                           "m_list()'s own echo_mess(S_errf) for a context ^R doesn't apply to -- using "
                           "Key mode, since Directory/File/Archive/ArchiveContent now all have their OWN "
                           "dedicated ^R meaning (see the \"^R directory-name/file-name history\"/\"^R in "
                           "Archive\" tests)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  REQUIRE(app.currentMode() == ApplicationMode::Key);

  CHECK_FALSE(app.handleCommand(Command::ToggleHistoryView));
  CHECK(app.currentMode() == ApplicationMode::Key);
}

// -- ^R prompt-time history recall (MAN/LOCATE/FGREP/EGREP), ported
// from menu.c's m_list() per-context dispatch -- see CLAUDE.md item
// 161/162 for the source read this is based on --

TEST_CASE_FIXTURE(Fixture, "^R (startPromptHistoryRecall): opens the RAW chronological history ListBrowser "
                           "while an FGREP prompt is active, name=\"HISTORY\" with \"FGREP\" as a separate "
                           "right-aligned badge (2026-08-05 correction: an earlier same-day revision showed a "
                           "SORTED, de-duplicated list with name=\"FGREP\" here, matching FILE/DIR's own ^X^F/"
                           "^X^D screen -- a real user reproduction (screenshot ⑪) showed this is wrong for a "
                           "PROMPT-triggered recall specifically: no separate sorted ring exists for these "
                           "categories in legacy at all, so it's always the raw log, always labeled \"HISTORY\", "
                           "with the category shown as a badge instead)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // records "plain.txt" into RecentFgrepPattern

  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.handleCommand(Command::StartFgrep));  // a fresh prompt -- history exists from the search above
  CHECK(app.promptActive());

  REQUIRE(app.feedByte(0x12, std::nullopt));  // ^R
  CHECK_FALSE(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.listBrowserMode().items().size() == 1);
  CHECK(app.listBrowserMode().items()[0] == "plain.txt");
  CHECK(app.activeContentView().modeLineText().rfind("HISTORY ", 0) == 0);
  std::string row = display.rowText(display.rows() - 1);
  CHECK(row.find("FGREP") != std::string::npos);  // the right-aligned category badge
}

TEST_CASE_FIXTURE(Fixture, "^R prompt-time recall: Enter on a picked pattern re-runs the search exactly as a "
                           "live-typed submission would") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.handleCommand(Command::Back));

  REQUIRE(app.handleCommand(Command::StartFgrep));
  REQUIRE(app.feedByte(0x12, std::nullopt));  // ^R
  REQUIRE(app.listBrowserMode().items().size() == 1);

  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Grep);
  REQUIRE(app.grepMode().matches().size() == 1);
  CHECK(app.grepMode().matches()[0].text.find("plain.txt") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "^R prompt-time recall: ^G restores the ORIGINAL prompt with whatever had already "
                           "been typed before ^R was pressed, not a blank one") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("plain.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.handleCommand(Command::Back));

  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("partial");  // typed, but not yet submitted, when ^R interrupts
  REQUIRE(app.feedByte(0x12, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.promptText() == "partial");
  CHECK(display.rowText(display.rows() - 1).find("FGREP") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "^R prompt-time recall: distinguishes FGREP from EGREP history, matching legacy's "
                           "own separate p.hfgrep/p.hegrep rings") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("fixedpattern");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.handleCommand(Command::Back));

  // EGREP opens directly into the candidate list, not a blank prompt
  // (startGrepPrompt()'s own comment). ^G no longer reopens a blank
  // prompt to type a fresh pattern from there (2026-08-12 fix, a
  // direct user instruction -- see returnFromListBrowser()'s own
  // comment) -- ^U (clears the pre-filled seed, same as the "start
  // editing" transition any other byte would trigger) is the current
  // way to type a genuinely fresh pattern instead.
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.feedByte(0x15, std::nullopt));  // ^U: starts editing, clears the seed
  feedText("plain\\.txt");
  REQUIRE(app.feedByte(0x0d, std::nullopt));

  const auto &egrepRing = history.recentEntries(RingCategory::RecentEgrepPattern);
  CHECK(egrepRing.back() == "plain\\.txt");  // the just-submitted pattern landed in EGREP's own ring...
  CHECK(history.recentEntries(RingCategory::RecentFgrepPattern).back() ==
        "fixedpattern");  // ...NOT FGREP's, which stayed separate
}

TEST_CASE_FIXTURE(Fixture, "^R prompt-time recall: swallowed (no ListBrowser, no literal byte inserted) for a "
                           "prompt kind without its own history yet, matching m_list()'s own echo_mess(S_errf)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::ToggleMark));
  REQUIRE(app.handleCommand(Command::StartCopy));
  REQUIRE(app.promptActive());

  REQUIRE(app.feedByte(0x12, std::nullopt));  // ^R -- CopyDestination has no history integration in this slice
  CHECK(app.promptActive());
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.promptText().empty());  // 0x12 itself was NOT inserted as literal text
}

// -- ^R at the CUT prompt (CutPattern, 2026-08-05): FIXED (^Jc)
// shares FGREP's own ring, REGEX (^J^C) shares EGREP's -- matching
// legacy's own ss.p.hfgrep/hegrep split for M_cut2/M_cut, ported here
// (executeCut()'s own comment) after a direct user report that an
// EARLIER same-day revision used EGREP's ring unconditionally for
// BOTH, which is itself a real legacy bug (menu.c's own `case M_cut:
// case M_cut2: hp->ss.p.hegrep = save_history(...)` never branches
// here either, unlike the FGREP/EGREP block just above it in the same
// switch) -- also fixed in old/src/menu.c itself, see that commit. --

TEST_CASE_FIXTURE(Fixture, "^R at the CUT prompt (FIXED, ^Jc/StartCutFixed) recalls from RecentFgrepPattern, "
                           "NOT RecentEgrepPattern -- matching FGREP's own fixed-string semantics") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("plain");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // records "plain" into RecentFgrepPattern
  REQUIRE(app.handleCommand(Command::Back));  // not nested -- one ^G exits CUT entirely, back to File
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  REQUIRE(app.feedByte(0x12, std::nullopt));  // ^R
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.listBrowserMode().items().size() == 1);
  CHECK(app.listBrowserMode().items()[0] == "plain");

  const auto &fgrepRing = history.recentEntries(RingCategory::RecentFgrepPattern);
  REQUIRE(fgrepRing.size() == 1);
  CHECK(fgrepRing[0] == "plain");
  CHECK(history.recentEntries(RingCategory::RecentEgrepPattern).empty());
}

TEST_CASE_FIXTURE(Fixture, "^R at the CUT prompt (REGEX, ^J^C/StartCutRegex) recalls from RecentEgrepPattern") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  // StartCutRegex (2026-08-05: opens directly into the candidate list,
  // not a blank prompt -- see startCutPrompt()'s own comment) --
  // typing directly into the still-showing list (a live-editable
  // combobox, same as EGREP's own candidate list -- see
  // feedRegexListByte()'s own comment) is how a fresh pattern gets
  // typed; ^G no longer reopens a blank prompt here (2026-08-22 fix,
  // item 405: extended returnFromListBrowser()'s 2026-08-12 GrepPattern
  // fix to CutPattern too, so ^G instead returns straight to where
  // this was invoked from).
  REQUIRE(app.handleCommand(Command::StartCutRegex));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  feedText("pla.n");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // records "pla.n" into RecentEgrepPattern
  REQUIRE(app.currentMode() == ApplicationMode::Cut);
  // Regex direct-entry CUT's own LIFO stack (cutReturnsToCandidateList_,
  // see its own declaration comment): the FIRST Back returns to the
  // candidate list itself, the SECOND reaches File.
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartCutRegex));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.listBrowserMode().items().size() == 1);
  CHECK(app.listBrowserMode().items()[0] == "pla.n");

  const auto &egrepRing = history.recentEntries(RingCategory::RecentEgrepPattern);
  REQUIRE(egrepRing.size() == 1);
  CHECK(egrepRing[0] == "pla.n");
  CHECK(history.recentEntries(RingCategory::RecentFgrepPattern).empty());
}

TEST_CASE_FIXTURE(Fixture, "^R prompt-time recall: the list is the RAW chronological log, duplicates included "
                           "and NEWEST at the end/selected -- not sorted or de-duplicated (2026-08-05 user "
                           "reproduction, screenshot ⑪: 11 entries with \"man\"/\"usilic\" each repeated several "
                           "times, \"usilic\" -- the most recent -- selected)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("man");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.handleCommand(Command::Back));

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("usilic");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.handleCommand(Command::Back));

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("man");  // revisit, non-consecutive -- must NOT be de-duplicated away
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.handleCommand(Command::Back));

  REQUIRE(app.handleCommand(Command::StartCutFixed));
  REQUIRE(app.feedByte(0x12, std::nullopt));  // ^R
  REQUIRE(app.listBrowserMode().items().size() == 3);  // man, usilic, man -- raw order, duplicate "man" kept
  CHECK(app.listBrowserMode().items()[0] == "man");
  CHECK(app.listBrowserMode().items()[1] == "usilic");
  CHECK(app.listBrowserMode().items()[2] == "man");
  CHECK(app.activeContentView().cursorIndex() == 2);  // newest ("man", the last one) selected, not index 0

  std::string text = app.activeContentView().modeLineText();
  CHECK(text.rfind("HISTORY ", 0) == 0);
  std::string row = display.rowText(display.rows() - 1);
  CHECK(row.find("CUT") != std::string::npos);  // the right-aligned category badge
}

// -- ^R at the live SHELL prompt (integration point 3 of the ^R
// design -- see CLAUDE.md item 162/163). History is seeded directly
// via history.pushRecent() rather than actually running commands
// through the real bash child, keeping these independent of shell
// startup timing except where the live prompt itself is what's under
// test. --

TEST_CASE_FIXTURE(Fixture, "^R (startShellHistoryRecall): opens a sorted ShellCommand history ListBrowser at "
                           "the live SHELL prompt") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));
  history.pushRecent(RingCategory::ShellCommand, "echo two");
  history.pushRecent(RingCategory::ShellCommand, "echo one");

  REQUIRE(app.feedShellByte(0x12, std::nullopt));  // ^R
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.listBrowserMode().items().size() == 2);
  CHECK(std::is_sorted(app.listBrowserMode().items().begin(), app.listBrowserMode().items().end()));
}

TEST_CASE_FIXTURE(Fixture, "^R at SHELL prompt: Enter on a picked command INSERTS it into the command line "
                           "but does NOT run it (2026-08-05, direct user choice: option (A), since a picked "
                           "past command could be destructive -- rm, mv, ... -- unlike MAN/LOCATE/FGREP's own "
                           "side-effect-free recall)") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));
  history.pushRecent(RingCategory::ShellCommand, "echo recalled-not-run");

  REQUIRE(app.feedShellByte(0x12, std::nullopt));  // ^R
  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Shell);
  CHECK(app.shell().commandLine().text() == "echo recalled-not-run");
  // Not yet submitted -- no scrollback line reflects it having run.
  for (const auto &line : app.shell().scrollback().lines())
    CHECK(line.text.find("recalled-not-run") == std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "^R at SHELL prompt: ^G restores whatever had already been typed before ^R, not a "
                           "blank prompt") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));
  history.pushRecent(RingCategory::ShellCommand, "echo something");

  for (unsigned char c : std::string("partial")) app.feedShellByte(c, std::nullopt);
  REQUIRE(app.feedShellByte(0x12, std::nullopt));  // ^R
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Shell);
  CHECK(app.shell().commandLine().text() == "partial");
}

TEST_CASE_FIXTURE(Fixture, "^R at SHELL prompt: refused outside Shell mode (using Key mode, since Directory/"
                           "File/Archive/ArchiveContent now all have their OWN top-level ^R meaning -- see the "
                           "dedicated \"^R directory-name/file-name history\"/\"^R in Archive\" tests below)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  REQUIRE(app.currentMode() == ApplicationMode::Key);

  CHECK_FALSE(app.feedByte(0x12, std::nullopt));  // top-level ^R (ToggleHistoryView) is a no-op here
  CHECK(app.currentMode() == ApplicationMode::Key);
}

// 2026-08-20 addition, a direct user request: "ARC:^R新設。内容は、
// FILE一覧のうちARC対応拡張子...付属ファイルのみの一覧" + clarification
// "カレントディレクトリ如何に問わず今までアクセスしたアーカイブ
// ファイルが対象。ARCモードでの^Rで発動". Reuses the same RecentFile
// ring/picker Directory/File's own ^R (and ^X^F) already use, filtered
// to archive-compatible paths only (archiveHistoryFilterActive()).
TEST_CASE_FIXTURE(Fixture, "^R in Archive mode opens the RecentFile picker filtered to archive-compatible "
                           "paths only, regardless of the current directory") {
  fs::path plain = root / "plain2.txt";
  { std::ofstream(plain) << "not an archive\n"; }
  history.pushRecent(RingCategory::RecentFile, plain.string());
  history.pushRecent(RingCategory::RecentFile, (root / "archive.tar").string());

  REQUIRE(app.start(path()));
  int idx = indexOf("archive.tar");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Archive);

  CHECK(app.feedByte(0x12, std::nullopt));  // ^R
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK(app.listBrowserMode().items().size() == 1);
  CHECK(app.listBrowserMode().items()[0] == (root / "archive.tar").string());

  // Selecting the filtered entry still opens it as an actual archive.
  CHECK(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Archive);
}

TEST_CASE_FIXTURE(Fixture, "^R directory-name/file-name history (2026-08-18 addition, a direct user request): "
                           "plain ^R from ordinary Directory browsing opens the SAME DIR picker ^X^D does, and "
                           "from ordinary File viewing opens the SAME FILE picker ^J^F does -- a genuinely NEW "
                           "shortcut, not a reproduction of legacy's own dead-end bare-^R dispatch-table entry "
                           "(see item 353's own investigation: that path opened a functionally useless empty "
                           "\"HISTORY 0/0\" screen). Deliberately scoped to ONLY these two modes, unlike "
                           "toggleListBrowserHistory()'s own separate, unrelated in-ListBrowser sorted<->"
                           "chronological toggle (\"機能的にはDIR（FILE）とダブるけれども、どこからでもOKでは"
                           "ない点が違う\")") {
  REQUIRE(app.start(path()));
  CHECK(app.currentMode() == ApplicationMode::Directory);

  REQUIRE(app.feedByte(0x12, std::nullopt));  // plain ^R from Directory mode
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK(app.activeContentView().modeLineText().rfind("DIR ", 0) == 0);
  REQUIRE(app.feedByte(0x07, std::nullopt));  // ^G back out
  CHECK(app.currentMode() == ApplicationMode::Directory);

  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x12, std::nullopt));  // plain ^R from File mode
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK(app.activeContentView().modeLineText().rfind("FILE ", 0) == 0);
}

TEST_CASE_FIXTURE(Fixture, "^R directory-name/file-name history: excluded on the ^T banner screen -- a real "
                           "file is regular reading material, the banner is a synthetic index screen this "
                           "shortcut doesn't apply to (bannerFilePath()'s own comment)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::ShowUsilicBanner));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK_FALSE(app.feedByte(0x12, std::nullopt));  // falls through to toggleListBrowserHistory()'s own no-op
  CHECK(app.currentMode() == ApplicationMode::File);
}

// -- ^R at the PROGRAM prompt (integration point 4 of the ^R design
// -- see CLAUDE.md item 162/164). ProgramMode::run() already records
// every executed invocation's full command string into
// RingCategory::ShellCommand (ported from do_prog()'s own
// save_history() call into the shared ss.p.hist ring) -- this only
// wires ^R to recall from it, reusing startPromptHistoryRecall(). --

TEST_CASE_FIXTURE(Fixture, "^R (startPromptHistoryRecall): PROGRAM prompt recalls previously-run invocations "
                           "from RingCategory::ShellCommand (already populated by ProgramMode::run() itself)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartProgram));
  feedText("echo one");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // records "echo one" via ProgramMode::run()

  REQUIRE(app.handleCommand(Command::StartProgram));
  REQUIRE(app.feedByte(0x12, std::nullopt));  // ^R
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.listBrowserMode().items().size() == 1);
  CHECK(app.listBrowserMode().items()[0] == "echo one");
}

TEST_CASE_FIXTURE(Fixture, "^R at PROGRAM prompt: Enter on a picked entry auto-EXECUTES it, unlike SHELL's own "
                           "^R (2026-08-05, direct user confirmation: \"あくまで旧版にしたがうと(B)\" -- PROGRAM "
                           "invocations are already restricted to Configuration::programSeedCommands()'s "
                           "allow-list, unlike SHELL's unrestricted command line)") {
  REQUIRE(app.start(path()));
  std::string outPath = (root / "prog_recall_out.txt").string();
  REQUIRE(app.handleCommand(Command::StartProgram));
  feedText("echo recalled > '" + outPath + "'");
  REQUIRE(app.feedByte(0x0d, std::nullopt));

  REQUIRE(app.handleCommand(Command::StartProgram));
  REQUIRE(app.feedByte(0x12, std::nullopt));  // ^R
  REQUIRE(app.listBrowserMode().items().size() == 1);

  REQUIRE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Directory);  // PROGRAM never switches mode, same as a live run

  std::ifstream in(outPath);
  std::string content;
  std::getline(in, content);
  CHECK(content == "recalled");  // the real side effect happened -- it actually ran, not just got inserted
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

// 2026-08-18 fix (a real report + screenshots =bn/=cq: "基本的に右端
// まで埋まっていないモードラインは全部不良"): drawPromptLabel() only
// ever drew individual reversed RUNS (label/position/trailing)
// separated by plain, unfilled gaps -- visible as beige "holes" on a
// real terminal. A same-day earlier attempt at this same report tried
// removing RecentFile/RecentDirectory's trailing destination-directory
// field entirely (reasoning: it looked like a near-duplicate of the
// path already being typed) -- a direct user correction: the field
// itself was correct and should stay (see "戻り値「...16文字」で
// よろしく", the SAME truncatedTrailingPath() 16-char-tail+"..."
// convention every other prompt already uses), the actual bug was the
// row not being filled to the edge around it. Fixed by filling the
// WHOLE row in reverse video as a base layer before drawing anything
// else; the live editable field's own LineEditor::redraw() (ported
// from key_eeol()'s vt_endstand()) explicitly turns standout off
// before drawing itself, "punching" a plain hole through that fill
// exactly where typing happens -- everything else on the row stays
// filled.
// 2026-08-18 fix (a real report + screenshot =cr: "一文字も入力され
// ていないと不良のまま(一文字でも入力後はOK)"): item 348's
// drawPromptLabel() fill-to-edge fix only covers the screen AFTER
// editing starts (feedRegexListByte() routes the first typed byte
// there) -- the READ-ONLY candidate-list state BEFORE any typing is
// rendered by ContentView::renderModeLine() through listBrowserView_'s
// own ModeLineFieldsProvider instead, a completely different code
// path this fix never touched, still leaving the same beige gap from
// the candidate's own path to the screen's right edge.
// 2026-08-18 follow-up fix (a real report + reference screenshot =cw):
// the READ-ONLY (not-yet-editing) screen needs to look like an
// editable field FROM THE START -- the highlighted candidate shown
// non-highlighted, matching what it switches to the instant real
// editing begins -- not just once the first byte is typed. Routed
// through refreshDirectEntryModeLine(), which reuses drawPromptLabel()
// itself (same layout) plus a non-editing-state promptEditor_ redraw.
TEST_CASE_FIXTURE(Fixture, "K-46/K-48 (^X^F/^X^D): the mode line already fills to the screen's right edge on the "
                           "read-only candidate list, before a single character has been typed, with the "
                           "candidate itself shown non-highlighted like a real editable field") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDirectoryHistory));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  // Column 78, not 79 (2026-08-18 follow-up fix): the true last column
  // (79) is deliberately left non-Standout -- see
  // ContentView::renderModeLine()'s own auto-wrap-avoidance comment
  // (a real report: byte-for-byte correct output that a real
  // Terminal.app still didn't repaint until an unrelated keypress,
  // traced to the same last-column auto-wrap class renderLine() had
  // already been fixed for once before).
  CHECK(display.standoutAt(23, 78));
  CHECK_FALSE(display.standoutAt(23, 79));
  // "DIR" then ONE continuous "1/1 END" Standout run (2026-08-28
  // reversal of an earlier same-shape fix -- a direct user correction,
  // "一律「1/1 END」でよろしく", see application.cpp's own
  // drawPromptLabel() comment): labelWidth = 3("DIR")+1 = 4, so the gap
  // is at col 3 only -- "1/1 END" (cols 4-10) is one unbroken run, the
  // internal space (col 7) included.
  CHECK_FALSE(display.standoutAt(23, 3));  // gap: DIR | 1/1 END
  CHECK(display.standoutAt(23, 7));        // WITHIN "1/1 END" -- no internal gap
  // The candidate path itself (labelWidth=12 onward) is now plain,
  // like the live editable field it visually matches -- NOT reversed
  // the way a plain ModeLineFieldsProvider `trailing` field would be.
  CHECK_FALSE(display.standoutAt(23, 15));
}

TEST_CASE_FIXTURE(Fixture, "K-46/K-48 (^X^F/^X^D): the mode line fills all the way to the screen's right edge "
                           "while editing, with only the live input field itself left non-highlighted") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDirectoryHistory));  // app.start() already auto-recorded the CWD
  REQUIRE(app.feedByte('x', std::nullopt));  // starts editing -- exercises drawPromptLabel()'s trailing-field logic
  // Column 78, not 79 -- see the sibling read-only test's own comment.
  CHECK(display.standoutAt(23, 78));
  CHECK_FALSE(display.standoutAt(23, 79));
  CHECK_FALSE(display.standoutAt(23, 15));     // ...and the live input field itself stays plain
  // ^G (not handleCommand(Back) directly): editing an active prompt
  // routes bytes through feedPromptByte() (promptKind_ != None), which
  // handleCommand() alone bypasses -- matches how a real keypress
  // actually cancels out of this state.
  REQUIRE(app.feedByte(0x07, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);

  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // opens plain.txt -- auto-records it into RecentFile
  REQUIRE(app.handleCommand(Command::Back));

  REQUIRE(app.handleCommand(Command::StartFileHistory));
  REQUIRE(app.feedByte('x', std::nullopt));
  CHECK(display.standoutAt(23, 78));
  CHECK_FALSE(display.standoutAt(23, 79));
  CHECK_FALSE(display.standoutAt(23, 15));
}

TEST_CASE_FIXTURE(Fixture, "K-46/K-48 (^X^F/^X^D): plain ^R while still just BROWSING the read-only candidate "
                           "list (not yet editing) toggles sorted<->chronological via feedByte()'s real dispatch "
                           "path, instead of being misrouted into feedRegexListByte()'s \"start editing\" "
                           "fallthrough (2026-08-18 fix, a real report + screenshots =cz/=da: byte 0x12 wasn't in "
                           "feedRegexListByte()'s own nav whitelist, so it fell through to treating ^R as an "
                           "ordinary typed character, which activated a real prompt and then re-fed 0x12 through "
                           "feedPromptByte()'s OWN unrelated ^R handler -- startPromptHistoryRecall() with its "
                           "default directEntry=false -- silently clobbering promptHistoryRecallIsDirectEntry_ "
                           "back to false and breaking the very next ^G, =da's \"一覧が無い\")") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDirectoryHistory));
  CHECK(app.activeContentView().modeLineText().rfind("DIR ", 0) == 0);

  REQUIRE(app.feedByte(0x12, std::nullopt));  // plain ^R, via the real per-byte dispatch path
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);  // still browsing, NOT dropped into real editing
  CHECK(app.activeContentView().modeLineText().rfind("HISTORY ", 0) == 0);
  // Trailing field: legacy's own T_hdir shows the ORIGINAL category
  // ("DIR") here once toggled to chronological, not the destination
  // path drawPromptLabel() otherwise shows -- a direct user correction:
  // "^X^D→^Rは戻り値DIRでないと。旧版はそうなってるんで". Checked
  // against the real rendered row (drawPromptLabel()'s own screen
  // writes), not modeLineText() (a separate, independently-computed
  // getter that happens to already agree, but doesn't exercise this
  // function's own trailing-field logic at all).
  {
    const std::string &row23 = display.rowText(23);
    auto dirPos = row23.find("DIR");
    CHECK(dirPos != std::string::npos);
    // Left-justified, not right-flush against the screen edge (2026-08-18
    // follow-up, a direct user correction + screenshot =db: "戻り値の
    // DIRは左詰めでよろしく") -- everything after "DIR" through the true
    // last column (79, left unmarked by the base fill's own auto-wrap
    // workaround) must be blank padding, not more text crowded up
    // against the edge.
    if (dirPos != std::string::npos) {
      std::string afterDir = row23.substr(dirPos + 3, row23.size() - dirPos - 3 - 1);
      CHECK(afterDir.find_first_not_of(' ') == std::string::npos);
    }
  }

  REQUIRE(app.feedByte(0x12, std::nullopt));  // toggling again returns to the sorted "DIR" view
  CHECK(app.activeContentView().modeLineText().rfind("DIR ", 0) == 0);

  // ^G must land straight back on the real DirectoryMode screen (=cx/=cy's
  // own fix from the previous round), NOT reopen a blank ^X^D prompt --
  // this is exactly the regression =da caught: promptHistoryRecallIsDirectEntry_
  // getting clobbered by the misrouted ^R above would make this fall
  // through to returnFromListBrowser()'s general activatePrompt() branch.
  REQUIRE(app.feedByte(0x07, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.activeContentView().modeLineText().rfind("DIR ", 0) != 0);  // NOT the ^X^D prompt's own "DIR" label
}

TEST_CASE_FIXTURE(Fixture, "K-46/K-48 (^X^F/^X^D): ^G while the ^R chronological \"HISTORY\" view is showing pops "
                           "back to the underlying sorted \"DIR\"/\"FILE\" screen FIRST, not straight past it to "
                           "the mode ^X^D/^X^F was launched from -- confirmed against a freshly-built copy of the "
                           "legacy C binary itself (old/src), which shows exactly this two-^G shape for "
                           "^X^D->^R->^G->^G (2026-08-18 fix, a direct user correction: \"^X^D→^R→^G（DIR一覧から"
                           "来たんだからそこへ戻るのも当たり前）...現状そうなっていない（^GでDIR一覧、じゃなくて"
                           "(ls)へ戻る）\" -- the earlier =cx/=cy fix's early-return branch jumped straight to "
                           "promptHistoryRecallReturnMode_ regardless of listBrowserChronological_, skipping this "
                           "intermediate screen entirely)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDirectoryHistory));
  CHECK(app.activeContentView().modeLineText().rfind("DIR ", 0) == 0);

  REQUIRE(app.feedByte(0x12, std::nullopt));  // ^R: DIR -> HISTORY (chronological)
  CHECK(app.activeContentView().modeLineText().rfind("HISTORY ", 0) == 0);

  // First ^G: pops the toggle back to sorted "DIR", stays in ListBrowser --
  // does NOT touch promptHistoryRecallKind_/currentMode_ at all.
  REQUIRE(app.feedByte(0x07, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK(app.activeContentView().modeLineText().rfind("DIR ", 0) == 0);

  // Second ^G: NOW actually exits back to the real DirectoryMode screen.
  REQUIRE(app.feedByte(0x07, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.activeContentView().modeLineText().rfind("DIR ", 0) != 0);
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

TEST_CASE_FIXTURE(Fixture, "List Browser mode-line: shows the RingCategory label as name (left, unparenthesized) "
                           "and the highlighted item as trailing (2026-08-02 user report: \"^X^F（FILE）が"
                           "置き換わってない\"; 2026-08-03 follow-up: legacy puts the category on the LEFT "
                           "unparenthesized -- \"FILE 1/5 END /path\", not \"(FILE)\" trailing after the path, "
                           "the parens being reserved for the PROGRAM-name field elsewhere)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.handleCommand(Command::Back));

  REQUIRE(app.handleCommand(Command::StartFileHistory));
  std::string text = app.activeContentView().modeLineText();
  CHECK(text.rfind("FILE ", 0) == 0);                                     // RingCategory label -- as name, left
  CHECK(text.find((root / "plain.txt").string()) != std::string::npos);  // highlighted item -- as trailing, right

  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.handleCommand(Command::StartDirectoryHistory));
  CHECK(app.activeContentView().modeLineText().rfind("DIR ", 0) == 0);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(StartFileHistory): reachable from OUTSIDE Directory mode too "
                           "(2026-08-12 fix, same as StartDirectoryHistory's own identical correction just "
                           "above -- both share startListBrowser())") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  CHECK(app.handleCommand(Command::StartFileHistory));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
}

TEST_CASE_FIXTURE(Fixture, "handleCommand(StartFileHistory) mode line: a highlighted item whose OWN path is "
                           "too long to fit is middle-ellipsis truncated, not dropped entirely (2026-08-17 "
                           "fix, =bw: a real report + reproduction -- ^J^L search -> open a LOCATE result -> "
                           "^X^F -- found the mode line falling back to bare \"FILE 1/1 END\" with the path "
                           "gone, because `trailing` had no truncation option at all before TrailingKind "
                           "existed, unlike name/trailing2)") {
  std::string longPath =
      "/Applications/Some Very Long Application Name/Contents/Frameworks/SomeFramework.framework/"
      "Versions/A/Resources/deeply/nested/path/that/does/not/fit/at/all/target-file.txt";
  history.pushRecent(RingCategory::RecentFile, longPath);

  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFileHistory));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);

  std::string modeLine = app.activeContentView().modeLineText();
  CHECK(modeLine.substr(0, 4) == "FILE");            // the category name still shows
  std::size_t ellipsisPos = modeLine.find("...");
  REQUIRE(ellipsisPos != std::string::npos);          // the path was truncated, not dropped
  CHECK(modeLine.find("/Applications") != std::string::npos);  // the HEAD survived (middle-ellipsis, not tail-only)
  CHECK(modeLine.substr(modeLine.size() - 9) == "-file.txt");  // the TAIL survived too
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

// 2026-08-13 (CLAUDE.md history item 292's ^G policy, DIR half): a
// user-specified policy -- "「選択後の画面」以降のDIR...については
// 履歴を廃棄して真っ新から始める...^Gの連打もDIRならカレントおよび
// 以前のディレクトリを行き来" -- picking a directory via the ^X^D
// history picker should discard whatever multi-level descent chain
// was active before, and ^G afterward should just toggle between the
// picked directory and wherever the picker was invoked FROM (not
// resurrect the old chain). See DirectoryMode::resetNavigationToFloor()'s
// own comment for the mechanism (ModeStack's pre-existing, previously
// unused pushFloor()/isFloor()).
TEST_CASE_FIXTURE(Fixture, "DIR Select (^X^D): picking a directory discards a prior descent chain, "
                           "^G then toggles current<->previous") {
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));  // descends into subdir/ -- a real chain on the DIR ModeStack
  CHECK(app.directoryMode().canGoBack());

  history.pushRecent(RingCategory::RecentDirectory, root.string() + "/");
  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte(0x04, std::nullopt));  // ^D: StartDirectoryHistory (K-48)
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  for (int i = 0; i < static_cast<int>(app.listBrowserMode().items().size()); i++) {
    if (app.listBrowserMode().items()[i] == root.string() + "/") {
      app.activeContentView().setCursorIndex(i);
      break;
    }
  }
  REQUIRE(app.handleCommand(Command::Enter));  // picks root -- back to path(), previous == subdir/
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.directoryMode().currentDirectory() == path());

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.directoryMode().currentDirectory() == (root / "subdir").string() + "/");
  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.directoryMode().currentDirectory() == path());
  // Never exhausts -- the OLD descent chain (subdir/'s own parent
  // frame from the Enter above) is gone, replaced by this fresh
  // toggling floor, matching resetNavigationToFloor()'s own contract.
  CHECK(app.directoryMode().canGoBack());
}

TEST_CASE_FIXTURE(Fixture, "feedByte: ^X^F/^X^D/^J^R all work from CUT mode too, not just Directory "
                           "(2026-08-12 fix -- a real report: tried ^J^C into CUT, then found these didn't "
                           "work there, matching a broader design question -- \"^J^D/^J^F自体、どのモード"
                           "からでも（^Rも含め）移行可能な仕様が目的\" (the whole point of these keys is to "
                           "be reachable from ANY mode) -- see startListBrowser()'s own comment for the "
                           "investigation confirming this was ONE narrow Directory-only guard, not scattered "
                           "ad-hoc restrictions needing a mode-by-mode redesign; ^X^F itself moved back from "
                           "^J^F on 2026-09-09, see leaderXPending_'s own switch comment)") {
  REQUIRE(app.start(path()));
  history.pushRecent(RingCategory::RecentFile, "/some/file.txt");
  history.pushRecent(RingCategory::RecentDirectory, "/some/dir/");
  history.pushRecent(RingCategory::ShellCommand, "echo hi");
  REQUIRE(app.handleCommand(Command::StartCutFixed));  // ^Jc: blank prompt
  feedText("plain");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);

  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  CHECK(app.feedByte(0x06, std::nullopt));    // ^F: StartFileHistory (K-46)
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Cut);  // back to CUT, not Directory

  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  CHECK(app.feedByte(0x04, std::nullopt));    // ^D: StartDirectoryHistory (K-48)
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Cut);

  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  CHECK(app.feedByte(0x12, std::nullopt));    // ^R: StartCommandHistory (K-50, ^J^R)
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
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

// 2026-09-08、直接のユーザー依頼("^Je⇒e（←lessと同様）")によるキー
// 再編でプレフィックス無しの単一文字'e'へ移行。
TEST_CASE_FIXTURE(Fixture, "feedByte: e starts EDIT (StartEdit)") {
  configuration.loadRcText("EDITOR\n sh -c 'echo \"EDITED:$0\" >> \"$1\"'\n", true);
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

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

TEST_CASE_FIXTURE(Fixture, "EDIT: passes the raw (unwrapped) line number as +N, not the inflated "
                           "wrapped-display-row count -- 2026-09-08, a direct user report: \"usilic "
                           "-c ~/git/usilic で起動→CLAUDE.md選択→^[97^M→e→viが起動→^G / usilic画面"
                           "では98行だった行が97として表示されている\" -- a single long raw line "
                           "that wraps across multiple 80-column display rows was silently inflating "
                           "every line number after it (PipeBuffer::wrapLine()'s wrap continuations "
                           "were being counted as separate lines by ContentView::cursorIndex(), see "
                           "StyledLine::rawLineNumber/ContentView::Line::rawLineNumber)") {
  {
    std::ofstream out(root / "wraptest.txt");
    out << "raw line one\n";
    out << std::string(200, 'x') << "\n";  // wraps across multiple 80-column rows
    out << "raw line three\n";
  }
  configuration.loadRcText("EDITOR\n sh -c 'echo \"EDITED:$0\" >> \"$1\"'\n", true);
  REQUIRE(app.start(path()));
  int idx = indexOf("wraptest.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  // ^[3<Enter>: goto-line 3 (the RAW line number) -- must land on "raw
  // line three", not get stuck mid-wrap on the long line.
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  app.feedByte('3', std::nullopt);
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  // Confirm the long line genuinely did wrap (i.e. this test actually
  // exercises the bug): the wrapped-row cursor index must be > 2 even
  // though we asked for raw line 3.
  CHECK(app.activeContentView().cursorIndex() > 2);
  bool sawRawLineThree = false;
  for (int row = 0; row < display.rows(); row++)
    if (display.rowText(row).find("raw line three") != std::string::npos) sawRawLineThree = true;
  CHECK(sawRawLineThree);

  REQUIRE(app.handleCommand(Command::StartEdit));
  std::ifstream in(root / "wraptest.txt");
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(content.find("EDITED:+3") != std::string::npos);  // the real raw line number, not the wrapped row+1
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

// 2026-08-17 addition: a direct user request ("ディレクトリ上での^Jeは
// アンダーラインエラーメッセージ（適切なやつ）をモードラインのモード名
// 位置に表示") replaced the previous silent no-op with legacy do_edit()'s
// own S_errv message ("Invalid mode", str.c), underlined, in place of the
// mode line's name field (see DirectoryMode::reportEditError()'s own
// comment) -- so handleCommand() now returns true (handled, shown
// feedback) instead of false.
TEST_CASE_FIXTURE(Fixture, "EDIT: shows an underlined \"Invalid mode\" error in the mode line's name slot "
                           "when pressed on a directory entry, instead of silently doing nothing") {
  configuration.loadRcText("EDITOR\n sh -c 'echo \"EDITED:$0\" >> \"$1\"'\n", true);
  REQUIRE(app.start(path()));
  int idx = indexOf("subdir/");
  REQUIRE(idx >= 0);
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  std::string beforeRow = display.rowText(23);
  int nameWidth = static_cast<int>(app.directoryMode().currentDirectory().size());

  CHECK(app.handleCommand(Command::StartEdit));
  CHECK(app.currentMode() == ApplicationMode::Directory);  // never switches mode
  std::string expected = "Invalid mode";
  CHECK(display.rowText(23).substr(0, expected.size()) == expected);
  CHECK(display.underlineAt(23, 0));
  CHECK(display.underlineAt(23, static_cast<int>(expected.size()) - 1));
  CHECK_FALSE(display.standoutAt(23, 0));  // underlined, NOT reverse-video (see reportEditError()'s own comment)

  // 2026-08-18 addition: a direct follow-up report ("同じ幅（27文字）分
  // スペースでパディングすれば行数その他の位置がズレないので") found
  // that the shorter replacement string, left unpadded, shifted every
  // field after it (byte count, "(ls)", "usilic v1.5b") left by the
  // difference between "Invalid mode"'s own length and the directory
  // name's. Padded to the directory name's width instead (see
  // reportEditError()'s own comment), so everything from the field
  // separator onward is byte-for-byte identical to the un-errored row.
  CHECK(display.rowText(23).substr(nameWidth + 1) == beforeRow.substr(nameWidth + 1));
  // The padding itself is part of the underlined name field, not a
  // plain trailing space -- covers the =ci report's "no underline under
  // the space" complaint for this padding too, not just the message's
  // own internal space.
  CHECK(display.underlineAt(23, nameWidth - 1));

  // Consumed after exactly one redraw -- the very next mode-line render
  // (e.g. a plain cursor move) reverts to the ordinary directory path.
  // (MoveUp itself changes the position field, e.g. "15/15 END" ->
  // "14/15", so only the name field -- not the whole row -- is
  // compared back against beforeRow.)
  app.handleCommand(Command::MoveUp);
  std::string afterRow = display.rowText(23);
  CHECK(afterRow.substr(0, expected.size()) != expected);
  CHECK(afterRow.substr(0, nameWidth) == beforeRow.substr(0, nameWidth));
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

TEST_CASE_FIXTURE(Fixture, "handleResize: reflow preserves the cursor's RAW line number, not its wrapped-row "
                           "index -- 2026-09-08, a direct user report: after ^[97<Enter>e (jump to raw line "
                           "97, launch external editor) and returning without saving, only under -g (a "
                           "window resize -> reflow() happens between the jump and the return) the cursor "
                           "landed on line 103 instead of 97, and a subsequent ^L then centered on the wrong "
                           "line. Root cause: FileMode::reflow() re-wrapped content at the new width but "
                           "preserved the cursor by its OLD numeric wrapped-row index instead of translating "
                           "via ContentView::Line::rawLineNumber (see FileMode::rawLineNumberAt()/"
                           "translateCursorAfterReflow()).") {
  {
    std::ofstream out(root / "wraptest2.txt");
    out << "raw line one\n";
    out << std::string(200, 'x') << "\n";  // wraps across multiple display rows at 80 columns
    out << "raw line three\n";
  }
  REQUIRE(app.start(path()));
  int idx = indexOf("wraptest2.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  // ^[3<Enter>: goto-line 3 (the RAW line number) -- lands on "raw line three".
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  app.feedByte('3', std::nullopt);
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  const std::vector<fm::ContentView::Line> &beforeLines = app.activeContentView().lines();
  int beforeCursor = app.activeContentView().cursorIndex();
  REQUIRE(beforeCursor < static_cast<int>(beforeLines.size()));
  REQUIRE(beforeLines[beforeCursor].rawLineNumber);
  CHECK(*beforeLines[beforeCursor].rawLineNumber == 3);

  // Resizing narrower changes how the 200-char line wraps, shifting every
  // wrapped-row index after it -- reflow() must re-resolve the cursor by
  // raw line number rather than clamping the old numeric index.
  display.setSize(24, 40);
  app.handleResize();

  const std::vector<fm::ContentView::Line> &afterLines = app.activeContentView().lines();
  int afterCursor = app.activeContentView().cursorIndex();
  REQUIRE(afterCursor < static_cast<int>(afterLines.size()));
  REQUIRE(afterLines[afterCursor].rawLineNumber);
  CHECK(*afterLines[afterCursor].rawLineNumber == 3);  // must still be raw line 3, not shifted to some other row

  bool sawRawLineThree = false;
  for (int row = 0; row < display.rows(); row++)
    if (display.rowText(row).find("raw line three") != std::string::npos) sawRawLineThree = true;
  CHECK(sawRawLineThree);
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

  // Switch back to Shell and confirm its command-line field's row
  // really is the NEW viewport's own fixed row (rows - 2 = 8),
  // confirming Application actually propagated the resize down to
  // Shell. commandLineRow() is a fixed constant again (2026-08-04, see
  // its own comment), so this is an exact check now, not just a bound
  // -- the OLD, larger viewport's own row (24 - 2 = 22) would still be
  // in effect instead if the resize had failed to reach Shell.
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

TEST_CASE_FIXTURE(Fixture, "HELP: CutContent's mode-line shows the fixed name \"HELP\" and this segment's own "
                           "byte size, matching FILE/DIR's own richer name+trailing format instead of a bare "
                           "position (2026-08-04 user report/screenshot: real legacy shows \"HELP 01/48 47% "
                           "2618  ...\")") {
  REQUIRE(app.loadHelpText(testHelpText()));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartHelp));
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::CutContent);

  std::string text = app.activeContentView().modeLineText();
  CHECK(text.find("HELP") != std::string::npos);
  std::size_t expectedBytes = 0;
  for (const auto &line : app.activeContentView().lines()) expectedBytes += line.text.size() + 1;
  CHECK(text.find(std::to_string(expectedBytes)) != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "HELP: CutContent's mode-line trailing ALSO shows the current section's own label "
                           "alongside the byte size (2026-08-04 user correction: a second look at the same "
                           "screenshot showed \"2618\" and \"1.  Introduction\" both present on the real legacy "
                           "mode-line -- the byte count doesn't get replaced by the chapter label, both appear "
                           "together)") {
  REQUIRE(app.loadHelpText(testHelpText()));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartHelp));
  REQUIRE(app.handleCommand(Command::Enter));  // selects the first section, "  11. Section One"
  REQUIRE(app.currentMode() == ApplicationMode::CutContent);

  std::string text = app.activeContentView().modeLineText();
  std::size_t expectedBytes = 0;
  for (const auto &line : app.activeContentView().lines()) expectedBytes += line.text.size() + 1;
  CHECK(text.find(std::to_string(expectedBytes)) != std::string::npos);  // byte count still present...
  CHECK(text.find("11. Section One") != std::string::npos);              // ...alongside the section's own label
}

TEST_CASE_FIXTURE(Fixture, "HELP: CutContent shows no whole-line reverse-video highlight on the current section's "
                           "heading, matching FileMode's own plain-content behavior (2026-08-04 user report: "
                           "legacy's \"1.  Introduction\" heading line is plain text, not reverse-video, since "
                           "CutContent -- like plain file content -- isn't a selectable list the way the CUT/HELP "
                           "segment-label list itself is)") {
  REQUIRE(app.loadHelpText(testHelpText()));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartHelp));
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::CutContent);

  CHECK_FALSE(app.activeContentView().cursorHighlightEnabled());
  for (int row = 0; row < display.rows() - 1; row++)
    for (int col = 0; col < display.columns(); col++) CHECK_FALSE(display.standoutAt(row, col));
}

TEST_CASE_FIXTURE(Fixture, "CUT (real, File-origin): CutContent's mode-line shows the originating file's own "
                           "bare name, not \"HELP\"") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.handleCommand(Command::StartCutFixed));
  feedText("marker");
  REQUIRE(app.feedByte(0x0d, std::nullopt));

  REQUIRE(app.handleCommand(Command::Enter));  // select segment 0
  REQUIRE(app.currentMode() == ApplicationMode::CutContent);
  std::string text = app.activeContentView().modeLineText();
  CHECK(text.find("log.txt") != std::string::npos);
  CHECK(text.find("HELP") == std::string::npos);
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

TEST_CASE_FIXTURE(Fixture, "HELP (cutView_, the TOC level): mode-line shows the TOC LISTING's own byte size "
                           "(what's actually on screen), not HELP.jp's much larger total, plus the "
                           "originating directory, padded to the row's edge (2026-08-23 fix, a real report + "
                           "reference: \"HELP\" \"01/26 TOP\" \"<byte count>\" \"/path/    \"; corrected "
                           "2026-08-24 -- a direct user report the FIRST version showed the whole file's own "
                           "size instead of the TOC's, e.g. \"413\" expected, the much larger whole-file size "
                           "shown instead)") {
  REQUIRE(app.loadHelpText(testHelpText()));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartHelp));
  CHECK(app.currentMode() == ApplicationMode::Cut);

  std::size_t tocBytes = app.activeContentView().totalByteCount();
  CHECK(tocBytes != testHelpText().size());  // the TOC listing is a strict subset of the whole file
  std::string text = app.activeContentView().modeLineText();
  CHECK(text.find(std::to_string(tocBytes)) != std::string::npos);
  CHECK(text.find(path()) != std::string::npos);
}

// 2026-08-24 fix, a direct user report ("HELPの1. Introductionページ
// 上で^S→戻り値不良、正しくは「CUT:1.  Introduction」"): grepView_'s
// own return-destination field only ever populated for
// grepReturnMode_==Directory, leaving it blank for every other origin
// (File, CutContent -- e.g. FGREP run from within a HELP chapter's own
// content). CutContent mirrors cutContentView_'s own "CUT:<segment
// label>" convention.
TEST_CASE_FIXTURE(Fixture, "FGREP from within a HELP chapter's own content: mode line shows \"CUT:<chapter "
                           "label>\" as the return-destination field, not blank") {
  REQUIRE(app.loadHelpText(testHelpText()));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartHelp));
  REQUIRE(app.currentMode() == ApplicationMode::Cut);
  REQUIRE(app.handleCommand(Command::Enter));  // select "11. Section One"
  REQUIRE(app.currentMode() == ApplicationMode::CutContent);

  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("content");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);

  CHECK(display.rowText(23).find("CUT:11. Section One") != std::string::npos);
}

// 2026-08-24 fix (same report, its OWN follow-up correction): the
// FIRST version of this fix only touched grepView_'s own POST-
// submission mode line -- but the user's actual repro hits ^J^W WHILE
// STILL TYPING the pattern, before ever pressing Enter, i.e. while
// drawPromptLabel()'s own prompt-time "destinationDirectory" field is
// what's on screen (a SEPARATE mechanism, computed via
// previousModeName(grepReturnMode_) -- which has no per-instance CUT
// segment state, so it only ever gave the bare "CUT"). Now uses
// cutContentReturnLabel() there too, the same helper.
TEST_CASE_FIXTURE(Fixture, "FGREP prompt (not yet submitted) from within a HELP chapter: the prompt row's "
                           "own return-destination field already shows \"CUT:<chapter label>\", not the "
                           "bare \"CUT\"") {
  REQUIRE(app.loadHelpText(testHelpText()));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartHelp));
  REQUIRE(app.handleCommand(Command::Enter));  // "11. Section One"
  REQUIRE(app.currentMode() == ApplicationMode::CutContent);

  REQUIRE(app.handleCommand(Command::StartFgrep));  // ^S -- prompt now active, currentMode_ still CutContent
  REQUIRE(app.currentMode() == ApplicationMode::CutContent);
  REQUIRE(app.promptActive());

  CHECK(display.rowText(23).find("CUT:11. Section One") != std::string::npos);
}

// 2026-08-24 fix, a real report + the user's own careful trace: ^J^W
// pressed WHILE an editable prompt (FGREP, mid-pattern, not yet
// submitted) is still active jumps to Shell correctly, but
// dispatchByte()'s own top-level routing checks `promptKind_ !=
// PromptKind::None` BEFORE `currentMode_ == Shell` -- promptKind_
// staying stuck at GrepPattern meant every byte typed at the live
// SHELL prompt afterward kept being silently re-routed through
// feedPromptByte() instead of feedShellByte(), including a bare
// Enter (which re-submitted the STALE FGREP pattern via
// executeGrep(), which unconditionally overwrites currentMode_ back
// out of Shell entirely) -- exactly the user's own "^J^WはHELPに
// 戻っているようでいて...実はSHELLにいる" confusion.
TEST_CASE_FIXTURE(Fixture, "^J^W pressed while an editable FGREP prompt is still active (not yet submitted) "
                           "correctly hands SUBSEQUENT keystrokes to the real SHELL, not back to the stale "
                           "prompt") {
  REQUIRE(app.loadHelpText(testHelpText()));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartHelp));
  REQUIRE(app.handleCommand(Command::Enter));  // "11. Section One"
  REQUIRE(app.currentMode() == ApplicationMode::CutContent);

  REQUIRE(app.handleCommand(Command::StartFgrep));  // ^S
  feedText("content");
  REQUIRE(app.promptActive());

  bool ok = app.feedByte(0x0a, std::nullopt);  // ^J
  ok = app.feedByte(0x17, std::nullopt) && ok;  // ^W -- shellInsertBufferContent(), while the prompt is STILL active
  if (!ok) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Shell);
  CHECK_FALSE(app.promptActive());  // promptKind_ must be cleared, not left stuck
  REQUIRE(pumpUntil(app, [&] { return !app.shell().currentDirectory().empty(); }));  // handshake complete

  // The critical assertion: a bare Enter now reaches the REAL shell
  // (submitting the pre-filled "cat <w.N>" command), not
  // feedPromptByte()'s own submitPrompt() (which would re-run the
  // stale "content" FGREP search and snap currentMode_ back out of
  // Shell entirely).
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Shell);  // still in Shell -- NOT bounced back to Grep/CutContent

  bool sawCatOutput = pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("Section one content") != std::string::npos) return true;
    return false;
  });
  CHECK(sawCatOutput);  // the real `cat` command actually ran

  // ^G restores the suspended FGREP prompt, still mid-pattern.
  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::CutContent);
  CHECK(app.promptActive());
  CHECK(app.promptText() == "content");
}

// 2026-08-24 fix, a direct user report + their own diagnosis:
// "^J^Wは現在表示データのcatだが、FGREPの守備範囲はモードラインだけ
// なので同モードの^J^Wは本来意味が無い...見た目で言えば背景の
// Introductionページがcatされるのが自然". FGREP/EGREP results are
// just a filtered VIEW over whatever the search ran FROM
// (grepReturnMode_) -- catting grepView_'s own matched-only lines lost
// everything that didn't match. shellInsertBufferContent() now
// substitutes grepReturnMode_ for currentMode_==Grep, both for naming
// AND content, so ^J^W from a Grep results screen writes the ORIGIN's
// full content instead.
TEST_CASE_FIXTURE(Fixture, "^J^W from a Grep results screen writes the ORIGIN's own full content, not just "
                           "the matched lines") {
  REQUIRE(app.loadHelpText(testHelpText()));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartHelp));
  REQUIRE(app.handleCommand(Command::Enter));  // "11. Section One"
  REQUIRE(app.currentMode() == ApplicationMode::CutContent);

  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("line A");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);
  REQUIRE(app.activeContentView().lines().size() == 1);  // only "line A" matched, not "line B"

  if (!app.handleCommand(Command::ShellInsertBufferContent)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Shell);

  std::string commandText = app.shell().commandLine().text();  // "cat '<path>'"
  REQUIRE(commandText.substr(0, 5) == "cat '");
  REQUIRE(commandText.back() == '\'');
  std::ifstream in(commandText.substr(5, commandText.size() - 6));
  REQUIRE(in.good());
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(content.find("line A") != std::string::npos);
  CHECK(content.find("line B") != std::string::npos);  // the FULL chapter, not just the matched line
}

// 2026-08-23 fix, a real report: "^J^H→...→続いて^G^G→戻り値不良
// 「CUT: ^  [0-9]+¥.」（「usilic 1.5b」が正解）" -- returnFromCut() is
// shared by every CutMode-backed screen that can return to Directory,
// not just a real user-initiated ^J^C search; HELP reuses the exact
// same machinery internally (kHelpPattern's own section-heading
// regex), and backing all the way out of it used to leave that
// internal pattern behind as a "CUT: <pattern>" search annotation.
TEST_CASE_FIXTURE(Fixture, "HELP: backing all the way out to Directory does NOT leave a stale \"CUT: "
                           "<internal pattern>\" search annotation behind") {
  REQUIRE(app.loadHelpText(testHelpText()));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartHelp));
  CHECK(app.currentMode() == ApplicationMode::Cut);

  REQUIRE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);

  CHECK(app.activeContentView().modeLineText().find("CUT:") == std::string::npos);
  CHECK(display.rowText(23).find("usilic v1.5b") != std::string::npos);  // the normal watermark is back, not squeezed out
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
    if (display.rowText(row).find("画面を1行下へ") != std::string::npos) sawFirstEntry = true;
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
    // Leading space before "^S" (front-padding, since "^S" is the whole label) distinguishes this row from
    // "^J^S" (EGREP)'s own row, which also contains "^S :" as a substring but with no space before it.
    if (lines[i].text.find(" ^S :") != std::string::npos) row = static_cast<int>(i);
  REQUIRE(row >= 0);
  app.activeContentView().setCursorIndex(row);

  CHECK(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Directory);  // FGREP is a prompt, not a mode switch
  CHECK(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "Key: Enter on the '^X^C' entry (EXIT) chains a full multi-byte leader sequence "
                           "(^X leader -> ^C sub-command)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  const auto &lines = app.activeContentView().lines();
  int row = -1;
  for (std::size_t i = 0; i < lines.size(); i++)
    if (lines[i].text.find("^X^C :") != std::string::npos) row = static_cast<int>(i);
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
    if (lines[i].text.find("^X^C :") != std::string::npos) row = static_cast<int>(i);  // EXIT's own row -- would be very visible if wrongly executed
  REQUIRE(row >= 0);
  app.activeContentView().setCursorIndex(row);

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK_FALSE(app.promptActive());  // NOT the EXIT prompt -- Back never executed the highlighted entry
}

// -- KEY: dual "execute"/"reference" action per row (2026-08-31) --

TEST_CASE_FIXTURE(Fixture, "Key: Left/Right toggles between execute/reference, and Enter dispatches accordingly") {
  REQUIRE(app.loadHelpText(fm::expandHelpMarkup(fm::kHelpTextJa)));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  const auto &lines = app.activeContentView().lines();
  int row = -1;
  for (std::size_t i = 0; i < lines.size(); i++)
    if (lines[i].text.find(" ^S :") != std::string::npos) row = static_cast<int>(i);
  REQUIRE(row >= 0);
  app.activeContentView().setCursorIndex(row);

  // Default focus is "execute" (the key label side): Enter runs the binding as before.
  CHECK(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.promptActive());
  REQUIRE(app.feedByte(0x07, std::nullopt));  // ^G: cancel the FGREP prompt this just opened (via the
                                               // real dispatch path -- handleCommand(Back) alone doesn't
                                               // check promptKind_, see dispatchByte()'s own early check)
  REQUIRE_FALSE(app.promptActive());

  // Re-open KEY, move to the same row, flip focus to "reference" (the description side) with Right.
  REQUIRE(app.handleCommand(Command::StartKey));
  app.activeContentView().setCursorIndex(row);
  REQUIRE(app.feedByte(0, KeyInput::SpecialKey::Right));
  CHECK(app.handleCommand(Command::Enter));
  // ^S's helpSection is "2.3" (FGREP/EGREP) -- landing directly in that section's content.
  CHECK(app.currentMode() == ApplicationMode::CutContent);
  CHECK_FALSE(app.promptActive());  // reference view only, nothing was executed

  // ^G twice: back to the HELP table of contents, then back to KEY itself
  // (showKeyReference() opens HELP fresh from Key mode, so cutReturnMode_ is Key).
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Cut);
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Key);
}

TEST_CASE_FIXTURE(Fixture, "Key: Left brings focus back to execute from reference") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  REQUIRE(app.feedByte(0, KeyInput::SpecialKey::Right));  // focus reference
  REQUIRE(app.feedByte(0, KeyInput::SpecialKey::Left));   // back to execute
  // The first row is "j" -- executing it should behave exactly like the pre-redesign
  // KEY screen (scroll the window down), not open any HELP reference.
  CHECK(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK_FALSE(app.promptActive());
}

TEST_CASE_FIXTURE(Fixture, "Key: reference view is a no-op for entries with no mapped HELP section") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  const auto &lines = app.activeContentView().lines();
  int row = -1;
  for (std::size_t i = 0; i < lines.size(); i++)
    if (lines[i].text.find("^X( :") != std::string::npos) row = static_cast<int>(i);  // macro recording -- no HELP section
  REQUIRE(row >= 0);
  app.activeContentView().setCursorIndex(row);
  REQUIRE(app.feedByte(0, KeyInput::SpecialKey::Right));  // focus "reference"

  // No mapped HELP section for this entry -- showKeyReference() itself returns false,
  // and nothing about the current mode changes.
  CHECK_FALSE(app.handleCommand(Command::Enter));
  CHECK(app.currentMode() == ApplicationMode::Key);  // stayed put, nothing opened
}

TEST_CASE_FIXTURE(Fixture, "Key: clicking the description half of a row opens that row's HELP section "
                           "directly, regardless of the current Left/Right focus") {
  REQUIRE(app.loadHelpText(fm::expandHelpMarkup(fm::kHelpTextJa)));
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  const auto &lines = app.activeContentView().lines();
  int row = -1;
  for (std::size_t i = 0; i < lines.size(); i++)
    if (lines[i].text.find(" ^S :") != std::string::npos) row = static_cast<int>(i);
  REQUIRE(row >= 0);
  // Navigate there via real MoveDown commands (not a direct setCursorIndex() poke) so the
  // on-screen rendering -- and its label/description column split -- is actually up to date.
  for (int i = 0; i < row; i++) app.handleCommand(Command::MoveDown);
  app.activeContentView().redraw();

  int displayRow = -1;
  for (int r = 0; r < display.rows() - 1; r++)
    if (display.rowText(r).find(" ^S :") != std::string::npos) displayRow = r;
  REQUIRE(displayRow >= 0);

  std::string rendered = display.rowText(displayRow);
  // 2026-09-05変更: プレーンな^SはGREP(旧FGREP、^O^Sへ移動)を起動する
  // ようになった――kKeyTable自身のコメント参照。
  int descriptionColumn = static_cast<int>(rendered.find("GREP"));  // ^S's own description text

  REQUIRE(descriptionColumn >= 0);
  CHECK(app.handleMouseClick(displayRow, descriptionColumn));
  CHECK(app.currentMode() == ApplicationMode::CutContent);
}

TEST_CASE_FIXTURE(Fixture, "Key: clicking the label half of a row executes it directly, "
                           "same as pressing return with execute focus") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartKey));
  const auto &lines = app.activeContentView().lines();
  int row = -1;
  for (std::size_t i = 0; i < lines.size(); i++)
    if (lines[i].text.find(" ^S :") != std::string::npos) row = static_cast<int>(i);
  REQUIRE(row >= 0);
  for (int i = 0; i < row; i++) app.handleCommand(Command::MoveDown);
  app.activeContentView().redraw();

  int displayRow = -1;
  for (int r = 0; r < display.rows() - 1; r++)
    if (display.rowText(r).find(" ^S :") != std::string::npos) displayRow = r;
  REQUIRE(displayRow >= 0);
  std::string rendered = display.rowText(displayRow);
  int labelColumn = static_cast<int>(rendered.find("^S"));
  REQUIRE(labelColumn >= 0);

  CHECK(app.handleMouseClick(displayRow, labelColumn));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(app.promptActive());  // FGREP prompt -- the key was actually executed
}

// -- Recorded macros (p_km, ^X(/^X)/^Xe) --
//
// 2026-09-10, direct user request ("次はマクロ（^X(...）を実装して
// 欲しい"): the feature already existed here, just under ^O(/^O)/^Oe
// (moved there by a 2026-09-08 key reorg, per direct user request at
// the time). Once that was pointed out, the user explicitly chose
// "^X(/^X)/^Xeへ戻す(^Oは廃止)" -- move it back to ^X, ^O's own copy
// removed entirely. All the byte sequences below moved from leading
// with 0x0f (^O) to 0x18 (^X) accordingly; the actual '('/')'/'e'
// payload bytes are unchanged.

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

TEST_CASE_FIXTURE(Fixture, "recording: a real leaderO command (^Oc etc) IS recorded, unlike macro control itself") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::MoveDown));  // off "./" and "../" onto a real entry, BEFORE recording starts
  REQUIRE(app.handleCommand(Command::MoveDown));
  REQUIRE(app.handleCommand(Command::ToggleMark));  // mark BEFORE recording starts too (2026-08-27 CP redesign)
  app.feedByte(0x18, std::nullopt);
  app.feedByte('(', std::nullopt);

  app.feedByte(0x0f, std::nullopt);  // ^O
  app.feedByte('c', std::nullopt);   // c : a real CP command, not macro control
  CHECK(app.promptActive());  // ^Oc shows the destination prompt immediately now (2026-08-27 redesign)
  // Both bytes of this real ^Oc sequence are recorded -- ^O commands
  // are no longer special-cased at all now that macro control lives
  // exclusively under ^X (see feedByte()'s own comment), so this is
  // just the generic "record every byte" fallback path, same outcome.
  CHECK(app.recordedMacroLength() == 2);

  app.feedByte(0x07, std::nullopt);  // ^G : cancel the prompt
}

// 2026-09-09redesign: DU no longer prompts (runDuOnCurrentFolder()'s
// own comment) -- ^Jd alone now runs `du` and lands in Du mode
// immediately, so the recorded sequence is just ^Jd, no trailing ^M.
TEST_CASE_FIXTURE(Fixture, "recording: a ^J-leader sequence (^Jd for DU) is captured and replays correctly") {
  REQUIRE(app.start(path()));
  app.feedByte(0x18, std::nullopt);
  app.feedByte('(', std::nullopt);
  app.feedByte(0x0a, std::nullopt);  // ^J
  app.feedByte('d', std::nullopt);   // d : DU, runs immediately on the current directory
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);
  app.handleCommand(Command::Back);
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  app.feedByte(0x18, std::nullopt);
  app.feedByte(')', std::nullopt);

  app.feedByte(0x18, std::nullopt);
  app.feedByte('e', std::nullopt);  // replay: ^Jd
  waitForDu(app);
  CHECK(app.currentMode() == ApplicationMode::Du);
}

TEST_CASE_FIXTURE(Fixture, "recording: typed prompt text (a real GREP pattern) is captured and replays correctly") {
  REQUIRE(app.start(path()));
  app.feedByte(0x0f, std::nullopt);
  app.feedByte('(', std::nullopt);

  app.feedByte(0x13, std::nullopt);  // ^S : start GREP (2026-09-05: moved from FGREP)
  feedText("plain.txt");
  app.feedByte(0x0d, std::nullopt);  // submit
  // 2026-09-09: plain ^S (Basic syntax) from Directory now stays
  // in-place (see .steering/20260909-grep-inline-highlight/design.md)
  // instead of switching to ApplicationMode::Grep, so there's no
  // separate results screen to Back out of any more.
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.grepMode().matches().size() == 1);

  app.feedByte(0x0f, std::nullopt);
  app.feedByte(')', std::nullopt);

  app.feedByte(0x0f, std::nullopt);
  app.feedByte('e', std::nullopt);
  CHECK(app.currentMode() == ApplicationMode::Directory);
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
  // A column inside the entry's own highlighted span, not column 0 --
  // DirectoryMode's `ls`-style listing has a fixed left margin (see
  // ContentView::isColumnWithinLineScope()'s own comment) that
  // handleMouseClick() now deliberately ignores clicks in.
  int col = app.activeContentView().lines()[idx].highlightStartColumn.value_or(0) + 1;

  CHECK(app.handleMouseClick(idx, col));
  CHECK(app.directoryMode().selectedEntry()->name == "plain.txt");
  CHECK(app.currentMode() == ApplicationMode::Directory);  // selecting alone doesn't open it
}

TEST_CASE_FIXTURE(Fixture, "handleMouseClick: a second click on the already-selected row opens it") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  REQUIRE(idx >= 0);
  int col = app.activeContentView().lines()[idx].highlightStartColumn.value_or(0) + 1;

  REQUIRE(app.handleMouseClick(idx, col));  // first click: select
  REQUIRE(app.handleMouseClick(idx, col));  // second click on the same row: open
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == path() + "plain.txt");
}

TEST_CASE_FIXTURE(Fixture, "handleMouseClick: false outside the viewport or outside Directory/Archive/LOCATE "
                           "modes") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.handleMouseClick(999, 0));  // way past any real row

  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  CHECK_FALSE(app.handleMouseClick(0, 0));  // File mode still isn't covered (see class comment)
}

// 2026-08-26 addition: mirrors the Directory-mode click tests above
// exactly, for ArchiveMode's own listing (a direct user request:
// "内容一覧画面で左クリックで選べるようにして欲しいのとハイライト表示
// を（ls）同様ファイル・ディレクトリ名のみにして欲しい").
TEST_CASE_FIXTURE(Fixture, "handleMouseClick: ARC listing -- a click selects, a second click on the "
                           "already-selected row opens it") {
  REQUIRE(app.start(path()));
  int dirIdx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != dirIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Archive);

  int idx = archiveIndexOf(app, "inner.txt");
  REQUIRE(idx >= 0);
  int col = app.activeContentView().lines()[idx].highlightStartColumn.value_or(0) + 1;

  REQUIRE(app.handleMouseClick(idx, col));  // first click: select
  CHECK(app.currentMode() == ApplicationMode::Archive);  // selecting alone doesn't open it
  CHECK(app.archiveMode().selectedEntry()->name.find("inner.txt") != std::string::npos);

  REQUIRE(app.handleMouseClick(idx, col));  // second click on the same row: open
  CHECK(app.currentMode() == ApplicationMode::ArchiveContent);
}

// 2026-09-15 修正、直接のユーザー依頼("（ls）、LOCATE等で導入していた
// 「ドラッグエリア」も不要でいいので反映してみて"): 以前はここで
// マージン(サイズ欄)左のクリックがno-opになる(=ドラッグの開始点として
// 安全)ことを検証していたが、左クリックの「実行」自体がボタン解放まで
// 遅延するようになった(GraphicalDisplay::Impl::pendingLeftClick)ことで
// このマージン除外自体が不要になったため、handleMouseClick()側は
// isColumnWithinRowText()(上限のみ)へ緩めた――マージン上のクリックも
// 今は選択として機能することを検証する。
TEST_CASE_FIXTURE(Fixture, "handleMouseClick: ARC listing -- a click on the size-column margin also selects "
                           "now (the margin's click-scope exclusion was removed once click execution moved to "
                           "button-release)") {
  REQUIRE(app.start(path()));
  int dirIdx = indexOf("archive.tar");
  while (indexOf(app.directoryMode().selectedEntry()->name) != dirIdx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::Archive);

  int idx = archiveIndexOf(app, "inner.txt");
  REQUIRE(idx >= 0);
  int start = app.activeContentView().lines()[idx].highlightStartColumn.value_or(0);
  REQUIRE(start > 0);  // the size column margin is always real width here

  REQUIRE(app.handleMouseClick(idx, start - 1));  // one column left of the (now-informational-only) span
  CHECK(app.currentMode() == ApplicationMode::Archive);  // selecting alone doesn't open it
  CHECK(app.archiveMode().selectedEntry()->name.find("inner.txt") != std::string::npos);
}

// 2026-08-14 addition: mirrors the Directory-mode click tests above
// exactly, for LOCATE's own results (a direct user request, see
// handleMouseClick()'s own comment).
TEST_CASE_FIXTURE(Fixture, "handleMouseClick: LOCATE results -- a click selects, a second click on the "
                           "already-selected row opens it") {
  fs::path targetA = root / "click_a.txt";
  fs::path targetB = root / "click_b.txt";
  { std::ofstream(targetA) << "a"; }
  { std::ofstream(targetB) << "b"; }
  fs::path fakeLocate = root / "fake_locate_click.sh";
  {
    std::ofstream script(fakeLocate);
    script << "#!/bin/sh\necho '" << targetA.string() << "'\necho '" << targetB.string() << "'\n";
  }
  std::filesystem::permissions(fakeLocate, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
  configuration.setCommandForTest(ConfiguredRole::Locate, {{fakeLocate.string()}, std::nullopt});

  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText("anything");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  REQUIRE(app.locateMode().results().size() == 2);

  REQUIRE(app.handleMouseClick(1, 0));  // click row 1 (targetB), not yet selected -- just selects it
  CHECK(app.currentMode() == ApplicationMode::Locate);
  CHECK(app.activeContentView().cursorIndex() == 1);

  REQUIRE(app.handleMouseClick(1, 0));  // click the SAME row again -- opens it, matching Directory's convention
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == targetB.string());
}

// 2026-09-15 addition, a direct user report ("一覧上で画面ドラッグ
// できるのはDU画面だけ...本来ならLOCATEのように左クリックした時点で
// 内容表示された画面がスクロールするはずなのに、表示されないから
// 一覧画面のままスクロールしている"): handleMouseClick() had no
// ApplicationMode::Du branch at all -- every click on a DU listing
// silently fell through and did nothing, leaving the drag gesture as
// the only thing that ever fired. Mirrors the ARC listing tests above
// exactly, now that DuMode::loadUsage() also marks the size-column
// margin via Line::highlightStartColumn (same "ls"/ARC convention).
TEST_CASE_FIXTURE(Fixture, "handleMouseClick: DU listing -- a click selects, a second click on the "
                           "already-selected row opens it") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);

  int idx = -1;
  const auto &entries = app.duMode().entries();
  for (std::size_t i = 0; i < entries.size(); i++) {
    if (!entries[i].isDirectory) {
      idx = static_cast<int>(i);
      break;
    }
  }
  REQUIRE(idx >= 0);
  int col = app.activeContentView().lines()[idx].highlightStartColumn.value_or(0) + 1;

  // LocateMode自身の同種のテスト(すぐ上)の同じ注意点参照: cursorIndex_
  // の既定値は0なので、選んだidxがたまたま0だと「1回目のクリック」が
  // 既にidx==cursorIndex()を満たしてしまい、選択のみのつもりが即座に
  // オープンまで進んでしまう(このテストの本来の趣旨=2段階の検証が
  // 崩れる)。既定のカーソルから確実にずらしておく。
  if (idx == 0) {
    REQUIRE(entries.size() > 1);
    REQUIRE(app.handleCommand(Command::MoveDown));
    REQUIRE(app.duMode().selectedEntry() != &entries[idx]);
  }

  REQUIRE(app.handleMouseClick(idx, col));  // first click: select
  CHECK(app.currentMode() == ApplicationMode::Du);  // selecting alone doesn't open it
  CHECK(app.duMode().selectedEntry() == &entries[idx]);

  REQUIRE(app.handleMouseClick(idx, col));  // second click on the same row: open
  CHECK(app.currentMode() == ApplicationMode::File);
}

// 2026-09-15修正、すぐ上のARC listingテストの同じコメント参照:
// マージン除外自体が不要になったため、no-opの検証からクリックが
// 実際に効くことの検証へ反転した。idx=0はDU一覧のデフォルトの
// cursorIndex_と一致するため、この押下は(scope判定さえ通れば)
// 「1回目のクリック」ではなく「既に選択済みの行への2回目のクリック」
// 相当になりopenSelectedDuEntry()まで進む――ここでは margin除外が
// 消えたことそのものを検証したいだけなので、select/openどちらの
// 結果になるかは問わず、単に無視されない(true)ことだけを確認する。
TEST_CASE_FIXTURE(Fixture, "handleMouseClick: DU listing -- a click on the size-column margin also works now "
                           "(the margin's click-scope exclusion was removed once click execution moved to "
                           "button-release)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);

  int idx = 0;
  REQUIRE(!app.duMode().entries().empty());
  int start = app.activeContentView().lines()[idx].highlightStartColumn.value_or(0);
  REQUIRE(start > 0);  // the size column margin is always real width here

  CHECK(app.handleMouseClick(idx, start - 1));  // one column left of the (now-informational-only) span
}

// 2026-09-15修正、直接のユーザー依頼("（ls）、LOCATE等で導入していた
// 「ドラッグエリア」も不要でいいので反映してみて"): 左クリックの
// 「実行」自体がボタン解放まで遅延するようになった(GraphicalDisplay::
// Impl::pendingLeftClick自身のコメント参照)ことで、2026-08-05に
// このマージン除外を導入した理由(クリックとドラッグの区別)自体が
// 不要になった。以前はここでno-opを検証していたが、マージン上の
// クリックも今は選択として機能することを検証する。
TEST_CASE_FIXTURE(Fixture, "handleMouseClick: a click on DirectoryMode's own ls-style left margin also "
                           "selects now (the margin's click-scope exclusion was removed once click execution "
                           "moved to button-release)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  REQUIRE(idx >= 0);
  int start = app.activeContentView().lines()[idx].highlightStartColumn.value_or(0);
  REQUIRE(start > 0);  // DirectoryMode's own ls-style listing always has a real left margin

  REQUIRE(app.handleMouseClick(idx, start - 1));  // one column left of the (now-informational-only) span
  CHECK(app.currentMode() == ApplicationMode::Directory);  // selecting alone doesn't open it
  CHECK(app.directoryMode().selectedEntry()->name == "plain.txt");
}

TEST_CASE_FIXTURE(Fixture, "handleMouseClick: a press past the end of the entry's own text is also a no-op") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  REQUIRE(idx >= 0);
  int end = static_cast<int>(app.activeContentView().lines()[idx].text.size());

  CHECK_FALSE(app.handleMouseClick(idx, end));  // exactly at the end -- past the last real character
  CHECK(app.directoryMode().selectedEntry()->name != "plain.txt");
}


// -- Application::startPagerFileList() / Command::PagerNextFile etc. --
// 2026-08-25 (a direct user request, "旧版にない機能として...less同様
// 'n''p''x''f'"): main.cpp's own "通常ファイル引数" launch path --
// previously entirely unhandled (every positional CLI argument was
// always treated as a directory) -- now opens a plain FILE argument
// directly in File mode, and two or more such arguments as a
// navigable "pager file list". See Command::PagerNextFile's own
// declaration comment for the full design (mnemonics, key choices).

TEST_CASE_FIXTURE(Fixture, "startPagerFileList: refused for an empty list") {
  CHECK_FALSE(app.startPagerFileList({}));
}

TEST_CASE_FIXTURE(Fixture, "startPagerFileList: refused if the first file can't be loaded") {
  CHECK_FALSE(app.startPagerFileList({(root / "does-not-exist.txt").string()}));
}

TEST_CASE_FIXTURE(Fixture, "startPagerFileList: a single path loads it in File mode, same as an "
                           "ordinary Directory-mode Enter") {
  REQUIRE(app.startPagerFileList({(root / "plain.txt").string()}));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == (root / "plain.txt").string());
}

TEST_CASE_FIXTURE(Fixture, "startPagerFileList: n/p/x/f navigate a 3-file list, with no wrap-around "
                           "at either end") {
  std::string a = (root / "plain.txt").string();
  std::string b = (root / "log.txt").string();
  std::string c = (root / "subdir" / "sub.txt").string();
  REQUIRE(app.startPagerFileList({a, b, c}));
  CHECK(app.fileMode().currentFile() == a);

  CHECK(app.feedByte('n', std::nullopt));
  CHECK(app.fileMode().currentFile() == b);
  CHECK(app.feedByte('n', std::nullopt));
  CHECK(app.fileMode().currentFile() == c);
  CHECK_FALSE(app.feedByte('n', std::nullopt));  // already the last file -- no-op, not a wrap to the first
  CHECK(app.fileMode().currentFile() == c);

  CHECK(app.feedByte('p', std::nullopt));
  CHECK(app.fileMode().currentFile() == b);
  CHECK(app.feedByte('p', std::nullopt));
  CHECK(app.fileMode().currentFile() == a);
  CHECK_FALSE(app.feedByte('p', std::nullopt));  // already the first file -- no-op
  CHECK(app.fileMode().currentFile() == a);

  CHECK(app.feedByte('f', std::nullopt));  // jump to the last file
  CHECK(app.fileMode().currentFile() == c);
  CHECK(app.feedByte('x', std::nullopt));  // jump to the first file
  CHECK(app.fileMode().currentFile() == a);
}

TEST_CASE_FIXTURE(Fixture, "startPagerFileList: '[' and ']' double as PagerPreviousFile/PagerNextFile "
                           "while a pager list is active") {
  std::string a = (root / "plain.txt").string();
  std::string b = (root / "log.txt").string();
  REQUIRE(app.startPagerFileList({a, b}));

  CHECK(app.feedByte(']', std::nullopt));
  CHECK(app.fileMode().currentFile() == b);
  CHECK(app.feedByte('[', std::nullopt));
  CHECK(app.fileMode().currentFile() == a);
}

// 2026-08-25 bug fix: unlike ContentView's own move/scroll methods
// (which redraw() internally), FileMode::loadStream() doesn't --
// navigateToPagerFile() used to update fileMode_'s own internal state
// correctly (confirmed via currentFile()) while leaving the SCREEN
// showing the previous file's content, since nothing else in that call
// chain happened to trigger a redraw either. Checked via the actual
// rendered row text, not just currentFile(), so a regression here
// (someone removing navigateToPagerFile()'s own activeView().redraw()
// call) would be caught even if the internal-state-only assertions
// above still passed.
TEST_CASE_FIXTURE(Fixture, "startPagerFileList: navigating actually updates the RENDERED screen, not "
                           "just fileMode()'s internal currentFile()") {
  std::string a = (root / "plain.txt").string();  // "hello\nworld\n"
  std::string b = (root / "log.txt").string();    // "marker one\n..."
  REQUIRE(app.startPagerFileList({a, b}));
  CHECK(display.rowText(0).substr(0, 5) == "hello");

  REQUIRE(app.feedByte('n', std::nullopt));
  CHECK(display.rowText(0).substr(0, 10) == "marker one");

  REQUIRE(app.feedByte('p', std::nullopt));
  CHECK(display.rowText(0).substr(0, 5) == "hello");
}

// 2026-08-25 bug fix: a real PTY capture found ^G revealing an EMPTY
// Directory listing on a pager-launched file/stdin ONCE THE STREAM HAD
// FULLY FINISHED LOADING (a short `ls -l /tmp | usilic` reaches real
// EOF almost immediately) -- Command::Back's own File-mode branch had
// been gated on FileMode::hasActiveStream(), which turns false exactly
// at that point (see its own comment), instead of the EOF-independent
// FileMode::isPagerMode(). This fixture's own plain.txt is tiny enough
// to reach real EOF on the very first loadStream() call, reproducing
// the same "already fully loaded" condition directly.
TEST_CASE_FIXTURE(Fixture, "startPagerFileList: ^G stays a no-op even AFTER the file has fully loaded "
                           "(reached real EOF), not just while still streaming") {
  REQUIRE(app.startPagerFileList({(root / "plain.txt").string()}));
  REQUIRE(app.fileMode().streamAtEof());  // tiny file -- confirms this test actually reproduces the EOF case
  REQUIRE_FALSE(app.fileMode().hasActiveStream());  // the exact condition that broke the old gate

  CHECK_FALSE(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);  // did NOT fall through to an empty Directory
}

// 2026-09-06追記: この2つのテストは元々、"(Exit)"通知が一瞬の
// EAGAIN(書き込み側は開いたまま)と本物のEOFのどちらでも等しく出る
// ことを確認していたが、その通知自体を直接のユーザー報告("バイト
// 単位読み込み仕様となったSTDINではEOFと（Exit）がほぼ同時となり
// 無意味なので(stdin)では通知を無くしてほしい")を受けて削除した
// (showShellBusyNotice()自身のコメント参照)。通知の消えた今も、
// その下敷きになっているlastLoadAttemptYieldedNothing()/streamAtEof()
// 自身の区別(一瞬の取りこぼし vs 本物のEOF)は他の呼び出し元
// (ensureLoadedThrough()自身の複数回の訂正コメント参照)がまだ
// 頼っている値なので、その部分だけ引き続き確認する。
TEST_CASE_FIXTURE(Fixture, "PageDown: a pagerReturnsToShell() File screen that just tried to load more but "
                           "got nothing new YET (write end still open, not a real EOF) reports "
                           "lastLoadAttemptYieldedNothing() without having actually reached streamAtEof()") {
  REQUIRE(app.startPagerFileList({(root / "plain.txt").string()}));  // gets Application into File mode
  REQUIRE(app.currentMode() == ApplicationMode::File);

  int fds[2];
  REQUIRE(pipe(fds) == 0);
  REQUIRE(write(fds[1], "only one line\n", 14) == 14);
  // fds[1] (the write end) stays OPEN here -- NOT a real EOF, matching
  // the exact scenario this feature targets.
  app.fileMode().loadStream(fds[0], /*ownsFd=*/true, "", 100);
  app.fileMode().setPagerReturnsToShell(true);
  REQUIRE(app.fileMode().hasActiveStream());
  REQUIRE_FALSE(app.fileMode().streamAtEof());  // confirms this is NOT a real-EOF scenario

  // 2026-09-06再訂正(4回目)、実機PTYでの再検証を経て、この移植自身の
  // 先読みトリガーはhandleCommand()自身の前置ブロック(GoToBottom
  // 専用を除き撤去済み、その宣言コメント参照)ではなく
  // pollPagerExitNotice()(main.cpp自身のメインループから
  // handleCommand()の直後に必ず呼ばれる)へ一本化された――このテスト
  // でも同じ順序で呼ぶ。
  CHECK(app.handleCommand(Command::PageDown));
  app.pollPagerExitNotice();
  CHECK(app.fileMode().lastLoadAttemptYieldedNothing());
  CHECK_FALSE(app.fileMode().streamAtEof());  // still not a real EOF -- retryable later

  ::close(fds[1]);
}

// 対照テスト: 書き込み側が「実際に」閉じた(本物のEOF)場合、
// streamAtEof()自体もtrueになることを確認する――上のテストと合わせて
// 両方向を確認する。
TEST_CASE_FIXTURE(Fixture, "PageDown: a pagerReturnsToShell() File screen whose underlying pipe has actually "
                           "closed (a real EOF) sets streamAtEof()") {
  REQUIRE(app.startPagerFileList({(root / "plain.txt").string()}));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  int fds[2];
  REQUIRE(pipe(fds) == 0);
  REQUIRE(write(fds[1], "only one line\n", 14) == 14);
  ::close(fds[1]);  // a REAL EOF -- the write end is actually gone, not just momentarily quiet
  app.fileMode().loadStream(fds[0], /*ownsFd=*/true, "", 100);
  app.fileMode().setPagerReturnsToShell(true);

  CHECK(app.handleCommand(Command::PageDown));
  app.pollPagerExitNotice();
  CHECK(app.fileMode().streamAtEof());
  CHECK(app.fileMode().lastLoadAttemptYieldedNothing());
}

// 2026-09-05追加、直接のユーザー報告(スクリーンショット"=fp"、
// "-g起動ではここで止まった"): -gのマウスホイール/慣性スクロールは
// ContentView::setTopIndex()/stepTopIndex()を直接操作するだけで、
// handleCommand()を一切経由しない(main.cpp自身のコメント参照)。
// pollPagerExitNotice()自身の宣言コメント(2026-09-05再々訂正)が
// 説明する通り、このテストはまさにその経路――handleCommand()を
// 一度も呼ばずsetTopIndex()だけでビューアを動かし、メインループの
// 毎フレームの無条件呼び出しに相当するpollPagerExitNotice()の
// 反復だけで最初の1画面を超えて実際に続きが読み込まれることを確認
// する。
// 2026-09-06追記: 以前はここで「本当のスタールに当たると"(Exit)"が
// 出て、その後も上書きされず出続ける」ことも合わせて確認していたが、
// その通知自体を直接のユーザー報告("バイト単位読み込み仕様となった
// STDINではEOFと（Exit）がほぼ同時となり無意味なので(stdin)では
// 通知を無くしてほしい")を受けて削除したため、その部分は撤去した
// (showShellBusyNotice()自身のコメント参照)――以下は読み込み継続
// 自体の確認のみに絞る。
TEST_CASE_FIXTURE(Fixture, "pollPagerExitNotice: GUI-style scrolling that bypasses handleCommand() entirely "
                           "(ContentView::setTopIndex() alone, as the mouse-wheel/coast path does) still "
                           "loads more of a pagerReturnsToShell() stream as the viewport advances") {
  REQUIRE(app.startPagerFileList({(root / "plain.txt").string()}));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  int fds[2];
  REQUIRE(pipe(fds) == 0);
  // 最初の1画面分の自動読み込み(stillCatchingUpFirstScreen)を超えて
  // 「まだ余裕がある」区間を1回、GUIのホイールで進めても試せるよう、
  // rows*2ずつの先読みターゲットに対してたっぷり余裕を持たせた総量を
  // 一括で書いておく(write端は開けたまま)。readMoreIntoPipe()は
  // 256バイト単位でしか読まないため、要求行数ぴったりで止まるとは
  // 限らない(次のチャンクの途中まで余分に読み進むことがある)――
  // その丸め誤差を吸収できるだけの余裕(rows*8)を持たせてある。
  int rows = display.rows();
  int totalLinesWritten = rows * 8;
  std::string bulk;
  for (int i = 0; i < totalLinesWritten; ++i) bulk += "line " + std::to_string(i) + "\n";
  REQUIRE(write(fds[1], bulk.data(), bulk.size()) == static_cast<ssize_t>(bulk.size()));
  // 2026-09-06再訂正(4回目)、実機PTYでの再検証を経て、書き込み端は
  // すぐ閉じる――ensureLoadedThrough()自身の同日のコメント参照:
  // 一瞬のEAGAIN(書き込み端が開いたまま、単に今この瞬間だけ届いて
  // いない)はもはやpagerExitEncountered()のトリガーにならない
  // (legacy自身のブロッキング読み込みにはそもそも「EAGAIN」という
  // 概念が無いことが実機比較で判明したため)。既に書き込み済みの
  // 内容は、閉じた後でも全て消費し切るまでは通常通り読める――
  // 「本当の末尾に達した時」だけを正しくpagerExitEncountered()として
  // 検出できることを、このテスト自身で確認する。
  ::close(fds[1]);
  app.fileMode().loadStream(fds[0], /*ownsFd=*/true, "", rows * 2);
  app.fileMode().setPagerReturnsToShell(true);

  // handleCommand()は一度も呼ばない -- メインループの最初の数回分の
  // pollPagerExitNotice()だけで、まず最初の1画面分を埋めきる。
  app.pollPagerExitNotice();
  int linesAfterFirstScreen = static_cast<int>(app.activeContentView().lines().size());
  CHECK(linesAfterFirstScreen >= rows);
  CHECK(linesAfterFirstScreen < totalLinesWritten);  // まだ全部は読み切っていない

  // GUIのホイール/慣性スクロールを模して、setTopIndex()だけでビューを
  // 直接動かす(handleCommand()は経由しない)。書き込み済みの総量には
  // まだ十分な余裕があるので、この時点ではまだ本当のスタールではない
  // ――これが今回の修正の核心(以前は最初の1画面を埋めた後、GUIの
  // ホイール操作だけでは二度と追加読み込みが起きなかった)。
  app.activeContentView().setTopIndex(linesAfterFirstScreen);
  app.pollPagerExitNotice();
  CHECK_FALSE(app.fileMode().lastLoadAttemptYieldedNothing());

  // さらに繰り返しホイールで「今読み込めている範囲の一番下」まで
  // 進める -- ContentView::setTopIndex()自身の実装(すぐ上のCHECK)が
  // 常に現在のlines_.size()-height_へクランプするため、一度の大きな
  // ジャンプでは書き込み済みの総量を追い越せない(実機のホイール操作
  // でも同様、読み込み済みの末尾を超えては物理的にスクロールできない)。
  // 本物のホイール操作は「今の下端まで進む→少し追加が読み込まれて
  // 下端が伸びる→さらに進む」を繰り返すはずなので、ここでもそれを
  // そのまま繰り返し、書き込み済みの総量(totalLinesWritten)を実際に
  // 使い切って本当のスタールに当たるまで続ける。
  // 2026-09-06追記: readMoreIntoPipe()自身のペーシング精度の修正
  // (「あと1行」の要求は1バイト単位まで絞られ、以前のように256バイト
  // 単位の超過で数十行ぶん一気に進むことがなくなった)により、
  // topIndex_が可視範囲の際に張り付いている間は本当に1回のホイール
  // 操作につき1行しか伸びなくなった――残り(totalLinesWritten弱)を
  // 使い切るには以前の20回では全く足りないため、十分な余裕を見て
  // 回数を増やしてある。
  bool stalled = false;
  for (int i = 0; i < totalLinesWritten + 20 && !stalled; ++i) {
    app.activeContentView().setTopIndex(totalLinesWritten + rows * 4);  // 常にクランプされる、意図的
    app.pollPagerExitNotice();
    stalled = app.fileMode().lastLoadAttemptYieldedNothing();
  }
  CHECK(stalled);
}

// 2026-09-06追加、直接のユーザー報告("総行数が１行単位で進まない...
// 旧同様１バイトずつ読み込んで改行が来たら１増やす"): legacy自身の
// from_shell()(old/src/usilic.c)が、SHELLの生きたPAGERハンドオフに
// 限り画面に見えている範囲だけを1文字ずつ中継しているのに対し、この
// 移植は以前、パイプに既に十分なデータが溜まっていれば1回の呼び出しで
// 一気に(2画面分の先読み予算まで)満たしてしまっていた――man/groffの
// ような整形が一瞬で終わる短いページでは、これがlegacyのような
// 「1行ずつ増えていく」見た目には決してならない。当初はこれを
// 「pagerReturnsToShell()である間は、可視範囲を埋め終えた後も含めて
// 常に、1回のensureLoadedThrough()呼び出しが『あと1行』しか読み
// 進めない」という、最初の1画面もペーシング対象に含む形で修正した。
// 2026-09-06さらに訂正(6回目、直接のユーザー報告
// "`man ls|usilic -c^M`→「00001/00050」"「SHELLプロンプトで
// `man ls^M`後の最初の画面が瞬時に出ない」を受け、legacy自身の実機
// `U`で`man ls`を改めて逐一比較): その「最初の画面もペーシング対象」
// という一般化自体が誤りだったと判明した――legacy実機は、まだ画面が
// 一度も埋まっていない「最初の1画面」は(データが既に届いていれば)
// 0.3秒足らずでほぼ瞬時に埋め切り(`man ls\r`直後を0.1秒刻みで
// サンプリングして確認)、その後の通常のスクロール(`j`連打)でだけ
// 先読み分がちょうど1行ずつしか伸びない――「最初の画面もあと1行
// ずつ」ではなく「最初の画面だけは別枠で瞬時、以後は1行ずつ」という
// 2段構えが実態だった。このテストも合わせて訂正し、(1)最初の1画面は
// pollPagerExitNotice()の「1回の」呼び出しで一気に埋まること、
// (2)画面を埋め終えた後の通常のスクロールは、legacy同様「あと1行」
// だけであることの両方を確認する。
TEST_CASE_FIXTURE(Fixture, "ensureLoadedThrough: a pagerReturnsToShell() stream fills the very first screen "
                           "in one shot (bypassing the pacing) but then grows exactly one line per call for "
                           "ordinary scrolling afterward -- matching legacy's own \"(stdin)\" behavior "
                           "(instant first page, then a paced line-count-up)") {
  REQUIRE(app.startPagerFileList({(root / "plain.txt").string()}));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  int rows = display.rows();
  int fds[2];
  REQUIRE(pipe(fds) == 0);
  // rows*8行分をあらかじめ全部書いておく(write端は開けたまま) -- 十分
  // すぎるほどのデータが既にkernelのパイプに溜まっている、man/groffの
  // ような短いページが一瞬で整形し終わった状況を模す。
  std::string bulk;
  for (int i = 0; i < rows * 8; ++i) bulk += "line " + std::to_string(i) + "\n";
  REQUIRE(write(fds[1], bulk.data(), bulk.size()) == static_cast<ssize_t>(bulk.size()));

  // Application::pollShellHandoff()自身の実際の呼び出しと同じ形
  // (initialLineBudget=1)で読み込み、その直後にpagerReturnsToShell()を
  // 立てる。
  app.fileMode().loadStream(fds[0], /*ownsFd=*/true, "", 1);
  app.fileMode().setPagerReturnsToShell(true);
  REQUIRE(static_cast<int>(app.activeContentView().lines().size()) == 1);

  // まだ画面が一度も埋まっていない最初の1画面は、既に十分なデータが
  // 届いているので、pollPagerExitNotice()の「1回の」呼び出しだけで
  // 一気に埋まる(legacy実機の"0.3秒足らずでほぼ瞬時"に相当) -- 以前の
  // ように何度も反復してようやく埋まる、ではない。readMoreIntoPipe()
  // 自身のチャンク読み込みには高々1行程度の丸め超過があり得るため
  // (自身のコメント参照)、ここでは「少なくとも1画面分は埋まった」
  // ことだけを確認し、実際に埋まった行数を以後の基準点にする。
  app.pollPagerExitNotice();
  int linesAfterFirstScreen = static_cast<int>(app.activeContentView().lines().size());
  CHECK(linesAfterFirstScreen >= rows);
  CHECK_FALSE(app.fileMode().lastLoadAttemptYieldedNothing());  // 実際には十分残っている

  // 可視範囲をちょうど埋め終えた直後は、pollPagerExitNotice()自身の
  // 外側のゲート(stillCatchingUpFirstScreen×viewerMovedSinceLastAttempt、
  // 自身のコメント参照)がどちらも不成立になり、ユーザーがまだ何も
  // していない(topIndex_も動いていない)間は今回はもう何も試みない
  // ――これは元々の「ユーザー自身の次の操作を待つ」という設計通り
  // であり、退行ではない。実際にユーザーがスクロールした(topIndex_が
  // 動いた)時、続きが読み込まれることは確認するが、それでも「あと
  // 1行」だけであって、画面外の先読み分をまとめて一括で読み込んだり
  // はしない――legacy自身の`j`連打での逐一比較と一致する、以前からの
  // 設計のこの部分は変わっていない(こちらは「あと1行」への切り詰め
  // 自体がensureLoadedThrough()内で明示的に行われるので、丸め超過は
  // 起きない)。
  app.activeContentView().setTopIndex(1);
  app.pollPagerExitNotice();
  CHECK(static_cast<int>(app.activeContentView().lines().size()) == linesAfterFirstScreen + 1);
  CHECK_FALSE(app.fileMode().lastLoadAttemptYieldedNothing());  // まだ十分残っている

  // さらにもう1回スクロールしても、やはり1行だけ増える -- 通常の
  // スクロールが続く限り、この「常に+1」が続くことを確認する。
  app.activeContentView().setTopIndex(2);
  app.pollPagerExitNotice();
  CHECK(static_cast<int>(app.activeContentView().lines().size()) == linesAfterFirstScreen + 2);

  ::close(fds[1]);
}

// 2026-09-06追加、同じ直接のユーザー報告の調査中に見つけた回帰の
// 芽への予防線: Command::GoToBottom(`less`のGコマンド同様、本当の
// EOFまでブロックして読み切る意図、handleCommand()自身の
// std::numeric_limits<int>::max()-1というsentinelのコメント参照)を
// 可視範囲がまだ埋まっていない間(ストリームを開いた直後)に押した
// 場合、上のペーシングに巻き込まれて「あと1行」に絞られてしまっては
// 「今すぐ全部読み切る」というGoToBottom自身の契約が壊れる――
// sentinel値そのものをペーシングの対象外にしていることを確認する。
TEST_CASE_FIXTURE(Fixture, "ensureLoadedThrough: Command::GoToBottom's own \"read everything now\" sentinel "
                           "is exempt from the pagerReturnsToShell() line-by-line pacing, even when pressed "
                           "before the visible screen has even filled once") {
  REQUIRE(app.startPagerFileList({(root / "plain.txt").string()}));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  int rows = display.rows();
  int fds[2];
  REQUIRE(pipe(fds) == 0);
  std::string bulk;
  for (int i = 0; i < rows * 3; ++i) bulk += "line " + std::to_string(i) + "\n";
  REQUIRE(write(fds[1], bulk.data(), bulk.size()) == static_cast<ssize_t>(bulk.size()));
  ::close(fds[1]);  // 実際のEOF -- GoToBottomが「本当の末尾」に到達できることも併せて確認する

  app.fileMode().loadStream(fds[0], /*ownsFd=*/true, "", 1);
  app.fileMode().setPagerReturnsToShell(true);
  REQUIRE(static_cast<int>(app.activeContentView().lines().size()) == 1);

  CHECK(app.handleCommand(Command::GoToBottom));
  CHECK(app.fileMode().streamAtEof());  // ペーシングに巻き込まれず、本当の末尾まで一気に読み切れた
  CHECK(static_cast<int>(app.activeContentView().lines().size()) == rows * 3);
}

TEST_CASE_FIXTURE(Fixture, "'[' and ']' keep their ordinary Directory-mode marking behavior when NOT "
                           "in File mode with a pager list active (no regression)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);

  CHECK(app.feedByte(']', std::nullopt));
  CHECK(app.directoryMode().isMarked(idx));  // ordinary marking, not pager navigation
}

TEST_CASE_FIXTURE(Fixture, "n/p/x/f are no-ops in File mode reached via an ordinary Directory-mode "
                           "Enter (no pager list active)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string before = app.fileMode().currentFile();

  CHECK_FALSE(app.feedByte('n', std::nullopt));
  CHECK_FALSE(app.feedByte('p', std::nullopt));
  CHECK_FALSE(app.feedByte('x', std::nullopt));
  CHECK_FALSE(app.feedByte('f', std::nullopt));
  CHECK(app.fileMode().currentFile() == before);
}

TEST_CASE_FIXTURE(Fixture, "n/p/x/f are no-ops in Directory mode (not File mode at all)") {
  REQUIRE(app.start(path()));
  CHECK_FALSE(app.feedByte('n', std::nullopt));
  CHECK_FALSE(app.feedByte('p', std::nullopt));
  CHECK_FALSE(app.feedByte('x', std::nullopt));
  CHECK_FALSE(app.feedByte('f', std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

// 2026-08-28 fix, a direct user report ("'^-'[return]→戻り値に右端まで
// のパディングがない", referencing HISTORY.md:9473's general rule):
// grepView_'s own ModeLineFieldsProvider explicitly passed
// fillTrailingToEdge=false (unlike every other mode's provider),
// leaving a real, visible non-Standout gap between trailing3 (the
// return-destination path) and the row's true right edge.
TEST_CASE_FIXTURE(Fixture, "EGREP results screen (grepView_): the mode line fills to the right edge with "
                           "Standout, matching HISTORY.md:9473's general rule (was fillTrailingToEdge=false)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  REQUIRE(app.feedByte(0x13, std::nullopt));  // ^S -> EGREP direct entry
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  feedText("^-");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // Enter -> submit search
  REQUIRE(app.currentMode() == ApplicationMode::Grep);
  const std::string &row = display.rowText(23);
  std::size_t lastContent = row.find_last_not_of(' ');
  REQUIRE(lastContent != std::string::npos);
  // From just after the last real character out to (but not including)
  // the true right edge, this used to be a real, visible non-Standout
  // gap -- now Standout-filled, matching every other mode's own fill
  // convention (see drawPromptLabel()'s own "1 column short of the true
  // right edge" precedent, HISTORY.md:9479, for why column 79 itself
  // stays deliberately non-Standout).
  for (int col = static_cast<int>(lastContent) + 1; col < 79; col++) {
    CAPTURE(col);
    CHECK(display.standoutAt(23, col));
  }
  CHECK_FALSE(display.standoutAt(23, 79));
}

// 2026-08-28 fix, a direct user report + own explicit spec ("戻り値＋
// モード名エリアがいずれも長過ぎ名称の時に限り中央に...残りを半々でいい
// けれども今回は「CLAUDE.md」だけなので臨機応変で"): when landing on a
// match crowds the mode line into renderSplitModeLine()'s fallback
// layout, a SHORT name (e.g. "CLAUDE.md") must show in full (not
// truncated to its own forced half) and the freed-up space must go to
// the genuinely long trailing2 (the matched line's own text) instead of
// sitting unused.
TEST_CASE_FIXTURE(Fixture, "GREP: split mode-line layout (crowded File-mode landing) gives a SHORT name its "
                           "full width, not its own forced half, and gives trailing2 the freed-up space") {
  { std::ofstream(root / "CLAUDE.md") << "hello\n- Legacy C implementation description here matching old/src/config.sh\nworld\n"; }
  REQUIRE(app.start(path()));
  int idx = indexOf("CLAUDE.md");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::StartFgrep));
  feedText("Legacy");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  const std::string &row = display.rowText(23);
  CHECK(row.substr(0, 9) == "CLAUDE.md");  // full name, not ellipsis-truncated to share a forced half
  // The match text itself is still long enough to need SOME truncation
  // even after freeing up name's own unused half -- but both its own
  // head and tail should now survive (Finder-style middle-ellipsis),
  // unlike the OLD forced-half budget which cut it back much further.
  CHECK(row.find("- Legacy C implementation") != std::string::npos);
  CHECK(row.find("old/src/config.sh") != std::string::npos);
}

// 2026-08-28 fix, a direct user report + precise repro ("CLAUDE.md画面
// で^J^S^R...^G→EGREPなのに検索文字列一覧が無く（←FGREP風）行数情報も
// 無い"): plain ^R while still just BROWSING the EGREP direct-entry
// candidate list (nothing typed yet) used to be mistaken for a typed
// character -- see feedRegexListByte()'s own 0x12 case comment -- which
// silently clobbered promptHistoryRecallIsDirectEntry_ back to false,
// so the SUBSEQUENT ^G landed on a stray editable EGREP prompt drawn
// over File mode's plain view instead of back where the search actually
// started. A same-day follow-up report ("前回使用した正規表現は何
// だっけ？...EGREPの検索文字列一覧からも発動してくれないと困る")
// clarified plain ^R here shouldn't just be neutralized -- it's
// supposed to do the SAME real sorted<->chronological toggle
// RecentFile/RecentDirectory's own ^R already does, just reached
// safely now (via isNav/resolveCommand/toggleListBrowserHistory()),
// not through the corrupting "start editing" detour.
TEST_CASE_FIXTURE(Fixture, "feedRegexListByte: plain ^R while still just BROWSING the EGREP candidate list "
                           "(nothing typed yet) does a REAL sorted<->chronological toggle (not the old "
                           "corrupting detour), and a later Back still correctly returns to File mode") {
  history.pushRecent(RingCategory::RecentEgrepPattern, "one");
  history.pushRecent(RingCategory::RecentEgrepPattern, "two");
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x0a, std::nullopt));  // ^J
  REQUIRE(app.feedByte(0x13, std::nullopt));  // ^S
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK(*app.listBrowserMode().selectedItem() == "one");  // sorted, direct-entry default: TOP (oldest)

  CHECK(app.feedByte(0x12, std::nullopt));  // ^R: real toggle now
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  // 2026-08-29 fix, a direct user report with exact before/after field
  // values: name DOES switch to "HISTORY" once chronological (matching
  // RecentFile/RecentDirectory's own identical swap) -- the isDirectEntry
  // branch is only for the READ-ONLY-but-not-yet-toggled state; once
  // listBrowserChronological_ flips, this falls through to the same
  // "HISTORY" + rightWatermark=promptLabelText(kind) shape a real ^R
  // mid-typing recall already used.
  CHECK(display.rowText(23).find("HISTORY") != std::string::npos);
  CHECK(*app.listBrowserMode().selectedItem() == "two");  // chronological: newest (last-pushed) selected

  // 2026-08-29 fix: the FIRST ^G pops the toggle back to sorted (still
  // ListBrowser), matching RecentFile/RecentDirectory's own established
  // two-^G shape -- only the SECOND ^G actually leaves.
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
  CHECK(*app.listBrowserMode().selectedItem() == "one");  // back to sorted, TOP entry

  CHECK(app.handleCommand(Command::Back));  // second ^G
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK_FALSE(app.promptActive());  // NOT a stray editable EGREP prompt left drawn over File's own view
  CHECK(app.fileMode().currentFile() == (root / "plain.txt").string());
}

// 2026-08-28: a direct user report ("^H(bs)が利いてしまって「^[A-Z[A-Z]+」
// になるバグ" on usilicrc's own real "^[A-Z][A-Z]+" MAN seed pattern) --
// investigated using the REAL usilicrc GREP seed list (not synthetic
// "one"/"two"/"three") to match the exact reported string. Backspace
// while browsing correctly removes only the last byte ("+"), matching
// the existing synthetic-content test just above -- could not reproduce
// the reported "]" also disappearing this way; left as a passing
// regression check pending a more specific repro (e.g. via the ^R
// chronological view specifically, see this session's HISTORY.md).
TEST_CASE_FIXTURE(Fixture, "feedRegexListByte: backspace on the real usilicrc MAN seed pattern candidate "
                           "\"^[A-Z][A-Z]+\" removes only the last byte") {
  for (const std::string &pattern : {"/\\*[a-z]*\\*/", "^", "^  [0-9][0-9]+\\.", "^  [0-9]+\\.", "^Question",
                                      "^Subject: ", "^[0-9]*\\.", "^[A-Z][A-Z]+", "diff \\-", "~$"}) {
    history.pushRecent(RingCategory::RecentEgrepPattern, pattern);
  }
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  while (*app.listBrowserMode().selectedItem() != "^[A-Z][A-Z]+") {
    if (!app.handleCommand(Command::MoveDown)) break;
  }
  std::string highlighted = *app.listBrowserMode().selectedItem();
  REQUIRE(highlighted == "^[A-Z][A-Z]+");

  CHECK(app.feedByte(0x08, std::nullopt));  // ^H
  CHECK(app.promptText() == "^[A-Z][A-Z]");  // only the trailing "+" removed
}

// 2026-08-28: a direct user report referencing HISTORY.md:7833/7839
// (item 283 -- a candidate string with a LITERAL embedded control byte,
// e.g. from a corrupted/hand-edited usilicrc entry): investigated
// whether that fix (renderModeLine()'s trailingPlain path showing
// control bytes as reversed char+0x40, matching renderLine()'s own
// convention) also holds once ^H itself (as a KEYPRESS) transitions the
// highlighted candidate into a real editable LineEditor. It does --
// LineEditor::renderVisible() already had this exact convention (ported
// from vt_putchar(), predating item 283), and correctly removes only
// the LAST byte on backspace regardless of an embedded control byte
// elsewhere in the string.
TEST_CASE_FIXTURE(Fixture, "feedRegexListByte: a candidate string containing a literal embedded control byte "
                           "(^H/0x08) still backspaces correctly (only the last byte removed) once ^H itself "
                           "transitions it into editing") {
  std::string pattern = "^[A-Z]";
  pattern += static_cast<char>(0x08);
  pattern += "[A-Z]+";
  history.pushRecent(RingCategory::RecentEgrepPattern, pattern);
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(*app.listBrowserMode().selectedItem() == pattern);

  REQUIRE(app.feedByte(0x08, std::nullopt));  // ^H (backspace) -- transitions into editing
  CHECK(app.promptText() == pattern.substr(0, pattern.size() - 1));
}

// 2026-08-28 fix, a direct user report: "~/.usilic/usilicrcのGREP
// セクションに実際にある^L(0x0C、~/.usilic/history内のモード区切り用)
// がHISTORY一覧画面で「ハイライトのLが無い」" -- unlike the direct-entry
// candidate list (trailingPlain=true, already safe since item 283), the
// "HISTORY" chronological view (reached via a REAL ^R recall while
// already editing, trailingPlain=false) drew its own candidate text
// through renderModeLine()'s generic drawSegment(), which put raw
// control bytes straight through -- a real terminal's own reaction to
// ^L (commonly "clear/next page") would corrupt whatever followed.
TEST_CASE_FIXTURE(Fixture, "^R real recall (HISTORY chronological view): a candidate string containing a "
                           "literal embedded control byte (^L/0x0C) renders as reversed 'L', not a raw byte "
                           "that could corrupt a real terminal") {
  std::string pattern = "^";
  pattern += static_cast<char>(0x0c);
  // RecentFgrepPattern, NOT RecentEgrepPattern -- StartFgrep (used
  // below for the "plain prompt" flow the ^R-mid-typing scenario
  // needs, see its own comment) reads grepSyntax_==Fixed's own ring;
  // pushing to the wrong ring here left listBrowserMode().selectedItem()
  // returning nullopt, silently dereferenced by an earlier version of
  // this test -- undefined behavior that surfaced as a delayed SIGSEGV
  // well after every assertion had already reported as passed.
  history.pushRecent(RingCategory::RecentFgrepPattern, pattern);
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartFgrep));  // plain prompt, not direct-entry
  feedText("x");
  REQUIRE(app.feedByte(0x12, std::nullopt));  // ^R: real mid-typing recall (directEntry=false)
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.listBrowserMode().selectedItem() != nullptr);
  REQUIRE(*app.listBrowserMode().selectedItem() == pattern);

  const std::string &row = display.rowText(23);
  CHECK(row.find(static_cast<char>(0x0c)) == std::string::npos);  // no raw control byte reaches the display
  CHECK(row.find("^L") != std::string::npos);                     // shown as the reversed-letter convention
}

TEST_CASE_FIXTURE(Fixture, "^O^C (COPY): reachable from Directory mode, freezes the exact same content "
                           "on screen, and a plain ^G returns to Directory with the screen fully "
                           "restored and the origin mode's own state untouched") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  int selectedBefore = indexOf(app.directoryMode().selectedEntry()->name);
  std::vector<std::string> rowsBefore;
  for (int i = 0; i < 24; i++) rowsBefore.push_back(display.rowText(i));

  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte(0x03, std::nullopt));   // ^O^C
  CHECK(app.currentMode() == ApplicationMode::Copy);
  for (int i = 0; i < 23; i++) CHECK(display.rowText(i) == rowsBefore[i]);  // exact text, styling aside

  // 2026-08-29 fix, a direct user report against the real binary:
  // ("^Oc→モードライン不良（変化無し）。正しくは「COPY」「---」
  // 「/path/」" and "^Oc→ブロックカーソル位置が...正しくは直前の
  // ハイライト行位置") -- the mode-line must actually change (fixed
  // "COPY"/"---" plus the origin's own path as the value, NOT a frozen
  // copy of whatever Directory's own mode-line already showed), and
  // the cursor must start on the row that was highlighted before ^O^C,
  // not row 0.
  const std::string &modeLine = display.rowText(23);
  CHECK(modeLine.substr(0, 4) == "COPY");
  CHECK(modeLine.find("---") != std::string::npos);
  CHECK(modeLine.find(path()) != std::string::npos);
  CHECK(selectedBefore > 0);  // a meaningful test of this needs a non-zero starting row
  CHECK(display.cursorRow() == selectedBefore);

  CHECK(app.handleCommand(Command::Back));  // ^G
  CHECK(app.currentMode() == ApplicationMode::Directory);
  CHECK(indexOf(app.directoryMode().selectedEntry()->name) == selectedBefore);  // untouched by CopyMode
  for (int i = 0; i < 24; i++) CHECK(display.rowText(i) == rowsBefore[i]);      // screen fully restored
}

TEST_CASE_FIXTURE(Fixture, "^O^C (COPY): also reachable from File mode, GrepPattern's own candidate "
                           "list (ListBrowser), and a plain ListBrowser session (SHELL command "
                           "history) -- confirming this is genuinely mode-agnostic, not wired "
                           "per-mode") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Copy);
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.handleCommand(Command::StartEgrep));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Copy);
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);

  CHECK(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.handleCommand(Command::StartCommandHistory));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Copy);
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::ListBrowser);
}

TEST_CASE_FIXTURE(Fixture, "^O^C (COPY): a live selection renders as an actual reverse-video (Standout) "
                           "run over exactly the marked range on the real screen") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Copy);
  const std::string &row0 = display.rowText(0);
  REQUIRE(row0.size() >= 3);
  for (int i = 0; i < 2; i++) app.feedByte(0x06, std::nullopt);  // ^F x2: column 2
  app.feedByte(0x80, std::nullopt);                              // ^Space: mark start
  for (int i = 0; i < 2; i++) app.feedByte(0x06, std::nullopt);  // extend to column 4
  CHECK(display.standoutAt(0, 1) == false);
  CHECK(display.standoutAt(0, 2) == true);
  CHECK(display.standoutAt(0, 3) == true);
  CHECK(display.standoutAt(0, 4) == false);
}

TEST_CASE_FIXTURE(Fixture, "^[N/^[gg N (goto-line) work outside COPY mode too, on any ordinary "
                           "ContentView-backed screen -- Directory mode here (2026-09-08, a direct "
                           "user correction: \"「COPYの検証も今後の試行事案でいいとして」が伝わら"
                           "なかったみたいだけど、^[Nはおよそ行数あり画面ならどこでも機能でもある"
                           "のでよろしく\" -- this is Application::dispatchLeaderByte()'s own "
                           "gotoLineDigitsPending_/gotoLineGPending_, entirely separate from "
                           "CopyMode's own copyGotoLine*)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  int idx = indexOf("log.txt");

  // ^[N: ESC, digits, Enter -- jumps straight to that entry's row.
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  for (char c : std::to_string(idx + 1)) app.feedByte(c, std::nullopt);
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK(indexOf(app.directoryMode().selectedEntry()->name) == idx);

  // Move off of it, then jump back with ^[gg N instead.
  app.handleCommand(Command::MoveDown);
  REQUIRE(indexOf(app.directoryMode().selectedEntry()->name) != idx);
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  app.feedByte('g', std::nullopt);
  app.feedByte('g', std::nullopt);
  for (char c : std::to_string(idx + 1)) app.feedByte(c, std::nullopt);
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK(indexOf(app.directoryMode().selectedEntry()->name) == idx);
}

TEST_CASE_FIXTURE(Fixture, "^[N (goto-line) displays the digits being typed as plain (non-Standout) "
                           "text in the position field, matching other text-entry areas -- unlike "
                           "every other modeline field, which is always Standout (2026-09-08, a "
                           "direct user request: \"数字入力は、他の文字列入力エリア同様、ハイライト"
                           "無しにして欲しい\")") {
  REQUIRE(app.start(path()));
  // Diffed against the row's own content just before typing, rather
  // than searching the whole row for '4' -- the fixture's own temp
  // directory path (shown elsewhere on this same modeline row) has a
  // random suffix that could incidentally contain a '4' too, which
  // would make a plain row-wide search flaky.
  std::string rowBefore = display.rowText(23);
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  app.feedByte('4', std::nullopt);
  const std::string &rowAfter = display.rowText(23);
  size_t pos = std::string::npos;
  for (size_t i = 0; i < rowBefore.size() && i < rowAfter.size(); i++) {
    if (rowBefore[i] != rowAfter[i]) {
      pos = i;
      break;
    }
  }
  REQUIRE(pos != std::string::npos);
  CHECK(rowAfter[pos] == '4');
  CHECK(display.standoutAt(23, static_cast<int>(pos)) == false);
}

TEST_CASE_FIXTURE(Fixture, "^[N (goto-line) triggers the File-mode pager to load content through "
                           "the target line before jumping, both forward and backward, instead of "
                           "silently clamping to whatever happened to be loaded already (the pager "
                           "loads lazily -- see FileMode::ensureLoadedThrough()/Application::"
                           "preloadPagerAhead(), the same mechanism Command::ScrollLineDown/PageDown "
                           "already use)") {
  {
    std::ofstream out(root / "biglines.txt");
    for (int i = 1; i <= 60; i++) {
      std::string n = std::to_string(i);
      out << "bigline" << std::string(3 - n.size(), '0') << n << "\n";
    }
  }
  REQUIRE(app.start(path()));
  int idx = indexOf("biglines.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(display.rowText(0).substr(0, 10) == "bigline001");

  // Forward jump past whatever the pager has loaded so far.
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  for (char c : std::string("45")) app.feedByte(c, std::nullopt);
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK(app.activeContentView().cursorIndex() == 44);
  bool found45 = false;
  for (int row = 0; row < 24; row++)
    if (display.rowText(row).substr(0, 10) == "bigline045") found45 = true;
  CHECK(found45);

  // Backward jump -- must land exactly on line 5, not get stuck wherever
  // the forward jump left off. The jump also re-centers the viewport
  // (2026-09-08, a direct user request -- see ContentView::
  // centerOnCursor()'s own comment), so near the very start of the
  // file this clamps to topIndex 0 and the target line shows at its
  // own row (cursorIndex), not necessarily row 0.
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  for (char c : std::string("5")) app.feedByte(c, std::nullopt);
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK(app.activeContentView().cursorIndex() == 4);
  CHECK(app.activeContentView().topIndex() == 0);  // can't center fully this close to TOP -- clamps
  CHECK(display.rowText(4).substr(0, 10) == "bigline005");
}

TEST_CASE_FIXTURE(Fixture, "Command::Redraw (^L) re-centers the current line on screen, matching "
                           "vi's line-number-launch and emacs' own ^L (2026-09-08, a direct user "
                           "request: \"ページTOPとENDいがいでは、^L（描き直し）が投入された場合、"
                           "ブロックカーソル位置のカレント行が画面中央に来るようにしてほしい\")") {
  {
    std::ofstream out(root / "biglines2.txt");
    for (int i = 1; i <= 60; i++) {
      std::string n = std::to_string(i);
      out << "bigline" << std::string(3 - n.size(), '0') << n << "\n";
    }
  }
  REQUIRE(app.start(path()));
  int idx = indexOf("biglines2.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  for (int i = 0; i < 30; i++) app.handleCommand(Command::MoveDown);  // cursorIndex == 30
  REQUIRE(app.activeContentView().cursorIndex() == 30);
  int topBeforeRedraw = app.activeContentView().topIndex();

  REQUIRE(app.feedByte(0x0c, std::nullopt));  // ^L
  int height = app.activeContentView().height();
  CHECK(app.activeContentView().topIndex() == 30 - height / 2);
  CHECK(app.activeContentView().topIndex() != topBeforeRedraw);  // actually re-centered, not a no-op
  bool sawCurrentLine = false;
  for (int row = 0; row < display.rows(); row++)
    if (display.rowText(row).substr(0, 10) == "bigline031") sawCurrentLine = true;
  CHECK(sawCurrentLine);
}

TEST_CASE_FIXTURE(Fixture, "^[N (goto-line, general version) ignores unrecognized keys and keeps "
                           "waiting, supports Backspace to edit, and ^G cancels outright -- same "
                           "legacy-faithful spec as CopyMode's own version (old/src/disp.c's "
                           "print_line(), linep==F2 branch)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");

  // Ignored key mid-entry doesn't cancel the jump.
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  for (char c : std::to_string(idx + 1)) app.feedByte(c, std::nullopt);
  app.feedByte('q', std::nullopt);  // ignored
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK(indexOf(app.directoryMode().selectedEntry()->name) == idx);

  app.handleCommand(Command::MoveDown);
  int otherIdx = indexOf(app.directoryMode().selectedEntry()->name);
  REQUIRE(otherIdx != idx);

  // ^G cancels outright -- no jump back to idx.
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  for (char c : std::to_string(idx + 1)) app.feedByte(c, std::nullopt);
  app.feedByte(0x07, std::nullopt);  // ^G
  CHECK(indexOf(app.directoryMode().selectedEntry()->name) == otherIdx);  // unchanged
}

TEST_CASE_FIXTURE(Fixture, "^O^C (COPY): ^[N (ESC then digits then Enter) jumps the cursor to that "
                           "1-based line (2026-09-08, a direct user request: \"未サポートでもある"
                           "^[N（Nは行番号）で当該行へジャンプする機能をサポートして欲しい\")") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(display.rowText(0).substr(0, 10) == "marker one");  // log.txt: line1="marker one"

  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Copy);

  // ESC, '3', Enter -- jumps straight to line 3 ("marker two").
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  app.feedByte('3', std::nullopt);
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  app.feedByte(0x80, std::nullopt);                              // ^@: mark start at (row 2, col 0)
  for (int i = 0; i < 6; i++) app.feedByte(0x06, std::nullopt);  // extend over "marker"
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('w', std::nullopt));  // confirm -- stays in Copy (regular mark)

  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.handleCommand(Command::StartFgrep));
  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x19, std::nullopt));
  CHECK(app.promptText() == "marker");
}

TEST_CASE_FIXTURE(Fixture, "^O^C (COPY): ^[gg N (Emacs' own M-g g goto-line prefix, followed by "
                           "digits and Enter) is a full alias for ^[N (2026-09-08, a direct user "
                           "request: \"emacsではalt＋ggNなのでそちらもよろしく\")") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Copy);

  // ESC, 'g', 'g', '5', Enter -- jumps to line 5 ("line2b").
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  app.feedByte('g', std::nullopt);
  app.feedByte('g', std::nullopt);
  app.feedByte('5', std::nullopt);
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  app.feedByte(0x80, std::nullopt);                              // ^@: mark start at (row 4, col 0)
  for (int i = 0; i < 6; i++) app.feedByte(0x06, std::nullopt);  // extend over all of "line2b"
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('w', std::nullopt));

  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x19, std::nullopt));
  CHECK(app.promptText() == "line2b");
}

TEST_CASE_FIXTURE(Fixture, "^O^C (COPY): goto-line digit entry ignores any byte that isn't a "
                           "digit/Enter/Backspace/^G and keeps waiting for one, matching legacy's "
                           "own print_line() (old/src/disp.c, the linep==F2 branch) rather than "
                           "cancelling on the first unrecognized key (2026-09-08, a direct user "
                           "report: \"何も起きないんだが。旧ではモードライン「現在行/総行 ??%」が"
                           "数値入力エリアとなって数字入力を受け付ける仕様なのでそのようにして"
                           "欲しい\")") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Copy);

  // ESC, '3', 'x' (ignored, not a digit/Enter/Backspace/^G -- stays pending),
  // Enter -- still jumps to line 3, proving 'x' didn't cancel anything.
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  app.feedByte('3', std::nullopt);
  app.feedByte('x', std::nullopt);
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  app.feedByte(0x80, std::nullopt);                              // ^@: mark start at (row 2, col 0)
  for (int i = 0; i < 6; i++) app.feedByte(0x06, std::nullopt);  // extend over "marker"
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('w', std::nullopt));

  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x19, std::nullopt));
  CHECK(app.promptText() == "marker");  // from line 3 ("marker two") -- the jump DID happen
}

TEST_CASE_FIXTURE(Fixture, "^O^C (COPY): goto-line digit entry supports Backspace to edit a typo, "
                           "and Backspace on an empty digit string cancels the whole entry "
                           "(matching legacy's own K_dl/K_h handling, disp.c)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Copy);

  // ESC, '9', '9', Backspace, Backspace, '3', Enter -- "99" is typed then
  // fully erased via two Backspaces, then '3' is typed fresh -- must land
  // on line 3, not line 99 (which would clamp to the file's last line).
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  app.feedByte('9', std::nullopt);
  app.feedByte('9', std::nullopt);
  app.feedByte(0x7f, std::nullopt);
  app.feedByte(0x7f, std::nullopt);
  app.feedByte('3', std::nullopt);
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  app.feedByte(0x80, std::nullopt);
  for (int i = 0; i < 6; i++) app.feedByte(0x06, std::nullopt);  // extend over "marker"
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('w', std::nullopt));

  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x19, std::nullopt));
  CHECK(app.promptText() == "marker");  // line 3 ("marker two") -- backspace-edited digits took effect
}

TEST_CASE_FIXTURE(Fixture, "^O^C (COPY): ^G cancels goto-line digit entry outright, without "
                           "jumping and without leaving Copy mode (legacy's own K_g in the same "
                           "print_line() loop)") {
  REQUIRE(app.start(path()));
  int idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Copy);  // still on line 1 ("marker one")

  REQUIRE(app.feedByte(0x1b, std::nullopt));
  app.feedByte('9', std::nullopt);
  app.feedByte('9', std::nullopt);
  app.feedByte(0x07, std::nullopt);  // ^G
  CHECK(app.currentMode() == ApplicationMode::Copy);  // ^G cancelled the entry, not all of Copy

  app.feedByte(0x80, std::nullopt);                              // ^@: mark from wherever we actually are
  for (int i = 0; i < 6; i++) app.feedByte(0x06, std::nullopt);  // extend over "marker"
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('w', std::nullopt));

  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.handleCommand(Command::StartFgrep));
  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x19, std::nullopt));
  CHECK(app.promptText() == "marker");  // still line 1 ("marker one") -- no jump happened
}

TEST_CASE_FIXTURE(Fixture, "^O^C/^O^Y (COPY/PASTE) round trip: select a substring of a File-mode "
                           "buffer, leave COPY, open an FGREP prompt, and ^O^Y inserts exactly that "
                           "substring at the cursor") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(display.rowText(0).substr(0, 5) == "hello");  // plain.txt's own known content

  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Copy);
  app.feedByte(0x80, std::nullopt);                              // ^@: mark start at column 0
  for (int i = 0; i < 5; i++) app.feedByte(0x06, std::nullopt);  // extend over "hello"
  app.feedByte(0x1b, std::nullopt);
  app.feedByte('w', std::nullopt);  // ^[w: confirm
  CHECK(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.handleCommand(Command::StartFgrep));  // plain prompt, not direct-entry
  REQUIRE(app.promptActive());
  feedText("x");
  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte(0x19, std::nullopt));   // ^O^Y
  CHECK(app.promptText() == "xhello");
}

TEST_CASE_FIXTURE(Fixture, "^O^C (COPY): ^X SPC starts emacs-style rectangle-mark-mode, ^F/^B/^N/^P "
                           "build a genuine column-range rectangle across multiple rows, and ^[w "
                           "copies it AND auto-exits Copy (2026-09-07, a direct user request: "
                           "\"①emacsには矩形範囲指定の機能があるのであれを移植してほしい...^X␣で発動... "
                           "`^[w`でクリップボードへコピーしたのちCOPYを終了\")") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(display.rowText(0).substr(0, 5) == "hello");   // plain.txt: "hello\nworld\n"
  REQUIRE(display.rowText(1).substr(0, 5) == "world");

  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Copy);

  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte(' ', std::nullopt));   // SPC -- rectangle-mark-mode starts at (row 0, col 0)
  CHECK(display.rowText(23).find("Rectangle-Mark") != std::string::npos);
  for (int i = 0; i < 3; i++) app.feedByte(0x06, std::nullopt);  // ^F x3 -- column 3
  app.feedByte(0x0e, std::nullopt);                              // ^N -- row 1, column preserved (3)

  REQUIRE(app.feedByte(0x1b, std::nullopt));  // ESC
  REQUIRE(app.feedByte('w', std::nullopt));   // w -- confirm AND auto-exit (rectangle-specific)
  CHECK(app.currentMode() == ApplicationMode::File);  // left Copy automatically
  CHECK_FALSE(display.rowText(23).find("Rectangle-Mark") != std::string::npos);

  // Round-trip the clipboard through a normal ^O^Y paste to verify its content.
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.handleCommand(Command::StartFgrep));
  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x19, std::nullopt));
  CHECK(app.promptText() == "hel\nwor");  // columns [0,3) of rows "hello"/"world"
}

TEST_CASE_FIXTURE(Fixture, "^O^C (COPY): a REGULAR (non-rectangle) mark's ^[w still just confirms "
                           "and stays in Copy mode, unchanged -- the auto-exit is specific to "
                           "rectangle-mark, per the direct user request's own wording describing it "
                           "as part of that workflow") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Copy);
  app.feedByte(0x80, std::nullopt);  // ^@: regular mark start
  app.feedByte(0x06, std::nullopt);  // ^F
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('w', std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Copy);  // unchanged: still in Copy
}

TEST_CASE_FIXTURE(Fixture, "^O^C (COPY): ^[p confirms the mark, exits Copy, and moves specifically "
                           "into the PROGRAM prompt with the confirmed selection set as its text "
                           "(2026-09-07, a direct user request: \"^@で範囲指定を開始したのち、停止"
                           "位置で`^[p`が投入されたらその場で範囲指定終了（^[w相当）とみなして"
                           "PROGRAMへ移行し、プロンプトにペーストして欲しい\" -- PROGRAM "
                           "specifically, not whatever prompt happened to be suspended before "
                           "entering Copy)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);  // no prompt active before ^O^C

  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Copy);
  const std::string &row0 = display.rowText(0);
  REQUIRE(row0.size() >= 4);
  app.feedByte(0x80, std::nullopt);                              // ^@: mark start at column 0
  for (int i = 0; i < 2; i++) app.feedByte(0x06, std::nullopt);  // extend over 2 columns

  REQUIRE(app.feedByte(0x1b, std::nullopt));  // ESC
  REQUIRE(app.feedByte('p', std::nullopt));   // p -- confirm, exit, go straight to PROGRAM
  CHECK(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.promptActive());
  CHECK(app.promptText() == row0.substr(0, 2));  // the confirmed selection, as the PROGRAM prompt's text
}

TEST_CASE_FIXTURE(Fixture, "^O^C (COPY): GUI mouse drag (copyMouseDown()/copyMouseDrag()/copyMouseUp()) "
                           "selects a range the same way keyboard marking does, is a no-op outside "
                           "Copy mode, and round-trips through ^O^Y just like a keyboard-driven "
                           "selection would (2026-09-07, a direct user request: \"スクロールに関する"
                           "画面ドラッグと同じマウス操作に関して...COPYモードの際にそれは可能"
                           "だろうか？\")") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(display.rowText(0).substr(0, 5) == "hello");  // plain.txt's own known content

  // Outside Copy mode, all three must be harmless no-ops (no crash, no
  // mode/state change) -- these are called unconditionally from
  // main.cpp's DragEvent consumption loop regardless of currentMode().
  app.copyMouseDown(0, 0);
  app.copyMouseDrag(0, 3);
  app.copyMouseUp();
  CHECK(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Copy);

  app.copyMouseDown(0, 0);  // press at column 0 -- places the anchor there
  app.copyMouseDrag(0, 5);  // drag to column 5 -- extends the live selection
  CHECK(display.standoutAt(0, 0) == true);
  CHECK(display.standoutAt(0, 4) == true);
  CHECK(display.standoutAt(0, 5) == false);
  app.copyMouseUp();  // release -- 2026-09-07訂正⑩: no longer auto-confirms
  CHECK(app.currentMode() == ApplicationMode::Copy);  // still marking, highlight preserved
  CHECK(display.standoutAt(0, 0) == true);
  CHECK(display.standoutAt(0, 4) == true);
  // An explicit ^[w is now required to confirm the drag-selected range.
  REQUIRE(app.feedByte(0x1b, std::nullopt));
  REQUIRE(app.feedByte('w', std::nullopt));
  CHECK(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::File);

  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  REQUIRE(app.handleCommand(Command::StartFgrep));
  REQUIRE(app.promptActive());
  REQUIRE(app.feedByte(0x0f, std::nullopt));  // ^O
  REQUIRE(app.feedByte(0x19, std::nullopt));  // ^O^Y
  CHECK(app.promptText() == "hello");
}

TEST_CASE_FIXTURE(Fixture, "^O^C (COPY): a mouse click does NOT cancel/downgrade an already-active "
                           "mark (regular or rectangle) -- it only repositions the free end, since "
                           "the mark may have been started by keyboard just before the click "
                           "(2026-09-07, a direct user report ⑪: \"^X␣→矩形モード発動後は矩形で"
                           "ハイライト表示[現状: クリックしたとたん矩形モード解除]\")") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x03, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Copy);

  // Keyboard starts a rectangle mark; a subsequent mouse click must NOT
  // downgrade it back to a regular (linear) mark.
  REQUIRE(app.feedByte(0x18, std::nullopt));  // ^X
  REQUIRE(app.feedByte(' ', std::nullopt));   // SPC
  CHECK(display.rowText(23).find("Rectangle-Mark") != std::string::npos);
  app.copyMouseDown(1, 2);  // a plain click, no drag
  CHECK(display.rowText(23).find("Rectangle-Mark") != std::string::npos);  // still a rectangle mark
}

TEST_CASE_FIXTURE(Fixture, "^O^Y (PASTE) with no active prompt is a harmless no-op, not a crash") {
  REQUIRE(app.start(path()));
  REQUIRE(app.feedByte(0x0f, std::nullopt));
  CHECK_FALSE(app.feedByte(0x19, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

// 2026-09-19追加、直接のユーザー報告("LOCATEもおかしいみたい。^Gは
// 問題無い。^J^L.md^M^S→...→usilic^M^M→戻り値不良→「GREP: usilic
// 」"): 同日のMAN向けの修正(すぐ上のTEST_CASE群参照)と対をなす
// LOCATE版。executeGrep()/openSelectedGrepMatch()自身のLocate分岐と
// grepView_自身のtrailing3のLocate分岐は、その回の修正で既に追加
// 済みだった(実際にこのテストの下半分、Grep結果一覧の"LOCATE: .md"
// 表示は既に正しい)――今回の見落としはlocateView_自身のモードライン
// プロバイダの方: openSelectedGrepMatch()がlocateReturnMode_を
// 一時的にGrepへ上書きしても、locateView_のtrailing2は無条件で
// 常に"LOCATE: <query>"(自己参照的な「今どこにいるか」表示)を
// 出し続けており、実際の^Gの戻り先(Grep結果一覧)を全く反映していな
// かった。
// 2026-09-11更新、直接のユーザー依頼("^Sと^[iをFILE/DIR以外へも反映
// してほしい")によりLocate起点の^S(Basic)もin-place方式になった――
// currentMode_は最初からLocate、専用のGrep一覧へは一度も切り替わら
// ない。以前あった「Enterで一旦Grep結果からLocateへ着地する」という
// 段階は無くなった(マッチへは検索確定と同時に既に着地済み)ので、
// Enter自体の検証(openSelectedLocateResult()、GREPとは独立した既存の
// 機能)はこのテストの関心事から外し、backing-out(マッチを選ばずに
// Backする)の検証に絞る。
TEST_CASE_FIXTURE(Fixture, "GREP started from Locate mode: stays in-place (Locate) with \"LOCATE: .md\" as "
                           "the mode line's origin label, and Back returns straight to Directory") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText(".md");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not installed or no database available -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Locate);
  if (app.activeContentView().lines().empty()) {
    MESSAGE("locate found no '.md' results -- skipping");
    return;
  }

  // Searching the ".md" results for ".md" itself is guaranteed to match
  // every line, regardless of what this machine's locate database
  // happens to contain -- unlike a more "realistic" pattern (e.g. this
  // repo's own name), which depends on updatedb having indexed this
  // particular checkout and is not reproducible across machines.
  REQUIRE(app.handleCommand(Command::StartGrep));
  feedText(".md");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Locate);  // in-place: never left Locate
  REQUIRE_FALSE(app.grepMode().matches().empty());
  CHECK(app.activeContentView().modeLineText().find(".md") != std::string::npos);

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

// 2026-09-19追加、直接のユーザー報告("^J^L.md^M^S→...戻り値空っぽは
// 今も健在"): previousModeName()(drawPromptLabel()自身のtrailing
// フィールド計算が使う、GrepPattern/CutPattern/ManName/LocateQuery
// 全てで共有される汎用ヘルパー)にLocateのケースが一度も無く、default
// の""へ落ちていた。そのため、LOCATEから起動したGREPプロンプトは、
// パターンがまだ未入力/未投入の間(currentMode_はまだLocateのまま、
// grepView_自身のModeLineFieldsProvider――こちらは既にLocate分岐
// 済み――がまだ一切関与しない段階)、trailingフィールドが丸ごと
// 空欄になっていた。このテストはmodeLineText()(ContentView自身の
// 計算、drawPromptLabel()の自前描画とは別物――このバグはmodeLineText()
// 経由では再現しないことを調査で確認済み)ではなく、実際に画面へ
// 描かれた行(display.rowText())を直接見て確認する。
TEST_CASE_FIXTURE(Fixture, "GREP prompt started from Locate mode, before any pattern is typed: the mode "
                           "line's trailing field already shows \"LOCATE: <query>\" (previousModeName() "
                           "used to have no Locate case at all, leaving it blank)") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartLocate));
  feedText(".md");
  if (!app.feedByte(0x0d, std::nullopt)) {
    MESSAGE("locate not installed or no database available -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Locate);

  REQUIRE(app.handleCommand(Command::StartGrep));
  CHECK(app.promptActive());
  const std::string &row = display.rowText(display.rows() - 1);
  CHECK(row.find("LOCATE: .md") != std::string::npos);
}

// 2026-09-19追加、直接のユーザー質問("もう一点、SHELL画面でのGREPは
// 可能なの？"): startGrepPrompt()自体はcurrentMode_ == Grep以外なら
// 常に許可しており(Shellを特に排除していない)、viewForMode(Shell)も
// 既にshell_.scrollback()を返すので、構造上は既に可能なはず――doctest
// で実際に確認した。副産物として、grepView_自身のtrailing3にShell
// 起点の分岐が一切無く(Man/Locate/Duと同じ穴)、検索は成功するのに
// 戻り値欄だけ空欄になる見落としも見つかったので併せて直した。
// 2026-09-11更新、直接のユーザー依頼("^Sと^[iをFILE/DIR以外へも反映
// してほしい")によりShell起点の^S(Basic)もin-place方式になった――
// currentMode_はShellのまま変わらないので、専用の一覧Grep画面(かつて
// その"trailing2"に出ていた"SHELL"という文字列)はもう無い。代わりに
// currentMode_自体がShellのままであること・マッチ件数・trailing3が
// SHELL自身の現在のディレクトリを示すことを確認する。
TEST_CASE_FIXTURE(Fixture, "GREP is possible from SHELL mode: searches the shell's own scrollback in-place, "
                           "without leaving Shell mode") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  app.shell().commandLine().setText("printf 'SHELLGREPTESTMARKER\\n'");
  app.shell().submitLine();
  bool sawOutput = pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("SHELLGREPTESTMARKER") != std::string::npos) return true;
    return false;
  });
  REQUIRE(sawOutput);
  REQUIRE(app.currentMode() == ApplicationMode::Shell);

  REQUIRE(app.handleCommand(Command::StartGrep));
  REQUIRE(app.promptActive());
  feedText("SHELLGREPTESTMARKER");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Shell);
  CHECK(app.grepMode().matches().size() == 1);
  CHECK(app.activeContentView().modeLineText().find("(grep)") != std::string::npos);
}

// 2026-09-20追加、直接のユーザー報告("^O^S/^J^Sが利かない"):
// feedShellByte()自身の新しいisShellSafeGrep分岐のコメント参照。
// FGREP(^O^S)/EGREP(^J^S)は、リーダーシーケンス完了時の事前切り替え
// (「認識されないシーケンスはShellから離れる」という一括処理)に
// 巻き込まれ、dispatchLeaderByte()に到達する前にcurrentMode_が
// shellReturnMode_へ書き換えられてしまっていた――結果、grepReturnMode_
// がShellではなく無関係な発動元のモードを記憶し、検索対象がShellの
// scrollbackではなくそちらになっていた。plainな^S同様、SHELL自身の
// scrollbackが検索対象になることを確認する(EGREPは候補リストを
// 経由するstartRegexPromptList()の形を取るため、プロンプトの代わりに
// ^Gで空のプロンプトへ抜けてから投入する)。
TEST_CASE_FIXTURE(Fixture, "SHELL: ^O^S (FGREP) and ^J^S (EGREP) also search SHELL's own scrollback, not "
                           "whatever mode SHELL was entered from") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartShell));
  app.shell().commandLine().setText("printf 'SHELLGREPTESTMARKER\\n'");
  app.shell().submitLine();
  bool sawOutput = pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("SHELLGREPTESTMARKER") != std::string::npos) return true;
    return false;
  });
  REQUIRE(sawOutput);
  REQUIRE(app.currentMode() == ApplicationMode::Shell);

  // ^O^S: FGREP
  REQUIRE(app.feedByte(0x0f, std::nullopt));
  REQUIRE(app.feedByte(0x13, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Shell);
  CHECK(app.promptActive());
  feedText("SHELLGREPTESTMARKER");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);
  CHECK(app.grepMode().matches().size() == 1);
  CHECK(app.activeContentView().modeLineText().find("SHELL") != std::string::npos);
  REQUIRE(app.handleCommand(Command::Back));  // back to Shell before the next probe
  REQUIRE(app.currentMode() == ApplicationMode::Shell);

  // ^J^S: EGREP (candidate-list based, per startRegexPromptList()) --
  // ^U starts live-editing the pre-filled seed in place (matching the
  // sibling "^R usage-history..." test's own established convention),
  // not ^G (which, from a fresh/unedited list, simply cancels straight
  // back to the origin -- correct behavior, not a bug).
  REQUIRE(app.feedByte(0x0a, std::nullopt));
  REQUIRE(app.feedByte(0x13, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::ListBrowser);
  REQUIRE(app.feedByte(0x15, std::nullopt));  // ^U: start editing, clear the pre-filled seed
  feedText("SHELLGREPTESTMARKER");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Grep);
  CHECK(app.grepMode().matches().size() == 1);
  CHECK(app.activeContentView().modeLineText().find("SHELL") != std::string::npos);
}

// 2026-09-20追加、直接のユーザー報告("SHELLプロンプトで^Sとやっても
// 認識しない（ハイライトのSが表示されるだけ）"): すぐ上のテストは
// handleCommand(Command::StartGrep)を直接呼んでおり、実際の生の^S
// キー入力(0x13)がfeedShellByte()経由で正しくそこまでルーティング
// されることまでは確認していなかった――feedShellByte()自身の新しい
// kControlS分岐のコメント参照。このテストは、実際のバイト入力
// (app.feedByte())だけを使い、^Sが「ハイライトされた制御文字グリフ
// としてコマンドラインへ挿入される」のではなく、実際にGREPプロンプト
// を起動することを確認する。
TEST_CASE_FIXTURE(Fixture, "SHELL: a real ^S keypress (not handleCommand(StartGrep) directly) actually "
                           "triggers GREP, instead of being inserted into the command line as a literal "
                           "control-character glyph") {
  REQUIRE(app.start(path()));
  if (!app.handleCommand(Command::StartShell)) {
    MESSAGE("bash not available or handshake failed -- skipping");
    return;
  }
  REQUIRE(app.currentMode() == ApplicationMode::Shell);

  // 2026-09-11更新: このテスト自身の本来の関心事(生の^Sバイトが実際に
  // GREPプロンプトを起動すること)には影響しないが、in-place方式では
  // マッチ0件だとfeedByte(0x0d)自体がfalseを返す(プロンプトを開いた
  // まま、Directory/File自身の既存のNo String仕様と同じ)ため、確実に
  // 一致するパターンが要る――実際にコマンドを1つ実行しておく。
  app.shell().commandLine().setText("printf 'SHELLGREPTESTMARKER\\n'");
  app.shell().submitLine();
  REQUIRE(pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("SHELLGREPTESTMARKER") != std::string::npos) return true;
    return false;
  }));

  REQUIRE(app.feedByte(0x13, std::nullopt));  // ^S
  // startGrepPrompt()自身のコメント参照――他のGroup1プロンプト
  // (MAN/CUT等)と同じく、ここではまだプロンプトを「開く」だけで、
  // currentMode_はパターン投入までShellのまま――すぐ下でパターンを
  // 投入して初めてin-placeのGREPセッションが始まる(専用のGrep一覧
  // モードへは切り替わらない)。
  CHECK(app.currentMode() == ApplicationMode::Shell);
  CHECK(app.promptActive());
  CHECK(app.shell().commandLine().text().empty());  // NOT inserted as a literal control-char glyph

  feedText("SHELLGREPTESTMARKER");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  CHECK(app.currentMode() == ApplicationMode::Shell);
  CHECK(app.grepMode().matches().size() == 1);
}

// 2026-09-20追加、直接のユーザー報告("ps等実行後、^Sps^Mでせっかく
// プロンプト位置へ移動するのに^Mで無反応は残念"): openSelectedGrepMatch()
// 自身の新しいShell分岐のコメント参照。マッチ選択で着地した「後」の
// Enterが、空の生きたプロンプトを投入する(見た目には何も起きない)
// のではなく、ハイライトされた過去のプロンプト行のコマンドを実際に
// 再実行することを確認する。
TEST_CASE_FIXTURE(Fixture, "SHELL: Enter on a GREP match found in SHELL's own scrollback re-runs the matched "
                           "historical prompt line's command, instead of silently submitting an empty live "
                           "prompt") {
  REQUIRE(app.start(path()));
  REQUIRE(app.handleCommand(Command::StartShell));
  app.shell().commandLine().setText("printf 'SHELLGREPTESTMARKER\\n'");
  app.shell().submitLine();
  bool sawOutput = pumpUntil(app, [&] {
    for (const auto &line : app.shell().scrollback().lines())
      if (line.text.find("SHELLGREPTESTMARKER") != std::string::npos) return true;
    return false;
  });
  REQUIRE(sawOutput);
  REQUIRE(app.currentMode() == ApplicationMode::Shell);
  int linesBeforeRerun = static_cast<int>(app.shell().scrollback().lines().size());

  REQUIRE(app.feedByte(0x13, std::nullopt));  // ^S
  feedText("SHELLGREPTESTMARKER");
  REQUIRE(app.feedByte(0x0d, std::nullopt));  // confirms the search -- lands in-place, no separate Grep screen
  // 2026-09-11更新、直接のユーザー依頼("^Sと^[iをFILE/DIR以外へも
  // 反映してほしい")によりShell起点の^S(Basic)もin-place方式になった
  // ため、currentMode_は最初からShellのまま(以前あった「一覧Grepへ
  // 切り替わり、そこでEnterして戻ってくる」という中間段階は無くなった)。
  // activateGrepInPlace()自身のshell_.beginHistoryBrowsing()呼び出し
  // (openSelectedGrepMatch()自身のShell分岐から複製)により、この時点で
  // 既にhistoryBrowsing()はtrueになっている。
  REQUIRE(app.currentMode() == ApplicationMode::Shell);
  REQUIRE(app.grepMode().matches().size() == 1);
  CHECK(app.shell().historyBrowsing());

  // 実際の生のEnterキー入力(feedShellByte()自身のkCarriageReturnケース
  // ――handleCommand(Command::Enter)にはShellの分岐が無いので、raw
  // byteでの投入が必須)が、ハイライトされたコマンドを実際に再実行
  // することを確認する(空の生きたプロンプトを黙って投入するのでは
  // なく)。
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  bool sawSecondRun = pumpUntil(app, [&] {
    return static_cast<int>(app.shell().scrollback().lines().size()) > linesBeforeRerun + 1;
  });
  CHECK(sawSecondRun);
}

// 2026-09-19六訂正、直接のユーザー報告の完全な手順を再現("CLAUDE.mdを
// 選択→^Jd→...→^S→...→doc^M→...→^M→...→^G連打で行き来ループ"):
// 一連の表示バグ(このファイル自身の直前の複数のテスト参照)を修正した
// 結果、この完全な連鎖を実際に再現してみても^Gの繰り返しはループせず
// File→Directoryへ正しく収束することを確認する――報告時点の「ループ」
// は、根本原因である表示バグ群(各画面の戻り値が食い違っていたこと)
// の症状の一部だった可能性が高い。
// 2026-09-11更新、直接のユーザー依頼("^Sと^[iをFILE/DIR以外へも反映
// してほしい")によりDu起点の^S(Basic)もin-place方式になった――以前は
// 「Grep結果画面→Enter→Du→Back→Grep→Back→Du→Back→File→...→Directory」
// という、専用のGrep一覧を経由する長い連鎖だったが、in-place方式では
// currentMode_が最初から一度もDuを離れないため、連鎖そのものが1段
// 短くなる: Du(in-place)→Enter(マッチしたDUエントリを開く、ここでは
// nested.txtという別のファイル)→Back→Du→Back→(Duの起点である)元の
// File→Back→Directory。
TEST_CASE_FIXTURE(Fixture, "GREP started from Du mode (Du itself started from File): the full reported "
                           "chain converges to File then Directory without looping") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string filePath = app.fileMode().currentFile();

  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);
  CHECK(app.activeContentView().modeLineText().find(filePath) != std::string::npos);
  if (app.activeContentView().lines().empty()) {
    MESSAGE("du produced no entries -- skipping");
    return;
  }
  std::string knownDuLine =
      std::max_element(app.activeContentView().lines().begin(), app.activeContentView().lines().end(),
                        [](const auto &a, const auto &b) { return a.text.size() < b.text.size(); })
          ->text;

  REQUIRE(app.handleCommand(Command::StartGrep));
  feedText(knownDuLine);
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  REQUIRE(app.currentMode() == ApplicationMode::Du);  // in-place: never left Du

  REQUIRE(app.handleCommand(Command::Enter));  // opens the matched DU entry as a file
  CHECK(app.currentMode() == ApplicationMode::File);

  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Du);
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == filePath);  // back to Du's own origin file
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  // Mashing further must NOT bounce back into Du/File -- it should
  // settle on Directory and stay there.
  for (int i = 0; i < 2; i++) app.handleCommand(Command::Back);
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

// 2026-09-19八訂正、直接のユーザー報告(①②③という通し番号付き)
// "②戻り値不良"+"^G連打で①③行き来ループ"への対応:
//
// ②(still-open GREP prompt trailing): previousModeName(Du)自身が
// "DU: "接頭辞付きの文字列を返すようになっていた(直前のコミット)が、
// drawPromptLabel()自身の末尾切り詰めロジックが、CutContentだけを
// 特別扱いして丸ごと切り詰めを免除する一方、Man/Locate/Duは素の長い
// パスと同じ「末尾だけを残す」切り詰めに通してしまい、接頭辞
// ("DU: ")自体が真っ先に食べられていた(MAN/LOCATEの値がたまたま
// 短く、この同じ穴が見過ごされていた)。値部分だけを切り詰め、
// 接頭辞は必ず残すよう一般化して修正。
//
// ①③ループ: GREPの結果からDU内のマッチへ着地した「後」、その場で
// さらにEnterを押して「別の」ファイルを直接開く(openSelectedDuEntry()、
// 今回のグレップ関連の遷移とは別個の既存機能)と、これが共有された
// fileMode_/fileReturnMode_を無関係な内容へ上書きしてしまい、
// duReturnStack_の恒久的な基点(File)まで戻ってきても、その基点が
// 指すべき「本来のファイル」ではなく後から開いた別のファイルを
// 指したままになっていた――AnchorSnapshot(application.hpp冒頭の
// 宣言参照)で、基点解決の瞬間にfileMode_の中身とfileReturnMode_を
// 凍結しておき、最後の1段まで戻ってきた時に明示的に復元するよう
// 修正。
TEST_CASE_FIXTURE(Fixture, "DU started from File, GREP a match, open a DIFFERENT file via the matched DU "
                           "entry itself, then Back repeatedly: converges to the ORIGINAL file and then "
                           "Directory, without bouncing between Du and File forever") {
  fs::path docFile = root / "subdir" / "doc-reviewer.md";
  { std::ofstream(docFile) << "some content\n"; }

  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string filePath = app.fileMode().currentFile();

  REQUIRE(app.handleCommand(Command::StartDu));  // ①
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);
  CHECK(app.activeContentView().modeLineText().find(filePath) != std::string::npos);

  REQUIRE(app.handleCommand(Command::StartGrep));  // ②
  REQUIRE(app.promptActive());
  // プロンプト行自体は編集可能なフィールドと同じ行を分け合うため、
  // 末尾切り詰めの予算がmodeLineText()よりずっと厳しく(接頭辞
  // "DU: "自身は必ず生き残るが、その後の値側はテスト用の一時
  // ディレクトリの絶対パス全体ほど長くは収まらない)、ここではフル
  // パスの完全一致ではなく、接頭辞と(短いので予算内に必ず収まる)
  // ファイル名自体の生存を確認する――checkpoint②が実際に検証したい
  // 「接頭辞が食べられていないか」を、この予算差に左右されずに見る。
  {
    std::string promptRow = display.rowText(display.rows() - 1);
    CHECK(promptRow.find("DU: ") != std::string::npos);
    CHECK(promptRow.find("plain.txt") != std::string::npos);
  }

  feedText("doc");
  REQUIRE(app.feedByte(0x0d, std::nullopt));
  // 2026-09-11更新、直接のユーザー依頼("^Sと^[iをFILE/DIR以外へも反映
  // してほしい")によりDu起点の^S(Basic)もin-place方式になった――
  // 以前は「Enterで一旦Grep結果からDuの選択済みエントリへ着地し、
  // さらにもう一度Enterでそれをファイルとして開く」という2段階だった
  // が、in-place方式ではGREP確定の時点で既にDu自身の上でマッチが
  // 選択済みのため、この2段階が1回のEnterへ収束する――そのEnter自体が
  // 直接doc-reviewer.md(マッチしたDUエントリ自身、起点のplain.txtとは
  // 別のファイル)を開く。
  REQUIRE(app.currentMode() == ApplicationMode::Du);  // in-place: never left Du
  CHECK(app.duMode().selectedEntry() != nullptr);
  CHECK(app.duMode().selectedEntry()->path == docFile.string());

  REQUIRE(app.handleCommand(Command::Enter));  // opens doc-reviewer.md itself, a DIFFERENT file
  REQUIRE(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == docFile.string());

  // Mash Back repeatedly: Du -> the ORIGINAL file (NOT doc-reviewer.md)
  // -> Directory. Must NOT loop between Du and File forever.
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Du);
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == filePath);  // the ORIGINAL file, not doc-reviewer.md
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
  for (int i = 0; i < 3; i++) app.handleCommand(Command::Back);
  CHECK(app.currentMode() == ApplicationMode::Directory);
}

// 2026-09-20追加、直接のユーザー報告("^Jdは直ったが^J^Dが手つかずな
// 感じ"): redisplayDu()自身のコメント参照――既存のDUセッションが
// ある間に別のファイルへ移動してから^J^D(RedisplayDu)を押すと、
// 以前はduReturnStack_/duAnchor_を一切再計算せず、古いセッションの
// 基点のまま固定されてしまっていた。startManPrompt()の「まだそのモード
// にいない時だけ再アンカー」というルールをここにも揃えたので、
// ^G(Back)は「DUを最初に起動した場所」ではなく「今回^J^Dを押した
// 場所」(この2つ目のファイル)へ戻るべき。
TEST_CASE_FIXTURE(Fixture, "RedisplayDu (^J^D) re-anchors to the CURRENT file/directory even when an "
                           "older DU session already exists from a different location") {
  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string firstFile = app.fileMode().currentFile();

  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);

  // Back out to Directory, then open a DIFFERENT file.
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::Directory);
  app.handleCommand(Command::GoToTop);
  idx = indexOf("log.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string secondFile = app.fileMode().currentFile();
  REQUIRE(secondFile != firstFile);

  // A DU session from `firstFile` still exists; RedisplayDu (^J^D)
  // must re-anchor to `secondFile`, the CURRENT location, not the
  // stale first one.
  REQUIRE(app.handleCommand(Command::RedisplayDu));
  REQUIRE(app.currentMode() == ApplicationMode::Du);
  CHECK(app.activeContentView().modeLineText().find(secondFile) != std::string::npos);
  CHECK(app.activeContentView().modeLineText().find(firstFile) == std::string::npos);

  REQUIRE(app.handleCommand(Command::Back));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == secondFile);
}

// 2026-09-20追加、予防的な修正(直接のユーザー報告はLOCATE限定
// だったが、runDuOnCurrentFolder()/redisplayDu()自身のstillInDuRealm
// 分岐のコメント参照――openSelectedDuEntry()もfileReturnMode_ =
// ApplicationMode::Duを全く同じ形で設定するため、DUでも全く同じ
// 再現手順・同じ①③ループが起こり得ることが判明したため、LOCATE版の
// 修正と揃えて先回りで塞いだ): DU結果を直接Fileとして開き、その
// Fileから「もう一度」^Jd(StartDu)を発動しても、中間のFileを飛ばして
// 最初の(DU発動前の)本来のFileへ収束することを確認する。
TEST_CASE_FIXTURE(Fixture, "DU started from a File that was ITSELF reached via a previous DU entry: "
                           "re-invoking DU (^Jd) skips the intermediate File entirely, converging straight "
                           "back to the ORIGINAL anchor without bouncing between File and Du forever") {
  fs::path target1 = root / "du_target1.txt";
  { std::ofstream(target1) << "one"; }
  fs::path target2 = root / "du_target2.txt";
  { std::ofstream(target2) << "two"; }
  configuration.setCommandForTest(ConfiguredRole::Du,
                                   {{"python3", "-c", "print('4\\t" + target1.string() + "')"}, std::nullopt});

  REQUIRE(app.start(path()));
  int idx = indexOf("plain.txt");
  while (indexOf(app.directoryMode().selectedEntry()->name) != idx) app.handleCommand(Command::MoveDown);
  REQUIRE(app.handleCommand(Command::Enter));
  REQUIRE(app.currentMode() == ApplicationMode::File);
  std::string originalFile = app.fileMode().currentFile();

  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);
  REQUIRE(app.duMode().entries().size() == 1);
  REQUIRE(app.duMode().entries()[0].path == target1.string());

  REQUIRE(app.handleCommand(Command::Enter));  // opens target1 directly as File
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.fileMode().currentFile() == target1.string());

  // Nested invocation: DU again, from this SAME File (itself reached
  // via the first DU).
  configuration.setCommandForTest(ConfiguredRole::Du,
                                   {{"python3", "-c", "print('8\\t" + target2.string() + "')"}, std::nullopt});
  REQUIRE(app.handleCommand(Command::StartDu));
  waitForDu(app);
  REQUIRE(app.currentMode() == ApplicationMode::Du);
  REQUIRE(app.duMode().entries().size() == 1);
  REQUIRE(app.duMode().entries()[0].path == target2.string());

  REQUIRE(app.handleCommand(Command::Enter));  // opens target2 directly as File
  REQUIRE(app.currentMode() == ApplicationMode::File);
  REQUIRE(app.fileMode().currentFile() == target2.string());

  // Mash Back: File(target2) -> Du (2nd session's own results) ->
  // straight to the ORIGINAL file (target1 is skipped entirely) ->
  // Directory. Must NOT loop between File and Du.
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Du);
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::File);
  CHECK(app.fileMode().currentFile() == originalFile);  // NOT target1
  CHECK(app.handleCommand(Command::Back));
  CHECK(app.currentMode() == ApplicationMode::Directory);
}
