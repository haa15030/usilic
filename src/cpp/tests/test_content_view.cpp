#include "content_view.hpp"
#include "doctest.h"
#include "recording_display.hpp"

using fm::ContentView;
using fm::test::RecordingDisplay;

namespace {

std::vector<ContentView::Line> makeLines(int n, const std::string &prefix = "line") {
  std::vector<ContentView::Line> lines;
  for (int i = 0; i < n; i++) lines.push_back({prefix + std::to_string(i), std::nullopt, std::nullopt, {}});
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
  // left edge too, not hidden/left dangling -- see syncVisibleCursor()
  // (2026-08-28: left edge, not the raw byte-length right edge -- see
  // its own comment).
  CHECK_FALSE(d.visibleCursorHidden());
  CHECK(d.cursorRow() == 0);
  CHECK(d.cursorCol() == 0);  // highlightStartColumn.value_or(0) -- no highlighted sub-range set here
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

TEST_CASE("Line::attributes: Bold/Underline byte-ranges render as their own SGR runs, independent of "
          "the cursor-line's own reverse-video (2026-08-05, see the field's own comment) -- PipeBuffer's "
          "overstrike detection already produced this data, it was just discarded before reaching a Line") {
  RecordingDisplay d(10, 30);  // wide enough for the full 28-char line below (20 cols clipped "UNDER"/"done")
  ContentView v(d);
  v.setViewport(0, 3);
  ContentView::Line line;
  line.text = "plain BOLD plain UNDER done";
  //           0123456789...
  line.attributes = {
      {6, 10, fm::Display::Attribute::Bold},        // "BOLD"
      {17, 22, fm::Display::Attribute::Underline},  // "UNDER"
  };
  v.setLines({line});
  v.redraw();
  CHECK(d.boldAt(0, 5) == false);   // space before "BOLD"
  CHECK(d.boldAt(0, 6) == true);    // 'B'
  CHECK(d.boldAt(0, 9) == true);    // 'D'
  CHECK(d.boldAt(0, 10) == false);  // space after "BOLD"
  CHECK(d.underlineAt(0, 6) == false);  // "BOLD" itself isn't underlined
  CHECK(d.underlineAt(0, 17) == true);  // 'U' of "UNDER"
  CHECK(d.underlineAt(0, 21) == true);  // 'R' of "UNDER"
  CHECK(d.underlineAt(0, 22) == false);
  // The whole line is still the reverse-video cursor line underneath --
  // Bold/Underline combine with Standout, they don't replace it.
  CHECK(d.standoutAt(0, 6) == true);
  CHECK(d.standoutAt(0, 17) == true);
}

TEST_CASE("Line::attributes: a Standout-typed AttributeRun reverse-videos just its own byte range, "
          "on a line that is NOT the cursor/marked row (2026-08-29, CopyMode's own selection highlight "
          "-- see copy_mode.hpp) -- distinct from the whole-row reverse-video markedNow/highlightStartColumn "
          "already drives") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 3);
  ContentView::Line line;
  line.text = "plain SELECT plain";
  //           0123456789...
  line.attributes = {{6, 12, fm::Display::Attribute::Standout}};  // "SELECT"
  v.setLines({line, ContentView::Line{"other", std::nullopt, std::nullopt, {}}});
  v.setCursorIndex(1);  // move the cursor OFF line 0, so line 0 is not markedNow at all
  v.redraw();
  CHECK(d.standoutAt(0, 5) == false);   // space before "SELECT"
  CHECK(d.standoutAt(0, 6) == true);    // 'S'
  CHECK(d.standoutAt(0, 11) == true);   // 'T'
  CHECK(d.standoutAt(0, 12) == false);  // space after "SELECT"
}

TEST_CASE("Line::attributes: a Standout-typed AttributeRun on the cursor/marked row ORs with the "
          "row's own whole-line reverse-video instead of turning it off outside the run's own range "
          "(2026-08-29)") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 3);
  ContentView::Line line;
  line.text = "plain SELECT plain";
  line.attributes = {{6, 12, fm::Display::Attribute::Standout}};
  v.setLines({line});
  v.redraw();  // cursor defaults to line 0 -- this line IS markedNow
  CHECK(d.standoutAt(0, 0) == true);   // whole row already reverse-video via markedNow
  CHECK(d.standoutAt(0, 6) == true);   // still true inside the AttributeRun's own range
  CHECK(d.standoutAt(0, 12) == true);  // still true past it (markedNow alone covers this)
}

TEST_CASE("ContentView::setContentCursorColumn: overrides the real terminal caret's column while "
          "cursorHighlightEnabled() is false (2026-08-29, CopyMode's positional caret). redraw() itself "
          "never calls syncVisibleCursor() while highlighting is disabled (that's every existing caller's "
          "own job, e.g. LineEditor positions its own caret after its OWN redraw()) -- so, like every "
          "such existing caller, CopyMode must call this once per cursor move/redraw to actually place "
          "the terminal caret; this test exercises exactly that call, not an automatic effect of redraw().") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines({ContentView::Line{"abcdefgh", std::nullopt, std::nullopt, {}}});
  v.setCursorHighlightEnabled(false);
  v.redraw();
  v.setContentCursorColumn(4);
  CHECK(d.cursorCol() == 4);
  v.setContentCursorColumn(2);
  CHECK(d.cursorCol() == 2);  // updates again on a later call, not just the first
}

TEST_CASE("Line::attributes: a CJK-heavy attributed line is walked by DISPLAY WIDTH, not raw byte "
          "count -- 2026-08-21 regression for a real -g report (HELP chapter 4, screenshots =eb/=ea). "
          "renderLine()'s widthAware gate used to also require line.attributes.empty(), so any FileMode "
          "line with even one nroff-overstruck bold/underline word (HELP.jp's own section headings) fell "
          "back to the old byte-counted loop -- fine for ASCII, but for CJK content (3 bytes/2 columns "
          "per character) a maxCol budget sized in DISPLAY columns was being compared against a byte "
          "count, silently dropping trailing characters and, worse, stopping mid-character. On "
          "GraphicalDisplay specifically that left a dangling partial UTF-8 sequence in putByte()'s own "
          "pendingUtf8 buffer (moveCursor() never used to reset it -- also fixed, see its own comment), "
          "which spliced onto the NEXT row's first byte and committed one garbage glyph at column 0 -- "
          "exactly the small black-square/hatched-box marks =ea showed on wrapped continuation rows.") {
  // RecordingDisplay records one raw BYTE per putChar() call in its own
  // column-indexed grid (unlike GraphicalDisplay's pendingUtf8-buffered
  // commitGlyph(), which only advances by one cell per whole character)
  // -- so the grid itself is constructed a full 12 columns wide (this
  // line's total byte length) to safely record every byte regardless of
  // outcome, then narrowed via setSize() (which -- see its own comment
  // in recording_display.hpp -- only ever updates columns()'s return
  // value, never the already-allocated grid vectors) so ContentView's
  // own maxCol sees 10: strictly between this line's true display width
  // (8) and its byte length (12), the exact byte-vs-column mismatch
  // window the old code got wrong.
  RecordingDisplay d(4, 12);  // +1 row beyond the 3-row viewport for redraw()'s own mode-line row
  ContentView v(d);
  v.setViewport(0, 3);
  v.setCursorHighlightEnabled(false);  // marked stays false for every row -- maxCol == columns(), not columns()-1
  d.setSize(4, 10);
  ContentView::Line line;
  const std::string kA = "\xe3\x81\x82";  // U+3042 "あ", 3 bytes, display width 2
  line.text = kA + kA + kA + kA;          // 4 chars: 12 bytes, 8 display columns -- fits within maxCol==10 columns
  line.attributes = {{9, 12, fm::Display::Attribute::Bold}};  // bold the 4th (last) character only
  v.setLines({line});
  v.redraw();
  // All 4 characters made it out whole -- the old byte-counted loop
  // would have stopped once its byte count reached maxCol (10),
  // writing only the 4th character's lead byte (a torn, incomplete
  // sequence) and dropping its other two bytes entirely.
  CHECK(d.rowText(0).substr(0, 12) == line.text);
  // The 4th character's bold attribute (byte offset 9-12) still applies
  // correctly -- proof the fix's attribute matching stayed keyed on the
  // BYTE offset, not the display-column count, even now that both loops
  // share the same widthAware walk.
  CHECK(d.boldAt(0, 8) == false);  // 3rd character (bytes 6-8) is not bold
  CHECK(d.boldAt(0, 9) == true);   // 4th character (bytes 9-11) is bold
  CHECK(d.boldAt(0, 11) == true);
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

TEST_CASE("syncVisibleCursor: moves the display's visible cursor to the highlighted row's own left edge "
          "when highlighting is enabled (the default) -- 2026-08-05 fix, see its own comment: "
          "TerminalDisplay::hideVisibleCursor() is a no-op, so the OLD behavior here (hiding) left the "
          "real terminal cursor wherever the mode-line was last drawn instead. 2026-08-28 fix, a real "
          "report: the right edge used the line's own raw BYTE length, landing far past a multi-byte "
          "(CJK) name's own real visual end -- now the row's own highlightStartColumn (0 here, no "
          "highlighted sub-range set), sidestepping the whole byte-vs-column problem entirely") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setCursorIndex(1);
  v.syncVisibleCursor();
  CHECK_FALSE(d.visibleCursorHidden());
  CHECK(d.cursorRow() == 1);          // row0(0) + cursorIndex(1)
  CHECK(d.cursorCol() == 0);          // highlightStartColumn.value_or(0) -- the row's own left edge
}

// 2026-08-28 addition, same report: confirms the cursor follows a REAL
// highlightStartColumn (not just the default-0 case above), e.g.
// DirectoryMode's own nameStartColumn convention.
TEST_CASE("syncVisibleCursor: follows a real highlightStartColumn (not just the default 0) to the row's "
          "own selectable text, not its raw byte length") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  std::vector<ContentView::Line> lines = makeLines(3);
  lines[1].highlightStartColumn = 3;
  lines[1].text = "abc\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";  // "abc" + multi-byte UTF-8 (far more bytes than columns)
  v.setLines(std::move(lines));
  v.setCursorIndex(1);
  v.syncVisibleCursor();
  CHECK_FALSE(d.visibleCursorHidden());
  CHECK(d.cursorCol() == 3);  // the highlighted text's own start, NOT text.size() (11 bytes)
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
  lines.push_back({"drwxr-xr-x  staff  plain.txt", std::nullopt, 20, {}});  // highlight starts at column 20
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
  lines.push_back({"short.txt", std::nullopt, std::nullopt, {}});  // no fixed margin -- highlight starts at 0
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

TEST_CASE("scrollHighlightForLine: the cursor row gets a Standout run spanning "
          "[highlightStartColumn, text.size()), matching what redraw()/renderLine() would show "
          "(2026-09-02, GUI drag/coast highlight parity -- a direct user request)") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  std::vector<ContentView::Line> lines;
  lines.push_back({"drwxr-xr-x  staff  aaa.txt", std::nullopt, 20, {}});
  lines.push_back({"drwxr-xr-x  staff  bbb.txt", std::nullopt, 20, {}});
  v.setLines(lines);
  v.setCursorIndex(1);

  auto none = v.scrollHighlightForLine(0);
  CHECK_FALSE(none.has_value());
  auto some = v.scrollHighlightForLine(1);
  REQUIRE(some.has_value());
  CHECK(some->attribute == fm::Display::Attribute::Standout);
  CHECK(some->start == 20);
  CHECK(some->end == static_cast<int>(std::string("drwxr-xr-x  staff  bbb.txt").size()));
}

TEST_CASE("scrollHighlightForLine: hoverIndex_ takes priority over cursorIndex_, matching redraw()'s "
          "own `hoverIndex_.has_value() ? *hoverIndex_ : cursorIndex_`") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setLines(makeLines(3));
  v.setCursorIndex(0);
  v.setHoverIndex(2);

  CHECK_FALSE(v.scrollHighlightForLine(0).has_value());
  CHECK(v.scrollHighlightForLine(2).has_value());
}

TEST_CASE("scrollHighlightForLine: nullopt when cursorHighlightEnabled_ is false, or the line is "
          "neverHighlighted, or the index is out of range") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  std::vector<ContentView::Line> lines = makeLines(2);
  v.setLines(lines);
  v.setCursorIndex(0);

  v.setCursorHighlightEnabled(false);
  CHECK_FALSE(v.scrollHighlightForLine(0).has_value());
  v.setCursorHighlightEnabled(true);
  CHECK(v.scrollHighlightForLine(0).has_value());

  CHECK_FALSE(v.scrollHighlightForLine(-1).has_value());
  CHECK_FALSE(v.scrollHighlightForLine(2).has_value());
}

TEST_CASE("scrollHighlightForLine: with setMarkedRowUsesUnderline(true), a marked row gets Underline "
          "(not Standout), and a row that is BOTH the cursor row AND marked gets only Underline -- "
          "matching redraw()'s own `cursorRow && !markedRow` precedence") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setLines(makeLines(3));
  v.setCursorIndex(0);
  v.setMarkedRowUsesUnderline(true);
  v.setMarkedPredicate([](int idx) { return idx == 1; });

  auto cursorOnly = v.scrollHighlightForLine(0);
  REQUIRE(cursorOnly.has_value());
  CHECK(cursorOnly->attribute == fm::Display::Attribute::Standout);

  auto markedOnly = v.scrollHighlightForLine(1);
  REQUIRE(markedOnly.has_value());
  CHECK(markedOnly->attribute == fm::Display::Attribute::Underline);

  CHECK_FALSE(v.scrollHighlightForLine(2).has_value());

  // Now make row 0 both the cursor row AND marked -- marked wins, no Standout.
  v.setMarkedPredicate([](int idx) { return idx == 0; });
  auto both = v.scrollHighlightForLine(0);
  REQUIRE(both.has_value());
  CHECK(both->attribute == fm::Display::Attribute::Underline);
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
    return {"file" + std::to_string(cursorIndex) + ".txt", "(ls)", false, "", ""};
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
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields { return {"", "", false, "", ""}; });
  CHECK(v.modeLineText() == "1/3 TOP");

  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields { return {"name.txt", "", false, "", ""}; });
  CHECK(v.modeLineText() == "name.txt 1/3 TOP");

  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields { return {"", "(ls)", false, "", ""}; });
  CHECK(v.modeLineText() == "1/3 TOP (ls)");
}

TEST_CASE("setModeLineFieldsProvider: no provider set leaves modeLineText unchanged") {
  RecordingDisplay d(10, 40);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  CHECK(v.modeLineText() == "1/3 TOP");
}

TEST_CASE("setModeLineFieldsProvider: an overflowing trailing is dropped (not truncated); "
          "an overflowing name is truncated (not dropped)") {
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));

  // "shortname.txt 1/3 TOP" (21 cols) fits within 30 columns on its
  // own, but adding the long trailing field pushes it over -- trailing
  // is dropped first, name is kept.
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    return {"shortname.txt", "this-trailing-is-also-way-too-long", false, "", ""};
  });
  CHECK(v.modeLineText() == "shortname.txt 1/3 TOP");

  // 2026-08-15 update: name itself no longer falls back to being
  // dropped entirely when nothing fits -- see withTruncatedTrailing2()'s
  // own comment (a real report + screenshot found an over-long
  // DirectoryMode::currentDirectory() corrupting the whole mode-line
  // row instead of just failing to fit). name is now ALWAYS truncated
  // to fit alongside at least the position text, so this case (both
  // fields too long) now shows a middle-ellipsis-truncated name
  // instead of falling back to bare position text.
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    return {"this-name-is-way-too-long-to-fit.txt", "this-trailing-is-also-way-too-long-to-fit", false, "", ""};
  });
  std::string text = v.modeLineText();
  CHECK(text.find("1/3 TOP") != std::string::npos);
  CHECK(text.find("...") != std::string::npos);  // the name was truncated, not dropped
  CHECK(text.substr(0, 4) != "1/3 ");             // name still comes first -- it wasn't dropped either
}

// 2026-08-14 fix: a real report ("モードラインにバイト数「397723」
// および戻り値「LOCATE: ...s/Adobe/HKSCS.txt」が無い") -- an earlier
// fix (item 298's own fitModeLineFields() fallback tier) kept trailing
// (a short byte count) visible when trailing2 (a long LOCATE-path
// annotation) didn't fit, but DROPPED trailing2 entirely in that case.
// withTruncatedTrailing2() now truncates it instead. 2026-08-14 update:
// changed from tail-only ("..."+tail) to Finder-style MIDDLE-ellipsis
// (head...tail, matching middleEllipsisTruncate()'s own convention) --
// a direct user request: "戻り値も検索一覧同様「/Applications/Adobe
// Acrob...source/.../HKSCS.txt」形式で", i.e. the same head-and-tail
// convention LOCATE's own results-list rows already use.
TEST_CASE("setModeLineFieldsProvider: trailing2Kind=PathMiddle middle-ellipsis truncates "
          "(head...tail), not drops, when trailing still fits alongside it") {
  RecordingDisplay d(10, 40);  // 40 columns
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));

  // "HKSCS.txt 1/3 TOP 283" (22 cols) fits; adding the long path pushes
  // it well over 40. trailing ("283", short) stays; trailing2 gets
  // truncated to whatever's left. 2026-08-15 update: this convention
  // turned out to be LOCATE-specific (see the next test) -- opted
  // into here via trailing2Kind=PathMiddle, matching FileMode's
  // own provider when reached via LOCATE.
  //
  // 2026-08-16 update: middleEllipsisTruncate()'s own head/tail split
  // moved from 1/3 head to 1/2 head (a real report: "「...」の位置を
  // 行中央付近にしてほしい" -- the "..." sat too far left to read
  // comfortably), which shrinks the tail budget available here, so
  // this narrow 40-column fixture no longer fits the FULL 9-character
  // "HKSCS.txt" filename in the tail -- checking a shorter, still-
  // meaningful suffix instead of the exact original assertion.
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    return {"HKSCS.txt",
            "283",
            false,
            "",
            "/very/long/path/that/does/not/fit/at/all/HKSCS.txt",
            /*trailingPlain=*/false,
            /*trailing2RightJustified=*/false,
            /*trailing2Kind=*/ContentView::TrailingKind::PathMiddle};
  });
  std::string text = v.modeLineText();
  CHECK(text.find("283") != std::string::npos);          // the byte count survived
  std::size_t ellipsisPos = text.find("...");
  REQUIRE(ellipsisPos != std::string::npos);              // trailing2 was truncated, not dropped
  CHECK(text.find("/very") != std::string::npos);         // the HEAD is also kept visible, not just the tail
  CHECK(text.find("/very") < ellipsisPos);
  CHECK(text.substr(text.size() - 4) == ".txt");           // the tail (filename suffix) is kept visible too
}

// 2026-08-15 addition: the DEFAULT (trailing2Kind left unset, i.e.
// QueryEcho) is tail-only "..." + up to 16 characters -- a direct
// user policy statement: "なおLOCATE以外では、右下端の戻り値が長す
// ぎる場合「...16文字」でよろしく". Every provider OTHER than
// FileMode-reached-via-LOCATE relies on this default.
TEST_CASE("setModeLineFieldsProvider: trailing2Kind left at its default (QueryEcho) "
          "truncates tail-only, \"...\" + up to 16 characters") {
  RecordingDisplay d(10, 40);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));

  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    return {"DIR", "", false, "", "/very/long/path/that/does/not/fit/at/all/HKSCS.txt"};
  });
  std::string text = v.modeLineText();
  std::size_t ellipsisPos = text.find("...");
  REQUIRE(ellipsisPos != std::string::npos);
  CHECK(text.find("/very") == std::string::npos);          // the HEAD is NOT kept -- tail-only this time
  CHECK(text.substr(text.size() - 9) == "HKSCS.txt");       // the tail is still kept
  CHECK(text.size() - ellipsisPos - 3 <= 16);               // at most 16 tail characters
}

// The GREP/CUT candidate-list precedent (2026-08-11 fix) still holds:
// when trailing2 would fit FULLY if trailing were simply dropped, drop
// trailing rather than truncate trailing2 needlessly.
TEST_CASE("setModeLineFieldsProvider: trailing is dropped (not trailing2 truncated) when trailing2 "
          "would otherwise fit in full") {
  RecordingDisplay d(10, 40);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));

  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    return {"EGREP", "this-preview-is-long-enough-to-force-a-choice", false, "", "/tmp/short/"};
  });
  std::string text = v.modeLineText();
  CHECK(text.find("/tmp/short/") != std::string::npos);         // trailing2 shown in FULL
  CHECK(text.find("this-preview") == std::string::npos);        // trailing dropped, not truncated either
}

// 2026-08-15 fix: a real report + screenshot (=ba) -- a deeply-nested
// directory reached via Application::goToFileSelector() (K-18, ^J^K)
// left the mode line's name field ("モードラインが不調（←ディレクト
// リ名長過ぎ？」") corrupted instead of just truncated, since
// fitModeLineFields() never dropped/truncated name on its own (only
// its LAST fallback tier drops it entirely, and only when even
// name+position alone don't fit -- see the test just above this one).
TEST_CASE("setModeLineFieldsProvider: an overflowing name is middle-ellipsis truncated to fit "
          "alongside the position text") {
  RecordingDisplay d(10, 80);  // 80 columns, matching the real report's own terminal width
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));

  // Mirrors DirectoryMode's own REAL field shape exactly (trailing=byte
  // count, trailing2="(ls)", rightWatermark="usilic v1.5b" -- see its
  // own 2026-08-15 provider comment), not just an approximation: an
  // earlier version of this test used trailing="(ls)"/no trailing2,
  // which didn't actually exercise the interacting-fields regression
  // this name-truncation reservation logic needed two more fix rounds
  // for (missing the name-position gap, then missing trailing2 itself)
  // -- DirectoryMode's own unit test (a real, short temp-dir path)
  // never triggered name truncation at all, so it couldn't catch this
  // either; only a genuinely long name here does.
  std::string longPath =
      "/Applications/Adobe Acrobat DC/Adobe Acrobat.app/Contents/Resources/Resource/TypeSupport/Unicode/Mappings/Adobe/";
  v.setModeLineFieldsProvider([&](int) -> ContentView::ModeLineFields {
    return {longPath, "460", false, "usilic v1.5b", "(ls)"};
  });
  std::string text = v.modeLineText();
  CHECK(text.find("1/3 TOP") != std::string::npos);   // position still shows
  CHECK(text.find("...") != std::string::npos);       // name was truncated, not corrupted/dropped
  CHECK(text.find("/Applications") == 0);              // the head is kept visible
  CHECK(text.find("460") != std::string::npos);          // trailing (byte count) survives
  CHECK(text.find("(ls)") != std::string::npos);         // trailing2 survives too (see =bd/=be)

  // rightWatermark isn't part of modeLineText() (composeModeLineRow()
  // never includes it -- renderModeLine() draws it separately, right-
  // justified) -- check the actual rendered row instead.
  v.redraw();
  CHECK(d.rowText(5).find("usilic v1.5b") != std::string::npos);
}

// 2026-08-18 addition: a direct follow-up to DirectoryMode's "Invalid
// mode" padding fix (CLAUDE.md item 331) -- that fix pads a SHORT
// replacement message up to the ORIGINAL (longer) name's width, so
// fields after it don't shift. The user then asked about the opposite
// case: what if the original name is itself SHORTER than the
// replacement message (e.g. "DIR" or a short directory path shorter
// than "Invalid mode")? DirectoryMode's own guard
// (`if (message.size() < fields.name.size()) message.resize(...)`)
// deliberately does nothing in that direction -- no truncation, no
// special-casing -- and this test confirms that's already correct:
// the OVERFLOWING name just flows through the exact same sequential
// drawnLen-based layout (renderModeLine()) and fitModeLineFields()
// tier system the test above already exercises for a much longer
// overflow, which naturally pushes every field after it later by
// exactly the overflow amount (or middle-ellipsis truncates the name
// itself if even that doesn't fit -- see the test above) instead of
// leaving them at a now-wrong fixed offset. No production code change
// was needed for this direction; this test exists purely to pin the
// behavior down explicitly for the exact "short name, long message"
// shape DirectoryMode's own error path produces.
TEST_CASE("setModeLineFieldsProvider: a name LONGER than the field it replaces pushes every later "
          "field back by exactly the overflow, rather than leaving them at the old (now wrong) offset") {
  RecordingDisplay d(10, 80);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));

  std::string shortName = "DIR";                // 3 chars
  std::string longMessage = "Invalid mode";      // 12 chars -- 9 chars longer than shortName
  v.setModeLineFieldsProvider(
      [&](int) -> ContentView::ModeLineFields { return {shortName, "460", false, "usilic v1.5b", "(ls)"}; });
  v.redraw();
  std::string before = d.rowText(5);

  v.setModeLineFieldsProvider(
      [&](int) -> ContentView::ModeLineFields { return {longMessage, "460", false, "usilic v1.5b", "(ls)"}; });
  v.redraw();
  std::string after = d.rowText(5);

  size_t overflow = longMessage.size() - shortName.size();
  CHECK(after.substr(0, longMessage.size()) == longMessage);  // name itself: not truncated (room to spare)
  // The sequentially-positioned fields (position/trailing/trailing2,
  // each drawn right after the previous one via renderModeLine()'s own
  // running drawnLen) are neither corrupted nor left at their old
  // (now name-colliding) column -- each shifts right by exactly the
  // name's own overflow.
  CHECK(after.find("1/3 TOP") == before.find("1/3 TOP") + overflow);
  CHECK(after.find("460") == before.find("460") + overflow);
  CHECK(after.find("(ls)") == before.find("(ls)") + overflow);
  // rightWatermark ("usilic v1.5b") is the one exception: renderModeLine()
  // right-justifies it against the row's own edge rather than chaining it
  // after trailing2, so it does NOT shift -- it stays pinned to the same
  // column regardless of how long the name grows (the gap before it just
  // shrinks instead).
  CHECK(after.find("usilic v1.5b") == before.find("usilic v1.5b"));
}

TEST_CASE("renderModeLine: name/position/trailing are separate reverse-video runs with plain gaps between") {
  RecordingDisplay d(10, 40);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setModeLineFieldsProvider(
      [](int) -> ContentView::ModeLineFields { return {"/tmp/dir/", "(ls)", /*fillTrailingToEdge=*/false, "", ""}; });
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
      [](int) -> ContentView::ModeLineFields { return {"file.txt", "42 /tmp/", /*fillTrailingToEdge=*/true, "", ""}; });
  v.redraw();

  // "file.txt 1/3 TOP 42 /tmp/" is 25 columns; padded with spaces (still standout) to fill 30.
  std::string row = d.rowText(5);
  CHECK(row.substr(0, 25) == "file.txt 1/3 TOP 42 /tmp/");
  CHECK(d.standoutAt(5, 20) == true);  // inside the trailing field itself
  // Padding reaches column 28 (cols-2), NOT the true last column 29
  // (2026-08-18 auto-wrap fix): writing a Standout-marked character to
  // the terminal's true last column triggers auto-wrap on a real
  // terminal (same root cause renderLine()'s own 2026-08-13 fix
  // already worked around for content rows) -- confirmed via a real
  // report + a `script`-captured raw session showing byte-for-byte
  // correct-looking output that Terminal.app still didn't repaint
  // until an unrelated keypress nudged it.
  CHECK(d.standoutAt(5, 28) == true);   // padding, still standout
  CHECK(d.standoutAt(5, 29) == false);  // true last column: deliberately left unmarked
}

TEST_CASE("renderModeLine: rightWatermark is right-aligned, not reverse-video, and dropped if it doesn't fit") {
  RecordingDisplay d(10, 40);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    return {"/tmp/dir/", "(ls)", false, "usilic v1.5b", ""};
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
    return {"/tmp/dir/", "(ls)", false, "usilic v1.5b", ""};
  });
  v2.redraw();
  CHECK(narrow.rowText(5).find("usilic") == std::string::npos);
}

TEST_CASE("renderModeLine: trailing3 draws as its own separate standout segment with a plain gap after trailing2") {
  // See ModeLineFields::trailing3's own comment: added for ArchiveMode's
  // own listing mode line (report =dh: "「(tar)」と「/Users/.../
  // utf8.tar」の間に空白が無い"), a real gap between the badge and the
  // path that a single combined trailing2 string couldn't provide since
  // renderModeLine() draws one field as ONE uninterrupted Standout run.
  RecordingDisplay d(10, 60);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    ContentView::ModeLineFields fields{"ARC", "1765", /*fillTrailingToEdge=*/false, "", "(tar)"};
    fields.trailing3 = "/tmp/archive.tar";
    return fields;
  });
  v.redraw();

  // "ARC 1/3 TOP 1765 (tar) /tmp/archive.tar":
  // columns 0-2 name, 4-10 position, 12-15 trailing, 17-21 trailing2
  // ("(tar)"), 23-38 trailing3 -- each pair separated by one plain gap.
  std::string row = d.rowText(5);
  CHECK(row.substr(0, 39) == "ARC 1/3 TOP 1765 (tar) /tmp/archive.tar");
  CHECK(d.standoutAt(5, 21) == true);   // ')' of trailing2
  CHECK(d.standoutAt(5, 22) == false);  // plain gap between trailing2 and trailing3
  CHECK(d.standoutAt(5, 23) == true);   // '/' start of trailing3
  CHECK(d.standoutAt(5, 38) == true);   // final 'r' of trailing3
  CHECK(d.standoutAt(5, 39) == false);  // past trailing3 -- not padded to the edge
}

TEST_CASE("renderModeLine: trailing3 is dropped (not truncated) if it doesn't fit") {
  RecordingDisplay d(10, 25);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    ContentView::ModeLineFields fields{"ARC", "1765", /*fillTrailingToEdge=*/false, "", "(tar)"};
    fields.trailing3 = "/a/very/long/path/that/will/not/fit";
    return fields;
  });
  v.redraw();
  CHECK(d.rowText(5).find("/a/very") == std::string::npos);
  // Everything else still shows, unaffected.
  CHECK(d.rowText(5).find("(tar)") != std::string::npos);
}

TEST_CASE("renderModeLine: splitLayoutWhenCrowded leaves the normal layout untouched when everything fits") {
  // A real user proposal (report =dh follow-up: "モードラインの長過ぎ
  // モード名および戻り値対処としては...") added an alternate fallback
  // layout, but only for rows that actually need it -- opting into the
  // flag on a provider whose fields comfortably fit must change nothing.
  RecordingDisplay d(10, 40);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    ContentView::ModeLineFields fields{"/tmp/dir/", "(ls)", /*fillTrailingToEdge=*/false, "", ""};
    fields.splitLayoutWhenCrowded = true;
    return fields;
  });
  v.redraw();

  std::string row = d.rowText(5);
  CHECK(row.substr(0, 22) == "/tmp/dir/ 1/3 TOP (ls)");
  CHECK(d.standoutAt(5, 0) == true);
  CHECK(d.standoutAt(5, 9) == false);  // plain gap, same as the non-split test above
}

TEST_CASE("renderModeLine: splitLayoutWhenCrowded centers position+trailing and splits the remaining "
          "width between name and trailing2 once the normal layout doesn't fit") {
  // Direct user proposal, a real follow-up report (=dh): the archive
  // content view's own long CJK filename made name+trailing2 together
  // never fit alongside trailing (the byte count) -- fitModeLineFields()'s
  // usual tiers silently dropped trailing entirely. This layout keeps
  // ALL four pieces of information visible instead: "『「1/1 END」
  // 「111」』をまずは中央へ→残りのエリア半分ずつを「モード名」
  // 「戻り値」で分け合った上で...".
  RecordingDisplay d(10, 40);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  std::string longName = "this_is_a_very_long_mode_name_indeed_and_then_some.txt";
  std::string longTrailing2 = "this_is_a_very_long_trailing2_path_value_and_then_some";
  v.setModeLineFieldsProvider([&](int) -> ContentView::ModeLineFields {
    ContentView::ModeLineFields fields{longName, "1234", /*fillTrailingToEdge=*/false, "", longTrailing2};
    fields.splitLayoutWhenCrowded = true;
    return fields;
  });
  v.redraw();

  std::string row = d.rowText(5);
  // The byte count ("1234") is never dropped, unlike the old tiers.
  CHECK(row.find("1234") != std::string::npos);
  CHECK(row.find("1/3 TOP") != std::string::npos);
  // Position+trailing sit together, centered -- both standout, with a
  // plain gap between them (same convention as every other field pair).
  std::size_t posStart = row.find("1/3 TOP");
  CHECK(d.standoutAt(5, static_cast<int>(posStart)) == true);
  std::size_t trailingStart = row.find("1234");
  CHECK(d.standoutAt(5, static_cast<int>(trailingStart)) == true);
  CHECK(d.standoutAt(5, static_cast<int>(trailingStart) - 1) == false);  // the gap right before "1234"
  // name is drawn from column 0, truncated (Finder-style "...") rather
  // than dropped entirely -- some of both the head AND the byte-size
  // survive, unlike the old all-or-nothing tiers.
  CHECK(row.substr(0, 5) == longName.substr(0, 5));
  CHECK(row.substr(0, static_cast<std::size_t>(posStart)).find("...") != std::string::npos);
  CHECK(d.standoutAt(5, 0) == true);
  // trailing2 is right-aligned to the true last column, also truncated
  // rather than dropped.
  CHECK(row.substr(row.size() - 5) == longTrailing2.substr(longTrailing2.size() - 5));
  CHECK(d.standoutAt(5, 39) == true);
  CHECK(row.find("...") != row.rfind("..."));  // "..." appears on BOTH sides (name and trailing2), not just one
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

TEST_CASE("canStepTopIndex: a non-mutating preview of stepTopIndex()'s own boundary check -- added "
          "2026-08-10 after a real report that the FIRST version of its main.cpp caller (the inertial-"
          "coast boundary-flicker fix) passed the wrong sign and never actually triggered") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(5));  // max top is 5 - 3 = 2

  // At the top: can't step further up, CAN step down -- and neither
  // call mutates topIndex_, unlike stepTopIndex() itself.
  CHECK(v.canStepTopIndex(-1) == false);
  CHECK(v.canStepTopIndex(1) == true);
  CHECK(v.topIndex() == 0);

  v.setTopIndex(2);
  // At the bottom: the reverse.
  CHECK(v.canStepTopIndex(1) == false);
  CHECK(v.canStepTopIndex(-1) == true);
  CHECK(v.topIndex() == 2);

  // Mid-list: room in both directions.
  v.setTopIndex(1);
  CHECK(v.canStepTopIndex(-1) == true);
  CHECK(v.canStepTopIndex(1) == true);
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
  lines.push_back({std::string(1, static_cast<char>(1)) + "x", std::nullopt, std::nullopt, {}});  // Ctrl-A + 'x'
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
  lines.push_back({std::string("a\tb"), std::nullopt, std::nullopt, {}});
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

TEST_CASE("scrollModeLineSegments: same fields as scrollModeLineText, but as separate standout/plain-gap "
          "segments instead of one flat string") {
  // Added 2026-08-07 alongside Display::ModeLineSegment -- a real
  // screenshot comparison found GraphicalDisplay::renderScrolled()'s
  // mode-line drawn as one uninterrupted highlight bar (from the flat
  // scrollModeLineText() string), unlike ContentView::renderModeLine()'s
  // own per-field gaps. This is the segment-sequence source both
  // GraphicalDisplay's and TerminalDisplay's renderScrolled() now
  // draw from instead.
  RecordingDisplay d(10, 60);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(10));
  v.setModeLineFieldsProvider(
      [](int) -> ContentView::ModeLineFields { return {"prompt.md", "/Users/haa15030/git/usilic/", false, "", ""}; });
  v.setCursorIndex(0);
  v.setTopIndex(5);
  auto segments = v.scrollModeLineSegments();
  REQUIRE(segments.size() == 5);
  CHECK(segments[0].text == "prompt.md");
  CHECK(segments[0].standout);
  CHECK(segments[1].text == " ");
  CHECK_FALSE(segments[1].standout);  // the gap -- see this test's own comment
  CHECK(segments[2].text == "06/10 60%");
  CHECK(segments[2].standout);
  CHECK(segments[3].text == " ");
  CHECK_FALSE(segments[3].standout);
  CHECK(segments[4].text == "/Users/haa15030/git/usilic/");
  CHECK(segments[4].standout);
}

TEST_CASE("scrollModeLineSegments: trailing2 draws as its own separate standout segment with a plain gap "
          "before it, and takes the fillTrailingToEdge padding instead of trailing") {
  // Added 2026-08-10 for FileMode's own byte-size/directory split -- a
  // real report (screenshots, pixel-color-verified) caught trailing
  // and trailing2 (when both set) being rendered as ONE unbroken
  // highlight run before this, unlike every other mode-line field
  // pair (name/position, position/trailing) which already had a plain
  // gap between them.
  RecordingDisplay d(10, 30);
  ContentView v(d);
  v.setViewport(0, 3);
  v.setLines(makeLines(10));
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    return {"plain.txt", "12", /*fillTrailingToEdge=*/true, "", "/tmp/"};
  });
  v.setCursorIndex(0);
  v.setTopIndex(5);
  auto segments = v.scrollModeLineSegments();
  REQUIRE(segments.size() == 7);
  CHECK(segments[0].text == "plain.txt");
  CHECK(segments[0].standout);
  CHECK(segments[1].text == " ");
  CHECK_FALSE(segments[1].standout);
  CHECK(segments[2].text == "06/10 60%");
  CHECK(segments[2].standout);
  CHECK(segments[3].text == " ");
  CHECK_FALSE(segments[3].standout);
  CHECK(segments[4].text == "12");  // trailing, NOT edge-padded -- trailing2 gets that instead
  CHECK(segments[4].standout);
  CHECK(segments[5].text == " ");
  CHECK_FALSE(segments[5].standout);  // the plain gap this test exists to confirm
  CHECK(segments[6].standout);
  CHECK(segments[6].text.rfind("/tmp/", 0) == 0);
  CHECK(segments[6].text.size() > 5);  // padded with trailing spaces to reach the row's own width
}

TEST_CASE("scrollModeLineSegments: rightWatermark is emitted as its own plain-gap + bold (not standout) "
          "segment, matching renderModeLine()'s own look -- 2026-09-03, a direct user report that the "
          "bottom-right \"usilic v1.5b\" watermark stayed missing during GUI drag/coast scrolling") {
  RecordingDisplay d(10, 40);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    return {"/tmp/dir/", "(ls)", false, "usilic v1.5b", ""};
  });

  auto segments = v.scrollModeLineSegments();
  REQUIRE(segments.size() == 7);
  CHECK(segments[0].text == "/tmp/dir/");
  CHECK(segments[2].text == "1/3 TOP");
  CHECK(segments[4].text == "(ls)");
  // The 4-column plain gap (columns 22-25 of 40) between trailing and the watermark.
  CHECK(segments[5].text == "    ");
  CHECK_FALSE(segments[5].standout);
  CHECK_FALSE(segments[5].bold);
  // The watermark itself: bold, NOT standout -- unlike every other field segment above.
  CHECK(segments[6].text == " usilic v1.5b ");
  CHECK_FALSE(segments[6].standout);
  CHECK(segments[6].bold);
}

TEST_CASE("scrollModeLineSegments: rightWatermark is dropped entirely (not truncated) when it doesn't "
          "fit, matching renderModeLine()'s own drop-if-too-narrow behavior") {
  RecordingDisplay d(10, 25);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(3));
  v.setModeLineFieldsProvider([](int) -> ContentView::ModeLineFields {
    return {"/tmp/dir/", "(ls)", false, "usilic v1.5b", ""};
  });

  auto segments = v.scrollModeLineSegments();
  for (const auto &segment : segments) CHECK(segment.text.find("usilic") == std::string::npos);
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

// 2026-08-13 (LOCATE results, real reports/screenshots =am/=an): a
// MARKED (cursor-highlighted) line exactly as wide as the terminal
// used to leave the last column's own trailing setStandout(false)
// escape landing on the NEXT row instead (most real terminals'
// auto-wrap silently advances the cursor on the last column's write,
// which this class's own escape-sequence emission has no way to
// detect) -- corrupting that next row's own highlight state one row
// out of sync with cursorIndex_/the mode line's own position text.
// renderLine() now reserves the last column specifically for a
// MARKED line (see its own comment for why NOT for an unmarked one).
// RecordingDisplay itself doesn't simulate real terminal auto-wrap
// (see its own putAdvance()), so this can't reproduce the visual
// symptom directly -- it instead verifies the actual fix (the marked
// line's own last column is left untouched) is in place; the fix
// itself was confirmed against the real bug via a live PTY
// reproduction (both wide-content-present and wide-content-absent
// real `locate` results in the same session).
// 2026-08-20 update: renderLine()'s own middle-ellipsis truncation is
// now opt-in per-line (Line::ellipsisEligible, see its own comment --
// a real report found plain FileMode content wrongly getting the SAME
// "elide the middle" treatment meant only for LOCATE/DU/Archive-style
// results lists), so this plain, unflagged line goes through the
// original byte-based right-truncation instead (a simple 19-byte
// prefix of 'x's, no "..."). The core regression this test guards (the
// terminal's own LAST column is never written to, avoiding the auto-
// wrap bug) is unrelated to ellipsis eligibility either way.
TEST_CASE("redraw: a MARKED line exactly as wide as the terminal leaves its own last column untouched") {
  RecordingDisplay d(10, 20);  // 20 columns wide
  ContentView v(d);
  v.setViewport(0, 5);
  std::vector<ContentView::Line> lines;
  lines.push_back({std::string(20, 'x'), std::nullopt, std::nullopt, {}});  // exactly the terminal's own width
  v.setLines(lines);
  v.setCursorIndex(0);  // this is the marked/highlighted row
  v.redraw();
  CHECK(d.rowText(0)[19] == ' ');  // the last column deliberately left untouched
  CHECK(d.rowText(0).substr(0, 19) == std::string(19, 'x'));  // plain right-truncation, no ellipsis
}

// The unmarked counterpart: an UNMARKED full-width line (e.g. every
// row of a reflowed File-mode view, cursorHighlightEnabled_ disabled
// there -- see FileMode's own class comment) is NOT truncated, since
// it never calls setStandout(true) in the first place and so has
// nothing for auto-wrap to corrupt -- confirmed by
// handleResize()'s own "reflows File mode's content at the new width"
// test, which intentionally relies on the full width rendering.
TEST_CASE("redraw: an UNMARKED line exactly as wide as the terminal still renders its own last column") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setCursorHighlightEnabled(false);  // FileMode's own convention
  v.setViewport(0, 5);
  std::vector<ContentView::Line> lines;
  lines.push_back({std::string(20, 'x'), std::nullopt, std::nullopt, {}});
  v.setLines(lines);
  v.setCursorIndex(0);
  v.redraw();
  CHECK(d.rowText(0) == std::string(20, 'x'));  // full width, including the last column
}

// 2026-08-14 fix: a real report ("^N→１つ上の行が、ハイライト時は
// 「...bat Uninstaller.app/...」だったのが「...obat Uninstaller.app/
// ...」", confirmed to flip back on a mouse re-hover) -- the SAME
// row's own ellipsis truncation point used to shift by one character
// depending purely on whether it was currently the highlighted/marked
// row, since the ellipsis budget reused maxCol (marked ? columns()-1 :
// columns(), a distinction that exists for an unrelated auto-wrap
// reason -- see maxCol's own comment). Two otherwise-identical rows,
// one marked (cursor on it) and one not, must now render byte-for-byte
// the same truncated text in a view where marked genuinely varies
// (cursorHighlightEnabled_ true, LOCATE/DU's own convention).
TEST_CASE("redraw: ellipsis truncation is identical whether or not the row is currently marked") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  std::string longText = "/Applications/Adobe Acrobat DC/Adobe Acrobat.app/Contents/Resources/x.txt";
  std::vector<ContentView::Line> lines;
  // ellipsisEligible=true: this test is specifically about the
  // LOCATE-style ellipsis-truncation flicker fix (see Line::
  // ellipsisEligible's own comment for why that's now opt-in).
  lines.push_back({longText, std::nullopt, std::nullopt, {}, true});
  lines.push_back({longText, std::nullopt, std::nullopt, {}, true});
  v.setLines(lines);

  v.setCursorIndex(0);  // row 0 marked, row 1 not
  v.redraw();
  std::string markedRendering = d.rowText(0);
  std::string unmarkedRendering = d.rowText(1);

  v.setCursorIndex(1);  // row 1 marked, row 0 not -- the same two rows, roles swapped
  v.redraw();
  CHECK(d.rowText(1) == markedRendering);
  CHECK(d.rowText(0) == unmarkedRendering);
  CHECK(markedRendering == unmarkedRendering);  // the actual bug: these used to differ by one character
}

// 2026-08-20 fix, a direct user report ("=dz...7行目・10行目の■",
// comparing against a screenshot showing a lone dakuten mark left
// floating right after "..." with no base character before it):
// middleEllipsisTruncateByWidth()'s own boundary computation used to
// treat a 0-width combining mark as its own separate unit, letting the
// tail-side backwards walk include a trailing mark "for free" (0
// never blows the budget) while the base character it depends on got
// excluded for actually costing width -- landing the cut point INSIDE
// a base+mark pair. Constructed so the tail-side budget exactly
// exhausts on the trailing 'B's alone, leaving no room for the
// preceding "ト"+dakuten pair (width 2) -- before the fix this left a
// bare dakuten mark right after "...", now the pair is excluded (and
// elided) as one atomic unit instead of being split.
TEST_CASE("middleEllipsisTruncateByWidth (via a plain ellipsisEligible line): never splits a base kana "
          "character from its own trailing combining dakuten mark") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  // "ト" (U+30C8, e3 83 88) + combining dakuten (U+3099, e3 82 99),
  // still decomposed (not precomposed "ド") -- the exact shape macOS's
  // own NFD filenames use, matching this port's real repro (utf8.tar).
  std::string toDakuten = "\xe3\x83\x88\xe3\x82\x99";
  std::string text = std::string(20, 'C') + toDakuten + std::string(8, 'B');
  std::vector<ContentView::Line> lines;
  lines.push_back({text, std::nullopt, std::nullopt, {}, true});
  v.setLines(lines);
  v.setCursorIndex(0);
  v.redraw();
  std::string rendered = d.rowText(0);
  CHECK(rendered.find("...") != std::string::npos);  // truncation actually triggered
  // The pair is excluded as a whole (falls in the elided middle along
  // with part of the C's) -- neither half appears on its own.
  CHECK(rendered.find(toDakuten) == std::string::npos);
  CHECK(rendered.find("\xe3\x82\x99") == std::string::npos);  // no orphaned mark either, on its own
}
