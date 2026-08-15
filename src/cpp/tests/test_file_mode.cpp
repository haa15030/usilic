#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "content_view.hpp"
#include "doctest.h"
#include "file_mode.hpp"
#include "recording_display.hpp"

namespace fs = std::filesystem;

using fm::Charset;
using fm::FileMode;
using fm::History;
using fm::PositionCategory;
using fm::test::RecordingDisplay;

namespace {

fs::path makeTempDir() {
  char tmpl[] = "/tmp/filemode_testXXXXXX";
  char *result = mkdtemp(tmpl);
  REQUIRE(result != nullptr);
  return fs::path(result);
}

struct Fixture {
  fs::path root;
  RecordingDisplay display{24, 80};
  fm::ContentView view{display};
  History history;
  FileMode mode{view, history, Charset::Encoding::Ascii, 80};

  Fixture() : root(makeTempDir()) {}
  ~Fixture() { fs::remove_all(root); }

  std::string writeFile(const std::string &name, const std::string &content) {
    std::string path = (root / name).string();
    std::ofstream(path, std::ios::binary) << content;
    return path;
  }
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "loadFile: a plain text file's lines are displayed") {
  std::string path = writeFile("plain.txt", "hello\nworld\n");
  REQUIRE(mode.loadFile(path));
  CHECK(mode.currentFile() == path);
  REQUIRE(view.lines().size() == 2);
  CHECK(view.lines()[0].text == "hello");
  CHECK(view.lines()[1].text == "world");
}

TEST_CASE_FIXTURE(Fixture, "loadFile: nroff-style overstrike bold (\"X\\bX\") carries through as an "
                           "AttributeRun on the resulting Line, not just de-overstruck plain text "
                           "(2026-08-05: PipeBuffer already detected this, FileMode was the one place "
                           "that discarded it before it ever reached the screen)") {
  std::string path = writeFile("bold.txt", "plain B\bBO\bOL\bLD\bD plain\n");
  REQUIRE(mode.loadFile(path));
  REQUIRE(view.lines().size() == 1);
  CHECK(view.lines()[0].text == "plain BOLD plain");
  // One AttributeRun PER overstruck character (nroff emits "X\bX" once
  // per bolded character, not once per bolded WORD -- PipeBuffer's own
  // wrapAttrs_.push_back() doesn't merge adjacent same-attribute runs,
  // confirmed by reading its source directly rather than assumed), so
  // "BOLD" is 4 separate 1-column runs at columns 6,7,8,9 -- not one
  // run spanning [6,10).
  REQUIRE(view.lines()[0].attributes.size() == 4);
  for (int i = 0; i < 4; i++) {
    CHECK(view.lines()[0].attributes[i].attribute == fm::Display::Attribute::Bold);
    CHECK(view.lines()[0].attributes[i].start == 6 + i);
    CHECK(view.lines()[0].attributes[i].end == 7 + i);
  }
}

TEST_CASE_FIXTURE(Fixture, "loadFile: a nonexistent file fails cleanly") {
  CHECK_FALSE(mode.loadFile((root / "does-not-exist.txt").string()));
}

TEST_CASE_FIXTURE(Fixture, "loadFile: shows a real display cursor at the current line, since plain file "
                            "content has no whole-line highlight of its own (2026-08-04 user request)") {
  view.setViewport(0, 10);
  std::string path = writeFile("plain.txt", "line0\nline1\nline2\n");
  REQUIRE(mode.loadFile(path));
  CHECK_FALSE(view.cursorHighlightEnabled());  // ported behavior, unchanged by this request
  CHECK_FALSE(display.visibleCursorHidden());
  CHECK(display.cursorRow() == view.cursorDisplayRow());
  CHECK(display.cursorCol() == 0);
}

TEST_CASE_FIXTURE(Fixture, "reflow: keeps the visible cursor in sync with the (possibly re-clamped) cursor "
                            "line after a width change") {
  view.setViewport(0, 10);
  std::string path = writeFile("plain.txt", "line0\nline1\nline2\n");
  REQUIRE(mode.loadFile(path));
  view.setCursorIndex(2);
  REQUIRE(mode.reflow(40));
  CHECK_FALSE(display.visibleCursorHidden());
  CHECK(display.cursorRow() == view.cursorDisplayRow());
}

TEST_CASE_FIXTURE(Fixture, "loadFile: long lines wrap at the configured width, via PipeBuffer") {
  fm::ContentView narrowView{display};
  History h2;
  FileMode narrowMode{narrowView, h2, Charset::Encoding::Ascii, 10};
  std::string path = writeFile("long.txt", "abcdefghijklmno\n");

  REQUIRE(narrowMode.loadFile(path));
  REQUIRE(narrowView.lines().size() == 2);
  CHECK(narrowView.lines()[0].text == "abcdefghij");
  CHECK(narrowView.lines()[1].text == "klmno");
}

TEST_CASE_FIXTURE(Fixture, "loadFile: UTF-8 content round-trips without corruption") {
  std::string path = writeFile("utf8.txt", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\n");  // "日本語\n"
  fm::ContentView utf8View{display};
  History h2;
  FileMode utf8Mode{utf8View, h2, Charset::Encoding::Utf8, 80};

  REQUIRE(utf8Mode.loadFile(path));
  REQUIRE(utf8View.lines().size() == 1);
  CHECK(utf8View.lines()[0].text == "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
}

TEST_CASE_FIXTURE(Fixture, "saveCursorPosition / loadFile: cursor position round-trips through History") {
  std::string path = writeFile("multi.txt", "line1\nline2\nline3\nline4\n");
  REQUIRE(mode.loadFile(path));
  view.setCursorIndex(2);
  mode.saveCursorPosition();

  auto saved = history.positionFor(PositionCategory::File, path);
  REQUIRE(saved.has_value());
  CHECK(saved->row == 2);

  // Re-loading the same file (as if returning to it later) restores
  // the cursor instead of resetting to the top.
  view.setCursorIndex(0);
  REQUIRE(mode.loadFile(path));
  CHECK(view.cursorIndex() == 2);
}

TEST_CASE_FIXTURE(Fixture, "loadFile: an empty file produces no lines and doesn't crash") {
  std::string path = writeFile("empty.txt", "");
  REQUIRE(mode.loadFile(path));
  CHECK(view.lines().empty());
}

TEST_CASE_FIXTURE(Fixture, "saveCursorPosition before any loadFile is a safe no-op") {
  FileMode fresh{view, history, Charset::Encoding::Ascii, 80};
  fresh.saveCursorPosition();  // must not crash
  CHECK(history.positionFor(PositionCategory::File, "").has_value() == false);
}

TEST_CASE_FIXTURE(Fixture, "reflow: re-wraps the current file at the new width") {
  std::string path = writeFile("long.txt", "abcdefghijklmno\n");
  REQUIRE(mode.loadFile(path));
  REQUIRE(view.lines().size() == 1);  // fits in 80 columns unwrapped

  REQUIRE(mode.reflow(10));
  REQUIRE(view.lines().size() == 2);
  CHECK(view.lines()[0].text == "abcdefghij");
  CHECK(view.lines()[1].text == "klmno");

  REQUIRE(mode.reflow(80));
  REQUIRE(view.lines().size() == 1);
  CHECK(view.lines()[0].text == "abcdefghijklmno");
}

TEST_CASE_FIXTURE(Fixture, "reflow: preserves the CURRENT session cursor, not a History-saved one") {
  std::string path = writeFile("multi.txt", "line1\nline2\nline3\nline4\n");
  REQUIRE(mode.loadFile(path));
  // A stale saved position elsewhere in History that reflow() must
  // NOT jump to -- the whole point is preserving THIS session's
  // in-progress scroll position across a resize, unlike loadFile().
  History::CursorPosition stale;
  stale.row = 0;
  history.setPosition(PositionCategory::File, path, stale);

  view.setCursorIndex(3);
  REQUIRE(mode.reflow(80));
  CHECK(view.cursorIndex() == 3);
}

TEST_CASE_FIXTURE(Fixture, "reflow: clamps the cursor if the new width produces fewer lines") {
  std::string path = writeFile("wrap.txt", "abcdefghijklmno\n");
  fm::ContentView narrowView{display};
  History h2;
  FileMode narrowMode{narrowView, h2, Charset::Encoding::Ascii, 10};
  REQUIRE(narrowMode.loadFile(path));
  REQUIRE(narrowView.lines().size() == 2);
  narrowView.setCursorIndex(1);

  REQUIRE(narrowMode.reflow(80));  // now fits on one line
  REQUIRE(narrowView.lines().size() == 1);
  CHECK(narrowView.cursorIndex() == 0);
}

TEST_CASE_FIXTURE(Fixture, "reflow: a no-op (false) when no file is currently loaded") {
  FileMode fresh{view, history, Charset::Encoding::Ascii, 80};
  CHECK_FALSE(fresh.reflow(40));
}

TEST_CASE_FIXTURE(Fixture, "mode-line fields: shows the bare filename, byte size, and directory") {
  std::string path = writeFile("plain.txt", "hello\nworld\n");  // 12 bytes
  REQUIRE(mode.loadFile(path));

  // Ported from disp_window()'s pager case: unlike Directory mode, the
  // name field here is the bare filename (NOT the full path). The byte
  // size and directory are two SEPARATE ModeLineFields::trailing/
  // trailing2 segments (2026-08-10, a real request/pixel comparison:
  // they need their own independent reverse-video highlight runs, like
  // every other mode-line field pair, not one unbroken run) -- as
  // plain text this still reads as "<byte size> <directory>/" with a
  // single space, the same gap width as name/position get, just now
  // via two separately-highlighted segments instead of one string with
  // extra spaces baked in. The byte size itself stays a plain,
  // ungrouped digit run (a separate, earlier misreading of this same
  // request already established and reverted that).
  std::string text = view.modeLineText();
  CHECK(text.rfind("plain.txt ", 0) == 0);
  std::string expectedTrailing = "12 " + root.string() + "/";
  CHECK(text.substr(text.size() - expectedTrailing.size()) == expectedTrailing);
}

TEST_CASE_FIXTURE(Fixture, "mode-line fields: before any loadFile, no fields are shown") {
  FileMode fresh{view, history, Charset::Encoding::Ascii, 80};
  CHECK(view.modeLineText() == "0/0 END");
}

TEST_CASE_FIXTURE(Fixture, "mode-line fields: directory trailing2 uses the full available width when it "
                           "overflows, not legacy's fixed 16-char tail cap") {
  // 2026-08-16 fix: a real report/screenshot (=bk vs =bl, ^X^F on a
  // deeply-nested real path) found this port's own tail truncated to
  // exactly 16 characters ("...39537/Libraries/") while the real CUI
  // showed a far longer tail using whatever room was actually left on
  // the row -- the 16-char cap was only ever meant for a typed search
  // STRING's own fixed-width preview area (DirectoryMode's
  // searchAnnotation_), not a PATH like this one. See
  // ContentView::TrailingKind's own comment (trailing2Kind=Path).
  fs::path deep = root / "some" / "really" / "deeply" / "nested" / "Directory-x86_64-11.1.9.1039537" / "Libraries";
  fs::create_directories(deep);
  std::string path = (deep / "BDF_Driver_License.txt").string();
  { std::ofstream(path) << "x"; }
  REQUIRE(mode.loadFile(path));
  std::string text = view.modeLineText();
  // Still overflows an 80-column row (the temp root alone plus this
  // deep chain is well over 80 characters), so it's still truncated --
  // just with the FULL available tail, not capped at 16. "Libraries/"
  // (10 chars) is well past the old cap's own reach ("39537/Libraries/",
  // the last 16 characters of the path) but comfortably within budget.
  CHECK(text.find("Directory-x86_64-11.1.9.1039537/Libraries/") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "constructing FileMode disables the cursor-line highlight on its view") {
  view.setViewport(0, 5);  // otherwise height_ defaults to 0 and nothing but the mode-line row renders
  std::string path = writeFile("plain.txt", "hello\nworld\n");
  REQUIRE(mode.loadFile(path));
  view.redraw();
  CHECK(display.standoutAt(0, 0) == false);  // cursor is on line 0 -- real CUI has no highlight here
}
