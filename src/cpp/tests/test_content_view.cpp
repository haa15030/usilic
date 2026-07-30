#include "content_view.hpp"
#include "doctest.h"
#include "recording_display.hpp"

using fm::ContentView;
using fm::test::RecordingDisplay;

namespace {

std::vector<ContentView::Line> makeLines(int n, const std::string &prefix = "line") {
  std::vector<ContentView::Line> lines;
  for (int i = 0; i < n; i++) lines.push_back({prefix + std::to_string(i)});
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

TEST_CASE("redraw: rows past the end of the list are left blank") {
  RecordingDisplay d(10, 20);
  ContentView v(d);
  v.setViewport(0, 5);
  v.setLines(makeLines(2));
  v.redraw();
  CHECK(d.rowText(2)[0] == ' ');
  CHECK(d.rowText(4)[0] == ' ');
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
  lines.push_back({std::string(1, static_cast<char>(1)) + "x"});  // Ctrl-A + 'x'
  v.setLines(lines);
  v.redraw();
  CHECK(d.rowText(0)[0] == static_cast<char>(1 + '@'));  // 'A'
  CHECK(d.rowText(0)[1] == 'x');
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
