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

TEST_CASE("CopyMode: moveUp()/moveDown() are blocked entirely while a mark is active (ported from "
          "code.c's own `if (!f_copy && row) vt_move(...)` -- the row variable simply never updates "
          "while marking, so a selection can never span more than one row)") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  CopyMode copy(v);
  copy.loadSnapshot(makeLines({"row zero", "row one"}));
  copy.startMark();
  copy.moveDown();
  CHECK(d.cursorRow() == 0);  // blocked
  copy.moveRight();
  copy.moveRight();
  CHECK(d.cursorRow() == 0);  // horizontal movement still works while marking
  CHECK(d.cursorCol() == 2);
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
