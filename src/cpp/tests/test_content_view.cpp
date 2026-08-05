#include "content_view.hpp"
#include "doctest.h"
#include "recording_display.hpp"

using fm::ContentView;
using fm::test::RecordingDisplay;

namespace {

std::vector<ContentView::Line> makeLines(int n, const std::string &prefix = "line") {
  std::vector<ContentView::Line> lines;
  for (int i = 0; i < n; i++) lines.push_back({prefix + std::to_string(i), std::nullopt, std::nullopt});
  return lines;
}

}  // namespace

TEST_CASE("redraw: shows lines from the top and marks the cursor line") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.redraw();
  CHECK(d.rowText(0).substr(0, 5) == "line0");
  CHECK(d.rowText(1).substr(0, 5) == "line1");
  CHECK(d.rowText(2).substr(0, 5) == "line2");
  CHECK(d.standoutAt(0, 0) == true);   // cursor starts on line 0
  CHECK(d.standoutAt(1, 0) == false);
}

TEST_CASE("redraw: an empty list (e.g. ^R with no history yet) shows a highlighted \"--\" placeholder on "
          "its top row instead of nothing at all -- 2026-08-05 fix, see its own comment") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines({});
  v.redraw();
  CHECK(d.rowText(0).substr(0, 2) == "--");
  CHECK(d.standoutAt(0, 0) == true);
  // The terminal's own real cursor now sits at the placeholder's own
  // right edge too, not hidden/left dangling -- see syncVisibleCursor().
  CHECK_FALSE(d.visibleCursorHidden());
  CHECK(d.cursorRow() == 0);
  CHECK(d.cursorCol() == 2);  // "--".size()
}

TEST_CASE("redraw: an empty list does NOT show the \"--\" placeholder when cursor highlighting is "
          "disabled (File mode's own case -- there's no content to view in the first place there, "
          "and this placeholder is specifically for highlighted SELECTION lists)") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setCursorHighlightEnabled(false);
  v.setLines({});
  v.redraw();
  CHECK(d.rowText(0).substr(0, 2) != "--");  // just clearToEndOfLine()'s blank fill, no placeholder text
  CHECK(d.standoutAt(0, 0) == false);
}

TEST_CASE("setCursorHighlightEnabled(false): the cursor line is no longer reverse-video") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setCursorHighlightEnabled(false);
  v.redraw();
  CHECK(d.standoutAt(0, 0) == false);  // cursor is on line 0, but highlighting is off
  CHECK(d.rowText(0).substr(0, 5) == "line0");  // content itself is unaffected
}

TEST_CASE("Line::highlightStartColumn: cursor-line reverse-video starts at the given column, not column 0") {
  // Ported from scroll.c's get_col()/init_col() (T_dir case) via
  // DirectoryMode::parseLsLine() -- see Line::highlightStartColumn's
  // own comment for the real-screenshot bug this fixes (the whole
  // `ls -alF` permissions/size/date prefix wrongly reverse-videoed
  // along with the filename).
  RecordingDisplay d(10, 60);  // wide enough for the full ls -l-style line below (20 cols was too narrow, clipping the check columns)
  ContentView v(d);
  v.setViewport(0, 3);
  std::vector<ContentView::Line> lines;
  ContentView::Line line;
  line.text = "drwxr-xr-x  1 a  b  0 Jan  1 00:00 name";
  line.highlightStartColumn = 35;  // where "name" starts
  lines.push_back(line);
  v.setLines(lines);
  v.redraw();
  CHECK(d.standoutAt(0, 0) == false);   // prefix stays plain
  CHECK(d.standoutAt(0, 34) == false);  // the space right before "name"
  CHECK(d.standoutAt(0, 35) == true);   // "name" itself is reverse-video
  CHECK(d.standoutAt(0, 38) == true);
}

TEST_CASE("Line::highlightStartColumn: nullopt (the default) still highlights the whole line") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(1));
  v.redraw();
  CHECK(d.standoutAt(0, 0) == true);
  CHECK(d.standoutAt(0, 4) == true);
}

TEST_CASE("redraw: rows past the end of the list are left blank") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(2));
  v.redraw();
  CHECK(d.rowText(2)[0] == ' ');
  CHECK(d.rowText(4)[0] == ' ');
}

TEST_CASE("setBottomAnchored: fewer-than-height lines render flush against the bottom, blank rows above") {
  // Added for Shell's own scrollback_ (2026-08-04) -- see its own
  // comment for why: a live prompt fixed at exactly row0_+height_ only
  // looks right if the last real content line is ALWAYS immediately
  // above it, which top-anchoring (the default, tested elsewhere)
  // can't guarantee until content happens to fill the whole viewport.
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(2));
  v.setBottomAnchored(true);
  v.redraw();
  CHECK(d.rowText(0)[0] == ' ');  // blank padding, not "line0" (unlike the top-anchored default)
  CHECK(d.rowText(1)[0] == ' ');
  CHECK(d.rowText(2)[0] == ' ');
  CHECK(d.rowText(3).substr(0, 5) == "line0");  // flush against the bottom: rows height-2, height-1
  CHECK(d.rowText(4).substr(0, 5) == "line1");
}

TEST_CASE("setBottomAnchored: once lines fill (or exceed) the viewport, behaves exactly like top-anchored") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(5));  // more lines than height -- goToBottom() shows the last 3
  v.setBottomAnchored(true);
  v.goToBottom();
  CHECK(d.rowText(0).substr(0, 5) == "line2");
  CHECK(d.rowText(1).substr(0, 5) == "line3");
  CHECK(d.rowText(2).substr(0, 5) == "line4");
}

TEST_CASE("setBottomAnchored: lineIndexAtRow accounts for the same blank padding redraw() uses") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(2));
  v.setBottomAnchored(true);
  v.redraw();
  CHECK(v.lineIndexAtRow(0) == -1);  // blank padding row
  CHECK(v.lineIndexAtRow(2) == -1);
  CHECK(v.lineIndexAtRow(3) == 0);  // "line0"
  CHECK(v.lineIndexAtRow(4) == 1);  // "line1"
}

TEST_CASE("lineIndexAtRow: maps a display row within the viewport to a line index") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(2, 5);  // viewport starts at display row 2, matching Application's own status-bar-reserved layout
  v.setLines(makeLines(5));
  CHECK(v.lineIndexAtRow(2) == 0);
  CHECK(v.lineIndexAtRow(4) == 2);
  CHECK(v.lineIndexAtRow(6) == 4);
}

TEST_CASE("cursorDisplayRow: the inverse of lineIndexAtRow, -1 when scrolled out of view") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(2, 5);
  v.setLines(makeLines(10));  // more lines than height -- room to scroll topIndex_ away from cursorIndex_
  v.setCursorIndex(2);
  CHECK(v.cursorDisplayRow() == 4);  // row0(2) + cursorIndex(2)

  v.setTopIndex(5);  // scroll cursorIndex_(2) out of view without moving it (see setTopIndex()'s own comment)
  CHECK(v.cursorDisplayRow() == -1);
}

TEST_CASE("cursorDisplayRow: accounts for bottom-anchoring the same way lineIndexAtRow does") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(2));
  v.setBottomAnchored(true);
  v.setCursorIndex(1);
  v.redraw();
  CHECK(v.cursorDisplayRow() == 4);  // "line1" renders at row 4 when bottom-anchored, see its own test
}

TEST_CASE("syncVisibleCursor: moves the display's visible cursor to the highlighted row's own right edge "
          "when highlighting is enabled (the default) -- 2026-08-05 fix, see its own comment: "
          "TerminalDisplay::hideVisibleCursor() is a no-op, so the OLD behavior here (hiding) left the "
          "real terminal cursor wherever the mode-line was last drawn instead") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setCursorIndex(1);
  v.syncVisibleCursor();
  CHECK_FALSE(d.visibleCursorHidden());
  CHECK(d.cursorRow() == 1);          // row0(0) + cursorIndex(1)
  CHECK(d.cursorCol() == 5);          // "line1".size()
}

TEST_CASE("syncVisibleCursor: hides the cursor rather than showing one out of view (scrolled away, "
          "highlighting still enabled)") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(2, 5);
  v.setLines(makeLines(10));
  v.setCursorIndex(2);
  v.setTopIndex(5);  // scrolls cursorIndex_ out of view, see cursorDisplayRow()'s own test
  REQUIRE(v.cursorDisplayRow() == -1);

  v.syncVisibleCursor();
  CHECK(d.visibleCursorHidden());
}

TEST_CASE("syncVisibleCursor: shows a real cursor at cursorDisplayRow() when highlighting is disabled "
          "(File mode's own case)") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(10));
  v.setCursorIndex(3);
  v.setCursorHighlightEnabled(false);
  CHECK_FALSE(v.cursorHighlightEnabled());

  v.syncVisibleCursor();
  CHECK_FALSE(d.visibleCursorHidden());
  CHECK(d.cursorRow() == 3);  // row0(0) + cursorIndex(3)
  CHECK(d.cursorCol() == 0);
}

TEST_CASE("syncVisibleCursor: hides the cursor rather than showing one out of view (scrolled away, "
          "highlighting still disabled)") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(2, 5);
  v.setLines(makeLines(10));
  v.setCursorHighlightEnabled(false);
  v.setCursorIndex(2);
  v.setTopIndex(5);  // scrolls cursorIndex_ out of view, see cursorDisplayRow()'s own test
  REQUIRE(v.cursorDisplayRow() == -1);

  v.syncVisibleCursor();
  CHECK(d.visibleCursorHidden());
}

TEST_CASE("isColumnWithinLineScope: DirectoryMode's fixed left margin excludes columns before "
          "highlightStartColumn (2026-08-05 user proposal: distinguish a click on an entry from a "
          "\"cat's paw\" drag start by WHERE it landed, not by timing)") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  std::vector<ContentView::Line> lines;
  lines.push_back({"drwxr-xr-x  staff  plain.txt", std::nullopt, 20});  // highlight starts at column 20
  v.setLines(lines);

  CHECK_FALSE(v.isColumnWithinLineScope(0, 19));  // one column left of the margin -- not on the entry
  CHECK(v.isColumnWithinLineScope(0, 20));         // the highlight's own first column
  CHECK(v.isColumnWithinLineScope(0, 27));         // still inside "plain.txt"
}

TEST_CASE("isColumnWithinLineScope: a left-aligned list (highlightStartColumn unset) excludes columns "
          "past the end of the entry's own text") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  std::vector<ContentView::Line> lines;
  lines.push_back({"short.txt", std::nullopt, std::nullopt});  // no fixed margin -- highlight starts at 0
  v.setLines(lines);

  CHECK(v.isColumnWithinLineScope(0, 0));           // the very first character
  CHECK(v.isColumnWithinLineScope(0, 8));           // the last character ("short.txt".size() - 1)
  CHECK_FALSE(v.isColumnWithinLineScope(0, 9));     // exactly at the end -- past the last real character
  CHECK_FALSE(v.isColumnWithinLineScope(0, 40));    // well into the empty trailing area of the row
}

TEST_CASE("isColumnWithinLineScope: false for an out-of-range line index") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setLines(makeLines(2));
  CHECK_FALSE(v.isColumnWithinLineScope(-1, 0));
  CHECK_FALSE(v.isColumnWithinLineScope(2, 0));
}

TEST_CASE("setLines: resets topIndex_ to 0, so a stale deep scroll position from the PREVIOUS content "
          "doesn't leave a much-shorter new listing scrolled almost to its own end (2026-08-05 user "
          "reproduction: cpp/(7 entries) -> Enter into src/(74 entries) -> scroll down several lines -> "
          "back up to cpp/ again -- cursorIndex_ itself was already correctly restored to a low, valid "
          "index by the caller's own history-based restore, but the stale topIndex_ from src/'s deep "
          "scroll survived and left cpp/'s own listing showing only its last couple of entries despite "
          "all 7 easily fitting in a tall-enough viewport; note the viewport here is deliberately taller "
          "than cursorIndex_ so clampViewport()'s own bottom-anchor branch does NOT independently explain "
          "topIndex_ landing on 0 -- this test isolates the setLines() reset itself)") {
  RecordingDisplay d(20, 20);
  ContentView v(d);
  v.setViewport(0, 10);
  v.setLines(makeLines(74, "deep"));
  v.setCursorIndex(5);
  v.setTopIndex(50);  // mimics scrolling deep into a long listing, decoupled from cursorIndex_
  REQUIRE(v.topIndex() == 50);

  v.setLines(makeLines(7, "short"));  // a much shorter, unrelated listing replaces it wholesale
  REQUIRE(v.cursorIndex() == 5);      // stays valid/unchanged -- not what forces the reset
  CHECK(v.topIndex() == 0);
}

TEST_CASE("setModeLineFieldsProvider: modeLineText wraps name/trailing around the position text") {
  RecordingDisplay d(10, 60);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setModeLineFieldsProvider([](int cursorIndex) -> ContentView::ModeLineFields {
    return {"file" + std::to_string(cursorIndex) + ".txt", "(ls)", false, ""};
  });
  CHECK(v.modeLineText() == "file0.txt 1/3 TOP (ls)");
  v.setCursorIndex(2);
  CHECK(v.modeLineText() == "file2.txt 3/3 END (ls)");
}

TEST_CASE("setModeLineFieldsProvider: an empty name/trailing is simply omitted") {
  RecordingDisplay d(10, 40);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields { return {"", "", false, ""}; });
  CHECK(v.modeLineText() == "1/3 TOP");

  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields { return {"name.txt", "", false, ""}; });
  CHECK(v.modeLineText() == "name.txt 1/3 TOP");

  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields { return {"", "(ls)", false, ""}; });
  CHECK(v.modeLineText() == "1/3 TOP (ls)");
}

TEST_CASE("setModeLineFieldsProvider: no provider set leaves modeLineText unchanged") {
  RecordingDisplay d(10, 40);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  CHECK(v.modeLineText() == "1/3 TOP");
}

TEST_CASE("setModeLineFieldsProvider: fields that would overflow the display width are dropped, trailing first, not truncated") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));

  // "shortname.txt 1/3 TOP" (21 cols) fits within 30 columns on its
  // own, but adding the long trailing field pushes it over -- trailing
  // is dropped first, name is kept.
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    return {"shortname.txt", "this-trailing-is-also-way-too-long", false, ""};
  });
  CHECK(v.modeLineText() == "shortname.txt 1/3 TOP");

  // Both fields too long even without each other -- falls back to bare position text.
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    return {"this-name-is-way-too-long-to-fit.txt", "this-trailing-is-also-way-too-long-to-fit", false, ""};
  });
  CHECK(v.modeLineText() == "1/3 TOP");
}

TEST_CASE("renderModeLine: name/position/trailing are separate reverse-video runs with plain gaps between") {
  RecordingDisplay d(10, 40);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setModeLineFieldsProvider(
      [](int) -> ContentView::ModeLineFields { return {"/tmp/dir/", "(ls)", /*fillTrailingToEdge=*/false, ""}; });
  v.redraw();

  // "/tmp/dir/ 1/3 TOP (ls)" -- columns 0-8 name, 9 gap, 10-16 position, 17 gap, 18-21 trailing.
  std::string row = d.rowText(5);
  CHECK(row.substr(0, 22) == "/tmp/dir/ 1/3 TOP (ls)");
  CHECK(d.standoutAt(5, 0) == true);    // '/' of name
  CHECK(d.standoutAt(5, 8) == true);    // '/' at the end of name
  CHECK(d.standoutAt(5, 9) == false);   // plain gap
  CHECK(d.standoutAt(5, 10) == true);   // '1' of position
  CHECK(d.standoutAt(5, 16) == true);   // 'P' of "TOP"
  CHECK(d.standoutAt(5, 17) == false);  // plain gap
  CHECK(d.standoutAt(5, 18) == true);   // '(' of trailing
  CHECK(d.standoutAt(5, 21) == true);   // ')' of trailing
  CHECK(d.standoutAt(5, 22) == false);  // past trailing -- not padded to the edge
}

TEST_CASE("renderModeLine: fillTrailingToEdge pads the trailing field's own reverse-video run to the right edge") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setModeLineFieldsProvider(
      [](int) -> ContentView::ModeLineFields { return {"file.txt", "42 /tmp/", /*fillTrailingToEdge=*/true, ""}; });
  v.redraw();

  // "file.txt 1/3 TOP 42 /tmp/" is 25 columns; padded with spaces (still standout) to fill 30.
  std::string row = d.rowText(5);
  CHECK(row.substr(0, 25) == "file.txt 1/3 TOP 42 /tmp/");
  CHECK(d.standoutAt(5, 20) == true);  // inside the trailing field itself
  CHECK(d.standoutAt(5, 29) == true);  // padding, still standout -- reaches the last column
}

TEST_CASE("renderModeLine: rightWatermark is right-aligned, not reverse-video, and dropped if it doesn't fit") {
  RecordingDisplay d(10, 40);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    return {"/tmp/dir/", "(ls)", false, "usilic v1.5b"};
  });
  v.redraw();

  // "/tmp/dir/ 1/3 TOP (ls)" occupies columns 0-21 (see the plain-gaps
  // test above); " usilic v1.5b " (14 chars) is right-aligned at the
  // last 14 columns (26-39 of a 40-column display).
  std::string row = d.rowText(5);
  CHECK(row.substr(26, 14) == " usilic v1.5b ");
  CHECK(d.standoutAt(5, 27) == false);  // bold, not reverse-video, unlike name/position/trailing
  CHECK(row.substr(22, 4) == "    ");   // the gap between trailing and the watermark stays plain/blank

  // Too narrow to fit both the drawn fields AND the watermark -- dropped entirely, not truncated.
  RecordingDisplay narrow(10, 25);
  ContentView v2(narrow);
  v2.setViewport(0, 5);
  v2.setLines(makeLines(3));
  v2.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    return {"/tmp/dir/", "(ls)", false, "usilic v1.5b"};
  });
  v2.redraw();
  CHECK(narrow.rowText(5).find("usilic") == std::string::npos);
}

TEST_CASE("lineIndexAtRow: -1 outside the viewport (above, below, or past content)") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(2, 5);
  v.setLines(makeLines(3));  // fewer lines than the viewport height
  CHECK(v.lineIndexAtRow(1) == -1);   // above the viewport
  CHECK(v.lineIndexAtRow(7) == -1);   // below the viewport (viewport is rows 2..6)
  CHECK(v.lineIndexAtRow(5) == -1);   // within the viewport, but past the last real line
}

TEST_CASE("lineIndexAtRow: accounts for scroll (topIndex_) after moving past the first page") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(10));
  for (int i = 0; i < 5; i++) v.moveDown();  // cursor at index 5, viewport scrolled so topIndex_ == 3
  CHECK(v.lineIndexAtRow(0) == 3);
  CHECK(v.lineIndexAtRow(2) == 5);
}

TEST_CASE("setTopIndex/topIndex: scrolls the viewport without touching cursorIndex_") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(10));
  CHECK(v.topIndex() == 0);

  v.setTopIndex(4);
  CHECK(v.topIndex() == 4);
  CHECK(v.cursorIndex() == 0);  // unlike moveDown(), the cursor doesn't follow
}

TEST_CASE("setTopIndex: clamps to a valid range") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(10));  // max top is 10 - 3 = 7

  v.setTopIndex(-5);
  CHECK(v.topIndex() == 0);
  v.setTopIndex(999);
  CHECK(v.topIndex() == 7);
}

TEST_CASE("stepTopIndex: unlike setTopIndex(), pins cursorIndex_ to the newly-revealed edge") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(10));  // max top is 10 - 3 = 7

  CHECK(v.stepTopIndex(1) == true);  // reveal LATER content, like coasting down
  CHECK(v.topIndex() == 1);
  CHECK(v.cursorIndex() == 3);  // bottom edge of the now 1..3 viewport

  CHECK(v.stepTopIndex(1) == true);
  CHECK(v.topIndex() == 2);
  CHECK(v.cursorIndex() == 4);

  CHECK(v.stepTopIndex(-1) == true);  // reveal EARLIER content, like coasting up
  CHECK(v.topIndex() == 1);
  CHECK(v.cursorIndex() == 1);  // top edge, not wherever it was before
}

TEST_CASE("stepTopIndex: false (a no-op) at either boundary") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(5));  // max top is 5 - 3 = 2

  CHECK(v.stepTopIndex(-1) == false);  // already at the top
  CHECK(v.topIndex() == 0);

  v.setTopIndex(2);
  CHECK(v.stepTopIndex(1) == false);  // already at the bottom
  CHECK(v.topIndex() == 2);
}

TEST_CASE("setHoverIndex: highlights the hover row instead of cursorIndex_") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  CHECK(v.hoverIndex() == std::nullopt);

  v.setHoverIndex(2);
  v.redraw();
  CHECK(v.hoverIndex() == 2);
  CHECK(d.standoutAt(0, 0) == false);  // cursor is on line 0, but hover takes over
  CHECK(d.standoutAt(2, 0) == true);

  v.setHoverIndex(std::nullopt);
  v.redraw();
  CHECK(d.standoutAt(0, 0) == true);  // back to highlighting cursorIndex_
  CHECK(d.standoutAt(2, 0) == false);
}

TEST_CASE("moveDown: cursor advances and marking follows it") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.redraw();
  v.moveDown();
  CHECK(v.cursorIndex() == 1);
  CHECK(d.standoutAt(0, 0) == false);
  CHECK(d.standoutAt(1, 0) == true);
}

TEST_CASE("moveUp at the first line beeps instead of underflowing") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  int before = d.cursorRow();  // moveUp() shouldn't touch the screen at all
  v.moveUp();
  CHECK(v.cursorIndex() == 0);
  CHECK(d.cursorRow() == before);
}

TEST_CASE("moveDown at the last line beeps instead of overflowing") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setCursorIndex(2);
  v.moveDown();
  CHECK(v.cursorIndex() == 2);
}

TEST_CASE("moveDown past the viewport scrolls the window (full redraw when smooth scroll is off)") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);  // only 3 rows visible
  v.setLines(makeLines(5));
  v.redraw();
  v.moveDown();
  v.moveDown();
  v.moveDown();  // cursor now at index 3, past the initial [0,1,2] window
  CHECK(v.cursorIndex() == 3);
  // window should have scrolled so the cursor is visible on some row
  CHECK(d.rowText(0).substr(0, 5) != "line0");  // line0 scrolled out of view
  bool cursorLineVisible = false;
  for (int row = 0; row < 3; row++)
    if (d.rowText(row).substr(0, 5) == "line3") cursorLineVisible = true;
  CHECK(cursorLineVisible);
}

TEST_CASE("moveUp/moveDown use hardware scroll when smoothScrollEnabled") {
  RecordingDisplay d(10, 20);
  d.setSmoothScrollEnabled(true);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(5));
  v.redraw();
  CHECK(d.scrollForwardCount() == 0);
  v.moveDown();
  v.moveDown();
  v.moveDown();  // cursor 0->3, crosses the viewport boundary -> scrolls forward
  CHECK(d.scrollForwardCount() >= 1);
  CHECK(v.cursorIndex() == 3);

  // Window is now [1,2,3] (topIndex_ == 1). Moving up twice more (3->2,
  // both still inside the window) shouldn't scroll; the third crosses
  // back below topIndex_ and should scroll reverse.
  v.moveUp();
  v.moveUp();
  CHECK(d.scrollReverseCount() == 0);
  v.moveUp();
  CHECK(v.cursorIndex() == 0);
  CHECK(d.scrollReverseCount() >= 1);
}

TEST_CASE("scrollLineDown/scrollLineUp: 'j'/'k' (usilicrc K-8/K-9) always scroll, unlike moveUp()/moveDown()") {
  // "Move window one line down/up", NOT "move cursor one line" -- see
  // ContentView::scrollLineDown()'s own comment for the usilicrc
  // reference. Every call moves topIndex_ by exactly one, and lands
  // cursorIndex_ on the newly-revealed edge line, regardless of where
  // the cursor started (unlike moveDown(), which only scrolls once
  // the cursor would otherwise leave the viewport).
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(10));
  CHECK(v.topIndex() == 0);
  CHECK(v.cursorIndex() == 0);

  v.scrollLineDown();
  CHECK(v.topIndex() == 1);
  CHECK(v.cursorIndex() == 3);  // new bottom row: topIndex_(1) + height_(3) - 1
  v.scrollLineDown();
  CHECK(v.topIndex() == 2);
  CHECK(v.cursorIndex() == 4);

  v.scrollLineUp();
  CHECK(v.topIndex() == 1);
  CHECK(v.cursorIndex() == 1);  // new top row: topIndex_ itself
}

TEST_CASE("scrollLineDown/scrollLineUp: beep instead of scrolling past either end") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(5));  // maxTop = 5 - 3 = 2

  v.scrollLineUp();  // already at topIndex_ 0
  CHECK(v.topIndex() == 0);

  v.scrollLineDown();
  v.scrollLineDown();
  CHECK(v.topIndex() == 2);
  v.scrollLineDown();  // already at maxTop
  CHECK(v.topIndex() == 2);
  CHECK(v.cursorIndex() == 4);
}

TEST_CASE("scrollLineDown/scrollLineUp use hardware scroll when smoothScrollEnabled") {
  RecordingDisplay d(10, 20);
  d.setSmoothScrollEnabled(true);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(5));
  CHECK(d.scrollForwardCount() == 0);
  v.scrollLineDown();
  CHECK(d.scrollForwardCount() == 1);
  CHECK(d.scrollReverseCount() == 0);
  v.scrollLineUp();
  CHECK(d.scrollReverseCount() == 1);
}

TEST_CASE("pageDown/pageUp move a full page and clamp at the ends") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(10));
  v.pageDown();
  CHECK(v.cursorIndex() == 3);
  v.pageDown();
  v.pageDown();
  v.pageDown();  // should clamp at the last line, not run off the end
  CHECK(v.cursorIndex() == 9);
  v.pageUp();
  CHECK(v.cursorIndex() == 6);
}

TEST_CASE("goToTop / goToBottom jump to the first/last line") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(10));
  v.goToBottom();
  CHECK(v.cursorIndex() == 9);
  v.goToTop();
  CHECK(v.cursorIndex() == 0);
}

TEST_CASE("control bytes render reverse-video, consistent with LineEditor") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  std::vector<ContentView::Line> lines;
  lines.push_back({std::string(1, static_cast<char>(1)) + "x", std::nullopt, std::nullopt});  // Ctrl-A + 'x'
  v.setLines(lines);
  v.redraw();
  CHECK(d.rowText(0)[0] == static_cast<char>(1 + '@'));  // 'A'
  CHECK(d.rowText(0)[1] == 'x');
}

TEST_CASE("a literal tab byte renders as a plain space, not a reverse-video control glyph") {
  // Defensive fallback for callers that embed a raw '\t' directly into
  // a Line (e.g. DuMode::formatEntryLine()'s "size\tpath"), for any
  // caller other than PipeBuffer -- PipeBuffer's own wrapLine() now
  // expands tabs to spaces before they ever reach a Line. Found via a
  // real user report (2026-08-02, screenshot): tab bytes used to hit
  // the same reverse-video path as other control bytes (< 0x20),
  // showing as visible boxes between `ls` filenames in SHELL scrollback.
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setCursorHighlightEnabled(false);  // isolate the tab's own standout, not the cursor row's
  std::vector<ContentView::Line> lines;
  lines.push_back({std::string("a\tb"), std::nullopt, std::nullopt});
  v.setLines(lines);
  v.redraw();
  CHECK(d.rowText(0)[0] == 'a');
  CHECK(d.rowText(0)[1] == ' ');
  CHECK(d.standoutAt(0, 1) == false);
  CHECK(d.rowText(0)[2] == 'b');
}

TEST_CASE("setLines clamps a stale cursor index to the new list's bounds") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(10));
  v.setCursorIndex(9);
  v.setLines(makeLines(3));  // shorter list
  CHECK(v.cursorIndex() == 2);
}

TEST_CASE("redraw draws modeLineText() into the row right after the viewport, in standout") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);  // rows 0-2 are content; row 3 is the mode-line row
  v.setLines(makeLines(10));
  v.setCursorIndex(1);
  v.redraw();
  CHECK(d.rowText(3).substr(0, v.modeLineText().size()) == v.modeLineText());
  CHECK(d.standoutAt(3, 0));
}

TEST_CASE("setModeLineRow: overrides the mode-line's row away from the default row0+height") {
  // Added for Shell's own scrollback_ view (2026-08-03): SHELL needs a
  // row permanently reserved below the scrollable content for its live
  // prompt overlay, decoupled from where the mode-line itself draws --
  // see Shell::commandLineRow()'s own comment for the real bug (prompt
  // vs. last-content-line collision) this unblocks fixing.
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);  // default mode-line row would be 3
  v.setLines(makeLines(2));
  v.setModeLineRow(7);
  v.redraw();
  CHECK(d.rowText(3)[0] == ' ');  // the default row is untouched
  CHECK(d.rowText(7).substr(0, v.modeLineText().size()) == v.modeLineText());
  CHECK(d.standoutAt(7, 0));

  v.setModeLineRow(std::nullopt);  // back to the default
  v.redraw();
  CHECK(d.rowText(3).substr(0, v.modeLineText().size()) == v.modeLineText());
}

TEST_CASE("modeLineText: tracks cursorIndex_, matching legacy print_line()'s row") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(10));
  v.setCursorIndex(4);
  CHECK(v.modeLineText().substr(0, 2) == "05");  // 1-indexed
}

TEST_CASE("scrollModeLineText: uses topIndex_ (the viewport's top line), not cursorIndex_") {
  // A real report: during a mouse-drag/Shift-arrow coast (which
  // deliberately never moves cursorIndex_, see setTopIndex()'s own
  // comment), modeLineText()'s number sat frozen even though the view
  // was visibly scrolling. scrollModeLineText() is what
  // GraphicalDisplay::renderScrolled() calls instead while actively
  // scrolling, so the mode-line moves in step with what's on screen.
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(10));
  v.setCursorIndex(0);  // stays here throughout, like a real coast
  v.setTopIndex(5);     // scrolled well past the cursor's own line
  CHECK(v.modeLineText() == "01/10 TOP");   // unaffected -- still cursorIndex_-based
  CHECK(v.scrollModeLineText() == "06/10 60%");  // topIndex_ + 1 = 6
}

TEST_CASE("scrollModeLineText: reaching the bottom boundary shows END, not a percentage") {
  // topIndex_ itself caps out at total-height_ (7 here), which never
  // equals totalLines the way cursorIndex_ does in modeLineText() --
  // a real report found coasting all the way down stayed stuck on a
  // percentage instead of reaching END like plain 'j'/'s' navigation.
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(10));
  v.setCursorIndex(0);
  v.setTopIndex(7);  // maxTop = 10 - 3 = 7 -- fully scrolled to the end
  CHECK(v.scrollModeLineText() == "10/10 END");
}
