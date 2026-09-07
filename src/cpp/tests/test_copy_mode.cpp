#include "copy_mode.hpp"

#include "content_view.hpp"
#include "doctest.h"
#include "recording_display.hpp"

using fm::ContentView;
using fm::CopyMode;
using fm::test::RecordingDisplay;

namespace {

std::vector<ContentView::Line> makeLines(std::initializer_list<std::string> texts) {
  std::vector<ContentView::Line> lines;
  for (const auto &t : texts) lines.push_back({t, std::nullopt, std::nullopt, {}});
  return lines;
}

}  // namespace

TEST_CASE("CopyMode: loadSnapshot() draws the snapshot's own text unchanged and starts the cursor "
          "at (0, 0)") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"first line", "second line"}));
  CHECK(d.rowText(0).substr(0, 10) == "first line");
  CHECK(d.rowText(1).substr(0, 11) == "second line");
  CHECK(d.cursorRow() == 0);
  CHECK(d.cursorCol() == 0);
}

TEST_CASE("CopyMode: moveRight()/moveLeft() step the real caret one UTF-8 character at a time, "
          "clamped at both edges of the current row") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"abc"}));
  copy.moveLeft();  // already at column 0 -- no-op, must not go negative
  CHECK(d.cursorCol() == 0);
  copy.moveRight();
  CHECK(d.cursorCol() == 1);
  copy.moveRight();
  copy.moveRight();
  CHECK(d.cursorCol() == 3);  // end of "abc"
  copy.moveRight();           // past the end -- no-op
  CHECK(d.cursorCol() == 3);
  copy.moveLeft();
  CHECK(d.cursorCol() == 2);
}

TEST_CASE("CopyMode: moveRight()/moveLeft() treat a multi-byte UTF-8 character (a 2-column CJK "
          "character here) as one step, matching legacy's own count_i()/count_h() UTF-8-aware "
          "cursor arithmetic rather than stepping by raw byte") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  // "a" + U+65E5 ("日", 3 bytes / 2 display columns) + "b"
  copy.loadSnapshot(makeLines({"a\xe6\x97\xa5" "b"}));
  CHECK(d.cursorCol() == 0);
  copy.moveRight();  // past 'a'
  CHECK(d.cursorCol() == 1);
  copy.moveRight();  // past the whole 3-byte/2-column kanji in ONE step
  CHECK(d.cursorCol() == 3);
  copy.moveRight();  // past 'b'
  CHECK(d.cursorCol() == 4);
  copy.moveLeft();  // back over 'b'
  CHECK(d.cursorCol() == 3);
  copy.moveLeft();  // back over the kanji in ONE step, not stopping mid-byte
  CHECK(d.cursorCol() == 1);
}

TEST_CASE("CopyMode: moveToLineStart()/moveToLineEnd() jump within the current row") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"hello"}));
  copy.moveToLineEnd();
  CHECK(d.cursorCol() == 5);
  copy.moveToLineStart();
  CHECK(d.cursorCol() == 0);
}

TEST_CASE("CopyMode: moveUp()/moveDown() move rows and are bounds-checked at the top/bottom edge, "
          "preserving the current display column across the row change") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"aaaaa", "bb", "ccccc"}));
  copy.moveUp();  // already at row 0 -- no-op
  CHECK(d.cursorRow() == 0);
  copy.moveRight();
  copy.moveRight();
  copy.moveRight();  // column 3 on row 0 ("aaaaa")
  copy.moveDown();   // row 1 ("bb") is shorter -- column clamps to its own end
  CHECK(d.cursorRow() == 1);
  CHECK(d.cursorCol() == 2);
  copy.moveDown();  // row 2 ("ccccc") -- column 3 preserved again from before the clamp... actually
                     // from row 1's clamped column (2), not the original 3
  CHECK(d.cursorRow() == 2);
  CHECK(d.cursorCol() == 2);
  copy.moveDown();  // already at the last row -- no-op
  CHECK(d.cursorRow() == 2);
}

TEST_CASE("CopyMode: startMark()+confirmMark() select a same-row substring into clipboardText(), and "
          "an empty selection (mark start immediately followed by confirm at the same column) leaves "
          "a prior clipboardText() unchanged, mirroring legacy's own `if (*p) strcpy(...)` guard") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"select me please"}));
  CHECK(copy.clipboardText().empty());

  copy.moveRight();  // column 1
  copy.startMark();
  CHECK(copy.markActive());
  for (int i = 0; i < 6; i++) copy.moveRight();  // columns 1..7 -- "elect "
  copy.confirmMark();
  CHECK_FALSE(copy.markActive());
  CHECK(copy.clipboardText() == "elect ");

  // Empty selection: mark then immediately confirm at the same position.
  copy.startMark();
  copy.confirmMark();
  CHECK(copy.clipboardText() == "elect ");  // unchanged
}

TEST_CASE("CopyMode: startMark() while already marking just resets the anchor to the current position "
          "(emacs' repeated set-mark-command), rather than confirming") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"select me please"}));

  copy.moveRight();  // column 1
  copy.startMark();
  for (int i = 0; i < 3; i++) copy.moveRight();  // column 4
  copy.startMark();                              // anchor moves to column 4, still marking
  CHECK(copy.markActive());
  for (int i = 0; i < 3; i++) copy.moveRight();  // column 7
  copy.confirmMark();
  CHECK(copy.clipboardText() == "ct ");  // columns 4..7, not 1..7
}

TEST_CASE("CopyMode: confirmMark() without an active mark is a safe no-op") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"select me please"}));
  copy.confirmMark();
  CHECK(copy.clipboardText().empty());
  CHECK_FALSE(copy.markActive());
}

TEST_CASE("CopyMode: moveUp()/moveDown() are ALLOWED while a mark is active, spanning the selection "
          "across multiple rows (2026-09-07, a direct user request: \"次はCOPYだな。旧の行指定は "
          "忘れていい\" -- legacy's own single-row selection model, ported verbatim from code.c's "
          "`if (!f_copy && row) vt_move(...)`, is deliberately NOT carried over here)") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"row zero", "row one", "row two"}));
  copy.moveRight();
  copy.moveRight();
  copy.startMark();  // anchor at (row 0, col 2)
  copy.moveDown();
  copy.moveDown();
  CHECK(d.cursorRow() == 2);  // no longer blocked
  copy.moveRight();
  CHECK(d.cursorCol() == 3);
  copy.confirmMark();
  // Row 0 from the anchor column onward, all of row 1, row 2 up to the cursor column.
  CHECK(copy.clipboardText() == "w zero\nrow one\nrow");
}

TEST_CASE("CopyMode: an empty snapshot leaves every operation a safe no-op") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot({});
  copy.moveUp();
  copy.moveDown();
  copy.moveLeft();
  copy.moveRight();
  copy.moveToLineStart();
  copy.moveToLineEnd();
  copy.startMark();
  copy.confirmMark();
  CHECK(copy.clipboardText().empty());
  CHECK_FALSE(copy.markActive());
}

TEST_CASE("CopyMode: placeCursorAt() jumps to an absolute row/column when not marking -- the GUI "
          "mouse-drag range-selection support (2026-09-07, a direct user request)") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"aaaaa", "bb", "ccccc"}));
  copy.placeCursorAt(2, 3);
  CHECK(d.cursorRow() == 2);
  CHECK(d.cursorCol() == 3);
  // Column past the row's own end clamps to the end, same as moveToLineEnd().
  copy.placeCursorAt(1, 99);
  CHECK(d.cursorRow() == 1);
  CHECK(d.cursorCol() == 2);  // "bb" is only 2 columns wide
}

TEST_CASE("CopyMode: placeCursorAt() honors `row` even while a mark is active, letting a mouse drag "
          "that wanders onto a different row extend the selection across rows (2026-09-07, matching "
          "moveUp()/moveDown()'s own removal of the single-row restriction)") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"select me please", "a different row"}));
  copy.placeCursorAt(0, 1);
  copy.startMark();
  copy.placeCursorAt(1, 7);  // drag wandered onto row 1 -- now honored, not ignored
  CHECK(d.cursorRow() == 1);
  CHECK(d.cursorCol() == 7);
  copy.confirmMark();
  CHECK(copy.clipboardText() == "elect me please\na diffe");
}

TEST_CASE("CopyMode: the live selection renders as a Standout AttributeRun over exactly the marked "
          "byte range while marking is in progress") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"plain SELECT plain"}));
  for (int i = 0; i < 6; i++) copy.moveRight();  // column 6, start of "SELECT"
  copy.startMark();
  for (int i = 0; i < 6; i++) copy.moveRight();  // column 12, end of "SELECT"
  CHECK(d.standoutAt(0, 5) == false);
  CHECK(d.standoutAt(0, 6) == true);
  CHECK(d.standoutAt(0, 11) == true);
  CHECK(d.standoutAt(0, 12) == false);
}

TEST_CASE("CopyMode: startOrUpgradeRectangleMark() starts a new rectangle mark from scratch (emacs' "
          "own C-x SPC, which can begin rectangle-mark-mode without a pre-existing mark) -- "
          "2026-09-07, a direct user request: \"①emacsには矩形範囲指定の機能があるのであれを移植して"
          "ほしい\"") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"abcdef", "ghijkl", "mnopqr"}));
  CHECK_FALSE(copy.markActive());
  copy.moveRight();
  copy.moveRight();               // column 2
  copy.startOrUpgradeRectangleMark();
  CHECK(copy.markActive());
  CHECK(copy.rectangleMarkActive());
}

TEST_CASE("CopyMode: startOrUpgradeRectangleMark() upgrades an ALREADY active (regular) mark in "
          "place, keeping its anchor -- lets a keyboard startMark() (or a GUI mouse-down, which also "
          "starts a regular mark) be promoted to a rectangle without losing where it began") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"abcdef", "ghijkl"}));
  copy.moveRight();
  copy.startMark();  // regular mark, anchor at (row 0, col 1)
  CHECK_FALSE(copy.rectangleMarkActive());
  copy.startOrUpgradeRectangleMark();
  CHECK(copy.rectangleMarkActive());
  copy.moveDown();
  copy.moveRight();
  copy.moveRight();  // (row 1, col 3)
  copy.confirmMark();
  // Rectangle spans columns [1,3) on rows 0..1, not the anchor's original row alone.
  CHECK(copy.clipboardText() == "bc\nhi");
}

TEST_CASE("CopyMode: confirmMark() on a rectangle mark extracts each row's own [loCol, hiCol) "
          "display-column slice and joins them with newlines, matching emacs' own "
          "copy-rectangle-as-kill() -- independent of each row's own text length/content") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"0123456789", "ABCDEFGHIJ", "abcdefghij"}));
  for (int i = 0; i < 3; i++) copy.moveRight();  // column 3
  copy.startOrUpgradeRectangleMark();
  copy.moveDown();
  copy.moveDown();
  for (int i = 0; i < 3; i++) copy.moveRight();  // (row 2, column 6)
  copy.confirmMark();
  CHECK(copy.clipboardText() == "345\nDEF\ndef");
}

TEST_CASE("CopyMode: a rectangle mark's live selection renders as a genuine per-row column-range "
          "Standout highlight (a true rectangle), NOT a linear reading-order range -- the middle "
          "row's own highlight does not extend to either edge of the row") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"0123456789", "ABCDEFGHIJ", "abcdefghij"}));
  for (int i = 0; i < 3; i++) copy.moveRight();  // column 3
  copy.startOrUpgradeRectangleMark();
  copy.moveDown();
  copy.moveDown();
  for (int i = 0; i < 3; i++) copy.moveRight();  // (row 2, column 6)
  // Row 1 (a "middle" row of the rectangle): only columns [3,6) are highlighted,
  // unlike a linear selection which would highlight the WHOLE middle row.
  CHECK(d.standoutAt(1, 2) == false);
  CHECK(d.standoutAt(1, 3) == true);
  CHECK(d.standoutAt(1, 5) == true);
  CHECK(d.standoutAt(1, 6) == false);
}

TEST_CASE("CopyMode: a rectangle mark shows \"Rectangle-Mark\" as the mode line's position field "
          "while active, and reverts once the mark ends (2026-09-07, a direct user request: "
          "\"モードラインのメッセージエリアにアンダーラインの`Rectangle-Mark`と表示\" -- the "
          "underline styling itself is not implemented, see this feature's own commit message)") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"abcdef"}));
  // viewport(0, 5) -- the mode line renders at row 5 (row0_ + height_).
  CHECK(d.rowText(5).find("Rectangle-Mark") == std::string::npos);
  copy.startOrUpgradeRectangleMark();
  CHECK(d.rowText(5).find("Rectangle-Mark") != std::string::npos);
  copy.confirmMark();
  CHECK(d.rowText(5).find("Rectangle-Mark") == std::string::npos);
}

TEST_CASE("CopyMode: startMark() (a regular, non-rectangle mark) downgrades an active rectangle "
          "mark back to linear selection, resetting the anchor to the current position") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"abcdef", "ghijkl"}));
  copy.startOrUpgradeRectangleMark();
  CHECK(copy.rectangleMarkActive());
  copy.startMark();
  CHECK(copy.markActive());
  CHECK_FALSE(copy.rectangleMarkActive());
}
