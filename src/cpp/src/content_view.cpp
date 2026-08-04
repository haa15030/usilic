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
  int rowOffset = bottomAnchorRowOffset();
  if (row < rowOffset) return -1;  // a blank padding row, bottom-anchored only
  int idx = topIndex_ + (row - rowOffset);
  if (idx < 0 || idx >= static_cast<int>(lines_.size())) return -1;
  return idx;
}

int ContentView::cursorDisplayRow() const {
  if (cursorIndex_ < topIndex_ || cursorIndex_ >= topIndex_ + height_) return -1;
  return row0_ + bottomAnchorRowOffset() + (cursorIndex_ - topIndex_);
}

void ContentView::syncVisibleCursor() {
  if (!cursorHighlightEnabled_) {
    int row = cursorDisplayRow();
    if (row >= 0) {
      display_.setVisibleCursor(row, 0);
      return;
    }
  }
  display_.hideVisibleCursor();
}

int ContentView::topIndex() const { return topIndex_; }

void ContentView::setTopIndex(int index) {
  int maxTop = std::max(0, static_cast<int>(lines_.size()) - std::max(height_, 0));
  topIndex_ = std::clamp(index, 0, maxTop);
}

bool ContentView::stepTopIndex(int direction) {
  if (height_ <= 0 || lines_.empty()) return false;
  int maxTop = std::max(0, static_cast<int>(lines_.size()) - height_);
  if (direction > 0) {
    if (topIndex_ >= maxTop) return false;
    topIndex_++;
    cursorIndex_ = std::min(topIndex_ + height_ - 1, static_cast<int>(lines_.size()) - 1);
  } else if (direction < 0) {
    if (topIndex_ <= 0) return false;
    topIndex_--;
    cursorIndex_ = topIndex_;
  }
  return true;
}

void ContentView::setHoverIndex(std::optional<int> index) { hoverIndex_ = index; }
std::optional<int> ContentView::hoverIndex() const { return hoverIndex_; }

void ContentView::setCursorHighlightEnabled(bool enabled) { cursorHighlightEnabled_ = enabled; }
bool ContentView::cursorHighlightEnabled() const { return cursorHighlightEnabled_; }

void ContentView::setBottomAnchored(bool enabled) { bottomAnchored_ = enabled; }

// Shared by redraw()/lineIndexAtRow() so both agree on where content
// starts when bottom-anchored -- see setBottomAnchored()'s own
// comment. 0 when top-anchored (the default) or when there's enough
// content to fill the viewport either way (nothing to pad).
int ContentView::bottomAnchorRowOffset() const {
  if (!bottomAnchored_) return 0;
  int visibleCount = std::min(static_cast<int>(lines_.size()) - topIndex_, height_);
  return std::max(0, height_ - visibleCount);
}

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

void ContentView::scrollLineDown() {
  if (height_ <= 0 || lines_.empty()) {
    display_.putChar(7);
    return;
  }
  int maxTop = std::max(0, static_cast<int>(lines_.size()) - height_);
  if (topIndex_ >= maxTop) {
    display_.putChar(7);
    return;
  }
  topIndex_++;
  cursorIndex_ = std::min(topIndex_ + height_ - 1, static_cast<int>(lines_.size()) - 1);
  if (display_.smoothScrollEnabled()) {
    // Same sf_row()-style single-row hardware scroll moveDown()'s own
    // fast path uses -- see its comment; the only difference here is
    // this always runs (no "only if the cursor would leave the
    // viewport" guard), matching scrollLineDown() always scrolling.
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
  redraw();
}

void ContentView::scrollLineUp() {
  if (height_ <= 0 || lines_.empty()) {
    display_.putChar(7);
    return;
  }
  if (topIndex_ <= 0) {
    display_.putChar(7);
    return;
  }
  topIndex_--;
  cursorIndex_ = topIndex_;
  if (display_.smoothScrollEnabled()) {
    // Same sr_row()-style single-row hardware scroll moveUp()'s own
    // fast path uses -- see its comment.
    display_.moveCursor(row0_, 0);
    display_.scrollReverse();
    renderLine(lines_[topIndex_], topIndex_ == cursorIndex_);
    renderModeLine();
    display_.flush();
    return;
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
  marked = marked && cursorHighlightEnabled_;
  int highlightStart = std::max(0, line.highlightStartColumn.value_or(0));
  bool standoutOn = false;
  auto setStandout = [&](bool on) {
    if (on == standoutOn) return;
    display_.setAttribute(on ? Display::Attribute::Standout : Display::Attribute::StandoutOff);
    standoutOn = on;
  };

  // `markedNow` starts true only if highlightStart is 0 (the common
  // case, and every caller but DirectoryMode's ls-listing) -- see
  // Line::highlightStartColumn's own comment. It flips on exactly once
  // the loop below reaches column highlightStart.
  bool markedNow = marked && highlightStart <= 0;
  setStandout(markedNow);
  int col = 0;
  for (unsigned char c : line.text) {
    if (col == highlightStart) {
      markedNow = marked;
      setStandout(markedNow);
    }
    // Tab specifically excluded from the control-byte check below --
    // ported from disp.c's own "convert '\t' to ' '" comment (its
    // count_h()/count_i() column-counting pass does the same
    // substitution before anything reaches the display layer). A raw
    // tab byte reaching THIS layer used to get the SAME reverse-video
    // glyph treatment as any other control byte -- found via a real
    // user report (2026-08-02, screenshot): plain `ls` output (which
    // real shells tab-align columns in, not space-align) showed
    // visible reverse-video boxes between filenames in SHELL's own
    // scrollback. PipeBuffer's own wrapLine() now expands tabs to the
    // right number of spaces BEFORE they ever reach a Line (see its
    // own comment) for content that passes through it, but this is a
    // second, simpler safety net here for any OTHER caller building a
    // Line directly with a literal '\t' in it (e.g. DuMode::
    // formatEntryLine()'s own size/path column separator) -- legacy's
    // own conversion is a bare 1-for-1 swap, not real tab-stop math,
    // so matching that exactly (a single space, not a computed
    // column-aligning run) is deliberate here, not a simplification.
    if (c == '\t') {
      display_.putChar(' ');
      col++;
      continue;
    }
    // K_dl (0x7f, DEL) and plain control bytes (< 0x20) are shown
    // reverse-video as letter+'@', the same convention LineEditor's
    // renderVisible() uses -- ported from put_str()'s control-
    // character handling (K_a0's hex-dump-mode variant is excluded,
    // see the class comment).
    bool isControl = c && (c < ' ' || c == 0x7f);
    if (isControl) {
      // Show the control-byte marker distinctly from whatever
      // background is currently active (plain text, or this line's
      // own current marking -- markedNow, not marked, now that a line
      // can be partially marked), then restore it -- unlike legacy's
      // f_grep-conditional dance for the same idea (see the ported-
      // from comment in the header), this is derived structurally
      // (toggle away from the current state, then back) so it stays
      // correct regardless of whether the line is marked.
      setStandout(!markedNow);
      int shown = (c == 0x7f) ? c - 0x80 : c;
      display_.putChar(shown + '@');
      setStandout(markedNow);
    } else {
      display_.putChar(c);
    }
    col++;
  }
  setStandout(false);
}

std::string ContentView::modeLineText() const {
  int total = static_cast<int>(lines_.size());
  int current = total > 0 ? cursorIndex_ + 1 : 0;
  return composeModeLineRow(ModeLine::format(current, total));
}

std::string ContentView::scrollModeLineText() const {
  int total = static_cast<int>(lines_.size());
  int bottomVisible = std::min(topIndex_ + std::max(height_, 0), total);
  int current;
  if (total <= 0) {
    current = 0;
  } else if (bottomVisible >= total) {
    // Scrolled (or coasted) all the way to the end -- topIndex_ itself
    // caps out at total-height_, so it can never reach `total` on its
    // own the way cursorIndex_ can in modeLineText(); checking
    // bottomVisible instead is what makes coasting to the bottom show
    // END, matching a real report that it stayed stuck on a
    // percentage there while plain 'j'/'s' navigation (modeLineText())
    // correctly reached END.
    current = total;
  } else {
    current = topIndex_ + 1;  // topIndex_==0 here naturally shows TOP
  }
  return composeModeLineRow(ModeLine::format(current, total));
}

void ContentView::setModeLineFieldsProvider(ModeLineFieldsProvider provider) {
  modeLineFieldsProvider_ = std::move(provider);
}

void ContentView::setModeLineRow(std::optional<int> row) { modeLineRow_ = row; }

ContentView::FieldFit ContentView::fitModeLineFields(const ModeLineFields &fields,
                                                       const std::string &positionText) const {
  int cols = display_.columns();
  auto rowLen = [&](bool withName, bool withTrailing) {
    int len = static_cast<int>(positionText.size());
    if (withName && !fields.name.empty()) len += static_cast<int>(fields.name.size()) + 1;
    if (withTrailing && !fields.trailing.empty()) len += static_cast<int>(fields.trailing.size()) + 1;
    return len;
  };
  if (cols <= 0 || rowLen(true, true) <= cols) return {true, true};
  if (rowLen(true, false) <= cols) return {true, false};
  return {false, false};
}

std::string ContentView::composeModeLineRow(const std::string &positionText) const {
  if (!modeLineFieldsProvider_) return positionText;
  ModeLineFields fields = modeLineFieldsProvider_(cursorIndex_);
  FieldFit fit = fitModeLineFields(fields, positionText);

  std::string row = positionText;
  if (fit.showTrailing && !fields.trailing.empty()) row += " " + fields.trailing;
  if (fit.showName && !fields.name.empty()) row = fields.name + " " + row;
  return row;
}

// Ported from print_line()'s current-line/total-line/percent display
// (see ModeLine's own class comment for why only this sliver of
// disp_window() moved into this class). row0_+height_ is always the
// one screen row every mode's viewport leaves unused for exactly this
// purpose (see Application::resize()'s "contentHeight = rows()-1"), so
// no bounds check is needed here beyond what setViewport()'s caller
// already guarantees.
//
// Draws name/position/trailing as SEPARATE reverse-video runs with a
// single plain (non-standout) space between each, rather than one
// continuous run over composeModeLineRow()'s combined string --
// ported from disp_window() itself calling vt_stand()/vt_endstand()
// around each field individually (put_str() for the name, then its
// own pair inside print_line() for the position, see disp.c). A real
// side-by-side comparison against CUI screenshots caught this: this
// port's first attempt drew the fully-composed string as one run,
// which reads as a single unbroken highlight bar -- CUI's mode-line
// visibly has small black (unhighlighted) gaps between fields.
void ContentView::renderModeLine() {
  int total = static_cast<int>(lines_.size());
  int current = total > 0 ? cursorIndex_ + 1 : 0;
  std::string positionText = ModeLine::format(current, total);
  int modeLineRow = modeLineRow_.value_or(row0_ + height_);

  display_.moveCursor(modeLineRow, 0);
  display_.clearToEndOfLine();

  auto drawSegment = [&](const std::string &text) {
    display_.setAttribute(Display::Attribute::Standout);
    for (unsigned char c : text) display_.putChar(c);
    display_.setAttribute(Display::Attribute::StandoutOff);
  };

  if (!modeLineFieldsProvider_) {
    drawSegment(positionText);
    return;
  }

  ModeLineFields fields = modeLineFieldsProvider_(cursorIndex_);
  FieldFit fit = fitModeLineFields(fields, positionText);
  int cols = display_.columns();

  int drawnLen = 0;
  if (fit.showName && !fields.name.empty()) {
    drawSegment(fields.name);
    display_.putChar(' ');
    drawnLen += static_cast<int>(fields.name.size()) + 1;
  }
  drawSegment(positionText);
  drawnLen += static_cast<int>(positionText.size());
  if (fit.showTrailing && !fields.trailing.empty()) {
    display_.putChar(' ');
    drawnLen += 1;
    std::string trailing = fields.trailing;
    if (fields.fillTrailingToEdge) {
      // Ported from disp_window()'s own `for (...) prev[i] = ' '`
      // loop: pad with spaces (still inside this field's own standout
      // run) so the highlight reaches the screen's right edge instead
      // of stopping right after the text -- see ModeLineFields'
      // fillTrailingToEdge comment for which modes want this.
      int remaining = cols - drawnLen - static_cast<int>(trailing.size());
      if (remaining > 0) trailing += std::string(remaining, ' ');
    }
    drawSegment(trailing);
    drawnLen += static_cast<int>(trailing.size());
  }

  // Ported from disp_window()'s own fallback: when there's no
  // "previous mode" text to show (mode-line[6] -- see
  // ModeLineFields::rightWatermark's own comment for exactly which
  // modes this is), the program name and version are right-aligned in
  // BOLD (not reverse-video) at the screen's own right edge instead --
  // confirmed against a real CUI screenshot showing "usilic v1.5b"
  // there on the directory-listing screen specifically (not file
  // view, where fillTrailingToEdge above already fills the row and
  // naturally leaves no room, matching legacy: prev0 is non-empty
  // there so this whole fallback branch never runs in the C source
  // either).
  if (!fields.rightWatermark.empty()) {
    std::string watermark = " " + fields.rightWatermark + " ";
    if (cols > 0 && drawnLen + static_cast<int>(watermark.size()) <= cols) {
      display_.moveCursor(modeLineRow, cols - static_cast<int>(watermark.size()));
      display_.setAttribute(Display::Attribute::Bold);
      for (unsigned char c : watermark) display_.putChar(c);
      display_.setAttribute(Display::Attribute::BoldOff);
    }
  }
}

void ContentView::redraw() {
  clampViewport();
  int highlighted = hoverIndex_.has_value() ? *hoverIndex_ : cursorIndex_;
  int rowOffset = bottomAnchorRowOffset();
  for (int row = 0; row < height_; row++) {
    display_.moveCursor(row0_ + row, 0);
    display_.clearToEndOfLine();
    if (row < rowOffset) continue;  // blank padding above the content, bottom-anchored only
    int idx = topIndex_ + (row - rowOffset);
    if (idx < static_cast<int>(lines_.size())) renderLine(lines_[idx], idx == highlighted);
  }
  renderModeLine();
  display_.flush();
}

}  // namespace fm
