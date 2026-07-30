#include "content_view.hpp"

#include <algorithm>

#include "mode_line.hpp"

namespace fm {

ContentView::ContentView(Display &display) : display_(display) {}

void ContentView::setViewport(int row0, int height) {
  row0_ = row0;
  height_ = height;
  clampViewport();
}

void ContentView::setLines(std::vector<Line> lines) {
  lines_ = std::move(lines);
  if (cursorIndex_ >= static_cast<int>(lines_.size()))
    cursorIndex_ = std::max(0, static_cast<int>(lines_.size()) - 1);
  clampViewport();
}

const std::vector<ContentView::Line> &ContentView::lines() const { return lines_; }

int ContentView::cursorIndex() const { return cursorIndex_; }

int ContentView::lineIndexAtRow(int displayRow) const {
  int row = displayRow - row0_;
  if (row < 0 || row >= height_) return -1;
  int idx = topIndex_ + row;
  if (idx < 0 || idx >= static_cast<int>(lines_.size())) return -1;
  return idx;
}

int ContentView::topIndex() const { return topIndex_; }

void ContentView::setTopIndex(int index) {
  int maxTop = std::max(0, static_cast<int>(lines_.size()) - std::max(height_, 0));
  topIndex_ = std::clamp(index, 0, maxTop);
}

void ContentView::setHoverIndex(std::optional<int> index) { hoverIndex_ = index; }
std::optional<int> ContentView::hoverIndex() const { return hoverIndex_; }

void ContentView::setCursorIndex(int index) {
  if (lines_.empty()) {
    cursorIndex_ = 0;
    return;
  }
  cursorIndex_ = std::clamp(index, 0, static_cast<int>(lines_.size()) - 1);
  clampViewport();
}

void ContentView::clampViewport() {
  if (topIndex_ > cursorIndex_) topIndex_ = cursorIndex_;
  else if (height_ > 0 && cursorIndex_ >= topIndex_ + height_)
    topIndex_ = cursorIndex_ - height_ + 1;
  if (topIndex_ < 0) topIndex_ = 0;
}

void ContentView::moveUp() {
  if (cursorIndex_ <= 0) {
    display_.putChar(7);
    return;
  }
  cursorIndex_--;
  if (cursorIndex_ < topIndex_) {
    topIndex_ = cursorIndex_;
    if (display_.smoothScrollEnabled()) {
      // Ported from sr_row(): scroll the viewport down by one
      // terminal row (revealing a new line above) and draw just that
      // line, instead of redrawing the whole page.
      display_.moveCursor(row0_, 0);
      display_.scrollReverse();
      renderLine(lines_[topIndex_], topIndex_ == cursorIndex_);
      renderModeLine();
      display_.flush();
      return;
    }
  }
  redraw();
}

void ContentView::moveDown() {
  if (lines_.empty() || cursorIndex_ >= static_cast<int>(lines_.size()) - 1) {
    display_.putChar(7);
    return;
  }
  cursorIndex_++;
  if (cursorIndex_ >= topIndex_ + height_) {
    topIndex_++;
    if (display_.smoothScrollEnabled()) {
      // Ported from sf_row(): scroll the viewport up by one terminal
      // row (revealing a new line below) and draw just that line.
      int bottomRow = row0_ + height_ - 1;
      display_.moveCursor(bottomRow, 0);
      display_.scrollForward();
      display_.clearToEndOfLine();
      int bottomIndex = topIndex_ + height_ - 1;
      if (bottomIndex < static_cast<int>(lines_.size()))
        renderLine(lines_[bottomIndex], bottomIndex == cursorIndex_);
      renderModeLine();
      display_.flush();
      return;
    }
  }
  redraw();
}

void ContentView::pageUp() {
  if (height_ <= 0) return;
  topIndex_ = std::max(0, topIndex_ - height_);
  cursorIndex_ = std::max(0, cursorIndex_ - height_);
  redraw();
}

void ContentView::pageDown() {
  if (height_ <= 0 || lines_.empty()) return;
  int maxTop = std::max(0, static_cast<int>(lines_.size()) - height_);
  topIndex_ = std::min(maxTop, topIndex_ + height_);
  cursorIndex_ = std::min(static_cast<int>(lines_.size()) - 1, cursorIndex_ + height_);
  redraw();
}

void ContentView::goToTop() {
  cursorIndex_ = 0;
  topIndex_ = 0;
  redraw();
}

void ContentView::goToBottom() {
  if (lines_.empty()) {
    cursorIndex_ = topIndex_ = 0;
    redraw();
    return;
  }
  cursorIndex_ = static_cast<int>(lines_.size()) - 1;
  topIndex_ = std::max(0, cursorIndex_ - height_ + 1);
  redraw();
}

void ContentView::renderLine(const Line &line, bool marked) {
  bool standoutOn = false;
  auto setStandout = [&](bool on) {
    if (on == standoutOn) return;
    display_.setAttribute(on ? Display::Attribute::Standout : Display::Attribute::StandoutOff);
    standoutOn = on;
  };

  setStandout(marked);
  for (unsigned char c : line.text) {
    // K_dl (0x7f, DEL) and plain control bytes (< 0x20) are shown
    // reverse-video as letter+'@', the same convention LineEditor's
    // renderVisible() uses -- ported from put_str()'s control-
    // character handling (K_a0's hex-dump-mode variant is excluded,
    // see the class comment).
    bool isControl = c && (c < ' ' || c == 0x7f);
    if (isControl) {
      // Show the control-byte marker distinctly from whatever
      // background is currently active (plain text, or this line's
      // own whole-line marking), then restore it -- unlike legacy's
      // f_grep-conditional dance for the same idea (see the ported-
      // from comment in the header), this is derived structurally
      // (toggle away from the current state, then back) so it stays
      // correct regardless of whether the line is marked.
      setStandout(!marked);
      int shown = (c == 0x7f) ? c - 0x80 : c;
      display_.putChar(shown + '@');
      setStandout(marked);
    } else {
      display_.putChar(c);
    }
  }
  setStandout(false);
}

std::string ContentView::modeLineText() const {
  int total = static_cast<int>(lines_.size());
  int current = total > 0 ? cursorIndex_ + 1 : 0;
  int bottomVisible = std::min(topIndex_ + std::max(height_, 0), total);
  return ModeLine::format(current, total, bottomVisible);
}

// Ported from print_line()'s current-line/total-line/percent display
// (see ModeLine's own class comment for why only this sliver of
// disp_window() moved into this class). Drawn in standout, matching
// legacy's own vt_stand() around this line. row0_+height_ is always
// the one screen row every mode's viewport leaves unused for exactly
// this purpose (see Application::resize()'s "contentHeight =
// rows()-1"), so no bounds check is needed here beyond what
// setViewport()'s caller already guarantees.
void ContentView::renderModeLine() {
  display_.moveCursor(row0_ + height_, 0);
  display_.clearToEndOfLine();
  display_.setAttribute(Display::Attribute::Standout);
  for (unsigned char c : modeLineText()) display_.putChar(c);
  display_.setAttribute(Display::Attribute::StandoutOff);
}

void ContentView::redraw() {
  clampViewport();
  int highlighted = hoverIndex_.has_value() ? *hoverIndex_ : cursorIndex_;
  for (int row = 0; row < height_; row++) {
    int idx = topIndex_ + row;
    display_.moveCursor(row0_ + row, 0);
    display_.clearToEndOfLine();
    if (idx < static_cast<int>(lines_.size())) renderLine(lines_[idx], idx == highlighted);
  }
  renderModeLine();
  display_.flush();
}

}  // namespace fm
