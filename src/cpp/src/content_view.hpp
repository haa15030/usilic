#pragma once

#include <string>
#include <vector>

#include "display.hpp"

namespace fm {

// Scrollable list of text lines: renders a page at a time, tracks a
// cursor line, and scrolls (single-line or full-page) through the
// list, including Display's hardware-scroll-region path when
// available. Ported from disp.c's put_str()/disp_pp() and scroll.c's
// move_page()/move_row()/sr_row()/sf_row().
//
// Data model change from the legacy C: PP was a hand-rolled doubly-
// linked list (prev/next pointers), the only option available before
// std::vector existed. ContentView holds lines in a plain
// std::vector<Line> with index-based cursor/viewport tracking
// instead -- legacy's RV struct tracked three separate pointers (ps
// = viewport start, pe = viewport end, pp = cursor) because a linked
// list has no O(1) "how far is the end of my visible window", so it
// had to be walked and remembered explicitly; with a vector, the
// viewport end is just `topIndex_ + viewportHeight_`, a derived
// value, so there's nothing separate to keep in sync (and no
// PP::prev/next chain to keep correct as a side effect of scrolling).
//
// Deliberately excluded (belongs to future classes, not ContentView):
//   - disp_window()/print_line()/disp_prev()/disp_copy(): the bottom
//     mode-line/status-bar rendering (file-name truncation, line
//     counter with its own interactive line-jump input widget,
//     previous-mode breadcrumb, PROGRAM-name display). This is
//     status-bar rendering deeply specific to each mode's own
//     format, not part of showing a scrollable list of lines --
//     future StatusBar/Mode territory.
//   - get_col()/init_col(): per-mode column-offset calculation for
//     file selection (already fixed historically, see CLAUDE.md item
//     13) -- Mode's job, unrelated to scrolling/rendering.
//   - Per-character attribute runs (legacy PP::attr, used for e.g.
//     GREP-match highlighting and hex-dump-mode byte coloring) and
//     the K_a0 sentinel-byte handling in put_str() tied to that: both
//     depend on content preprocessing that doesn't exist yet (the
//     future Buffer/PipeBuffer classes' job to produce). What IS
//     ported here is put_str()'s other two rendering behaviors that
//     don't need that preprocessing: whole-line reverse-video for the
//     cursor line, and reverse-video for raw control bytes -- see
//     renderLine().
//   - Charset awareness: unlike LineEditor, ContentView never needs
//     to translate a byte offset into a screen column (it prints
//     whole lines and lets the terminal wrap/advance naturally), and
//     control-byte detection (< 0x20) is identical across every
//     encoding. There is nothing charset-dependent left to port here,
//     so this class takes no Charset::Encoding at all.
class ContentView {
 public:
  struct Line {
    std::string text;
  };

  explicit ContentView(Display &display);

  // Where on screen this view lives (row0..row0+height-1) and how
  // many lines tall it is.
  void setViewport(int row0, int height);

  void setLines(std::vector<Line> lines);
  const std::vector<Line> &lines() const;

  int cursorIndex() const;
  void setCursorIndex(int index);

  // Maps an absolute DISPLAY row (the same coordinate space
  // moveCursor()/setViewport() use, not viewport-relative) to the
  // line index currently rendered there, or -1 if displayRow is
  // outside this view's own viewport or past the end of lines_ (a
  // blank row within the viewport). Added for mouse click handling
  // (Application::handleMouseClick(), see IDEAS.md's GUI-mode click-
  // to-select/open request) -- the inverse of what redraw() already
  // computes internally (row0_+row <-> topIndex_+row) but not
  // previously exposed since nothing needed it before a pointing
  // device existed.
  int lineIndexAtRow(int displayRow) const;

  // The index of the topmost currently-visible line. Added alongside
  // setTopIndex() for the GUI drag-scroll prototype (see
  // GraphicalDisplay's own class comment) -- scrolling the viewport
  // WITHOUT moving cursorIndex_ (unlike moveUp()/moveDown(), which
  // always couple the two) matches how a real trackpad/mouse-wheel
  // scroll works in most applications: it repositions what's visible,
  // it doesn't change the selection.
  int topIndex() const;
  // Sets the topmost visible line directly (clamped to a valid
  // range), leaving cursorIndex_ untouched. Does not redraw --
  // callers driving a custom render loop (like the drag-scroll
  // prototype) own their own redraw timing.
  void setTopIndex(int index);

  // -- scrolling --
  // Single-line cursor movement (M_up/M_dn/M_kup/M_kdn): moves the
  // cursor one line, scrolling the viewport by one line (using
  // Display's hardware scroll when smoothScrollEnabled(), a full
  // redraw otherwise) only if the cursor would otherwise leave it.
  void moveUp();
  void moveDown();

  // Full-page movement (M_back/M_forw): always a full redraw, ported
  // from move_page(). The legacy smooth-scroll path also animated
  // page moves one line at a time when hardware scrolling was
  // available; simplified here to a single redraw regardless -- see
  // the class comment on scope.
  void pageUp();
  void pageDown();

  // Jump to the first/last line (M_head/M_tail).
  void goToTop();
  void goToBottom();

  // -- rendering --
  void redraw();

 private:
  void renderLine(const Line &line, bool marked);
  void clampViewport();

  Display &display_;

  std::vector<Line> lines_;
  int cursorIndex_ = 0;
  int topIndex_ = 0;

  int row0_ = 0, height_ = 0;
};

}  // namespace fm
