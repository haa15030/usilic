#include "content_view.hpp"

#include <algorithm>
#include <vector>

#include "mode_line.hpp"
#include "utf8.hpp"

namespace fm {

namespace {

// Finder-style truncation for renderLine()'s own plain-line case (see
// its own call site comment): keep a prefix and a suffix, elide the
// middle with "...", instead of simply cutting the tail off. Byte-
// indexed (matching renderLine()'s own `col`, which counts bytes not
// display columns) but walks via Utf8::sequence_length() so a multi-
// byte UTF-8 character is never split across the cut.
//
// 2026-08-16 fix: originally biased 1/3 head, 2/3 tail (LOCATE's
// absolute paths sharing one long common prefix needed the DIFFERING
// suffix to stay visible), but a real report + screenshot (=bu) found
// this put the "..." too far toward the LEFT to read comfortably --
// "「...」の位置を行中央付近にしてほしい". A roughly even (1/2, 1/2)
// split still keeps enough of the tail to disambiguate entries sharing
// a long prefix (the original motivating case) while looking visually
// centered, matching legacy's own equivalent fix in alloc.c's
// alloc_str()/alloc_pp() (same day, same "..." near the middle request).
std::string middleEllipsisTruncate(const std::string &text, int maxBytes) {
  static const std::string kEllipsis = "...";
  int budget = maxBytes - static_cast<int>(kEllipsis.size());
  if (budget <= 0) return text.substr(0, static_cast<std::size_t>(std::max(0, maxBytes)));

  std::vector<int> boundaries;  // character-start byte offsets, text.size() itself included as the final one
  boundaries.push_back(0);
  int i = 0;
  while (i < static_cast<int>(text.size())) {
    i += Utf8::sequence_length(static_cast<unsigned char>(text[static_cast<std::size_t>(i)]));
    boundaries.push_back(std::min(i, static_cast<int>(text.size())));
  }

  int headBudget = budget / 2;
  int tailBudget = budget - headBudget;

  int headEnd = 0;
  for (int b : boundaries) {
    if (b > headBudget) break;
    headEnd = b;
  }

  int tailStart = static_cast<int>(text.size());
  for (std::size_t k = boundaries.size(); k-- > 0;) {
    int b = boundaries[k];
    if (static_cast<int>(text.size()) - b > tailBudget) break;
    tailStart = b;
  }

  if (tailStart <= headEnd) return text;  // too narrow to usefully elide -- let the plain cutoff below handle it
  return text.substr(0, static_cast<std::size_t>(headEnd)) + kEllipsis + text.substr(static_cast<std::size_t>(tailStart));
}

// Display-WIDTH-based sibling of middleEllipsisTruncate() above (2026-08-19
// fix, a direct user report -- ARC's own content listing: "内容一覧は
// ^Olでも2/3くらいまでしか表示されていないので右端までよろしく"), used
// ONLY by renderLine()'s own new widthAware rendering path below --
// DELIBERATELY a separate function, not a modification of the byte-based
// one above: withTruncatedFields()'s own mode-line name/trailing2
// truncation also calls middleEllipsisTruncate() (a completely different
// caller, several call-sites away), and its own surrounding budget math
// AND fitModeLineFields()'s own downstream "does this actually fit"
// tiers are BOTH byte-length based throughout -- an earlier version of
// this fix changed middleEllipsisTruncate() itself to be width-based,
// which silently broke that caller: a CJK-heavy name is truncated to a
// SHORT display width but can come back with MORE bytes than before
// (a 2-column CJK character costs 3 bytes), so fitModeLineFields()'s own
// byte-length "does it fit" check saw an even-longer string than the
// untruncated original and fell through every tier to hiding the WHOLE
// mode line (confirmed via a real reproduction: opening a CJK-named file
// from inside a tar archive left the mode line reading bare "1/1 END",
// nothing else). For CJK-heavy text (a 3-byte UTF-8 character is only 2
// DISPLAY columns wide), a byte budget is up to 3x stingier than the
// real available screen width -- exactly the "stops at 2/3 width"
// symptom the ARC report was about; `maxWidth` here is a genuine
// display-column budget, boundaries found via Utf8::sequence_length()
// (never splits a multi-byte character), head/tail accumulated by
// Utf8::display_width() per character instead of raw byte length.
std::string middleEllipsisTruncateByWidth(const std::string &text, int maxWidth) {
  static const std::string kEllipsis = "...";
  int budget = maxWidth - static_cast<int>(kEllipsis.size());  // "..." itself is 3 plain ASCII columns
  if (budget <= 0) return text.substr(0, static_cast<std::size_t>(std::max(0, maxWidth)));

  // boundaries[k]/boundaries[k+1] bracket the k-th character's own byte
  // span; widths[k] is that character's own display width (0/1/2).
  std::vector<int> boundaries;
  std::vector<int> widths;
  boundaries.push_back(0);
  int i = 0;
  while (i < static_cast<int>(text.size())) {
    int len = Utf8::sequence_length(static_cast<unsigned char>(text[static_cast<std::size_t>(i)]));
    len = std::min(len, static_cast<int>(text.size()) - i);
    widths.push_back(Utf8::display_width(reinterpret_cast<const unsigned char *>(text.data()) + i, len));
    i += len;
    boundaries.push_back(i);
  }

  int headBudget = budget / 2;
  int tailBudget = budget - headBudget;

  int headEnd = 0, headWidth = 0;
  for (std::size_t k = 0; k < widths.size(); k++) {
    if (headWidth + widths[k] > headBudget) break;
    headWidth += widths[k];
    headEnd = boundaries[k + 1];
  }

  int tailStart = static_cast<int>(text.size());
  int tailWidth = 0;
  for (std::size_t k = widths.size(); k-- > 0;) {
    if (tailWidth + widths[k] > tailBudget) break;
    tailWidth += widths[k];
    tailStart = boundaries[k];
  }

  if (tailStart <= headEnd) return text;  // too narrow to usefully elide -- let the plain cutoff below handle it
  return text.substr(0, static_cast<std::size_t>(headEnd)) + kEllipsis + text.substr(static_cast<std::size_t>(tailStart));
}

}  // namespace

ContentView::ContentView(Display &display) : display_(display) {}

void ContentView::setCursorAtModeLineTrailingEnd(bool enabled) { cursorAtModeLineTrailingEnd_ = enabled; }

void ContentView::setEmptyPlaceholder(Line line, bool marked) {
  emptyPlaceholderLine_ = std::move(line);
  emptyPlaceholderMarked_ = marked;
}

void ContentView::setViewport(int row0, int height) {
  row0_ = row0;
  height_ = height;
  clampViewport();
}

void ContentView::setLines(std::vector<Line> lines) {
  lines_ = std::move(lines);
  if (cursorIndex_ >= static_cast<int>(lines_.size()))
    cursorIndex_ = std::max(0, static_cast<int>(lines_.size()) - 1);
  // topIndex_, like hoverIndex_ below, is stale scroll-position state
  // left over from whatever content THIS view was showing before --
  // meaningless (and actively harmful) once that content is replaced
  // wholesale. Added after a real user report/reproduction: cpp/(7
  // entries) -> Enter into src/(74 entries) -> scroll several lines
  // down (leaving src/'s OWN topIndex_ well into double digits) ->
  // back up to cpp/ again. cursorIndex_ itself gets correctly re-
  // clamped/restored (both here and by the caller's own subsequent
  // setCursorIndex() call, e.g. DirectoryMode::enterDirectory()'s
  // history-position restore), but the OLD, much-larger topIndex_
  // from src/ survived into cpp/'s own redraw() -- clampViewport()'s
  // own "topIndex_ > cursorIndex_ ? snap down" branch only fires when
  // topIndex_ is GREATER than the (now-clamped, in-range) cursorIndex_,
  // which a merely-still-in-bounds stale topIndex_ doesn't necessarily
  // trip, so cpp/'s listing rendered scrolled almost to its own end
  // (showing only its last couple of entries) despite all 7 entries
  // easily fitting in one screen. Resetting to 0 here means every
  // fresh setLines() starts from a clean, correct scroll position,
  // with the caller's own setCursorIndex() (if any) free to scroll it
  // back down again via the normal clampViewport() path if the
  // restored cursor position genuinely needs it.
  topIndex_ = 0;
  // hoverIndex_ (a separate, purely visual override -- see its own
  // setter comment) can't be trusted to mean anything once the actual
  // content underneath it has been replaced wholesale: it was still an
  // index into the OLD lines_, and even when it happens to also be a
  // valid index into the NEW lines_ (the common case, since most lists
  // aren't drastically shorter), it's pointing at unrelated content by
  // coincidence, not by intent. Added after a real user report: after
  // descending into a subdirectory, hovering the mouse there, then
  // going back up (^G) to the parent, the row that ended up visibly
  // highlighted was wherever the LAST hover in the CHILD directory
  // happened to land -- not the actual cursor position (which itself
  // was correct, confirmed via the mode-line's own current/total
  // figures, but visually overridden by this stale hoverIndex_ per
  // redraw()'s own `hoverIndex_.has_value() ? *hoverIndex_ :
  // cursorIndex_` priority). Clearing it here, in the single place
  // ALL content replacement funnels through (DirectoryMode::
  // enterDirectory(), FileMode::loadFile()/reflow(), every other
  // Mode's own search/listing refresh), is far more robust than
  // trying to remember a setHoverIndex(nullopt) call at each of those
  // individual call sites (which is exactly the narrower approach
  // Application::handleCommand()'s Move/Page/GoTo/Redraw dispatch
  // already takes for KEYBOARD navigation -- see its own comment --
  // but that only ever covered moving within the SAME content, never
  // content being replaced outright).
  hoverIndex_ = std::nullopt;
  clampViewport();
}

const std::vector<ContentView::Line> &ContentView::lines() const { return lines_; }

std::size_t ContentView::totalByteCount() const {
  std::size_t bytes = 0;
  for (const auto &line : lines_) bytes += line.text.size() + 1;
  return bytes;
}

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

bool ContentView::isColumnWithinLineScope(int lineIndex, int column) const {
  if (lineIndex < 0 || lineIndex >= static_cast<int>(lines_.size())) return false;
  const Line &line = lines_[lineIndex];
  int start = std::max(0, line.highlightStartColumn.value_or(0));
  return column >= start && column < static_cast<int>(line.text.size());
}

int ContentView::cursorDisplayRow() const {
  if (cursorIndex_ < topIndex_ || cursorIndex_ >= topIndex_ + height_) return -1;
  return row0_ + bottomAnchorRowOffset() + (cursorIndex_ - topIndex_);
}

void ContentView::syncVisibleCursor() {
  // cursorAtModeLineTrailingEnd_ (2026-08-12 fix): see its own setter
  // comment. Checked FIRST, ahead of the normal highlighted-content-
  // row logic below -- a direct user correction: "『候補一覧のハイ
  // ライト行末尾へ正しくカーソルを移動する設計』が有効なのはハイ
  // ライト行を選択する場合の話で文字入力はモードラインで行われる
  // ので最下行の文字列末尾にないとおかしい" (that "cursor moves to
  // the end of the highlighted content row" design is right for
  // SELECTING a highlighted row, but text INPUT happens in the mode
  // line, so the cursor belongs at the end of the candidate's own text
  // THERE instead). Confirmed via a real macOS report: ATOK's own
  // input-mode indicator icon (which macOS draws directly under the
  // system's actual current text-insertion caret) was appearing over
  // the TOP content row instead of the mode line -- independent,
  // OS-level proof that the caret this port was telling the terminal
  // about really was in the wrong place, not just a cosmetic
  // preference.
  if (cursorAtModeLineTrailingEnd_ && lastModeLineTrailingEndColumn_ >= 0) {
    display_.setVisibleCursor(modeLineRow_.value_or(row0_ + height_), lastModeLineTrailingEndColumn_);
    return;
  }
  if (!cursorHighlightEnabled_) {
    int row = cursorDisplayRow();
    if (row >= 0) {
      display_.setVisibleCursor(row, 0);
      return;
    }
    display_.hideVisibleCursor();
    return;
  }
  // cursorHighlightEnabled_ (2026-08-05 fix): the SGR reverse-video run
  // renderLine() draws for the current row is what's SUPPOSED to show
  // "you are here" here, so this branch used to just hideVisibleCursor()
  // the terminal's own separate block cursor out of the way -- but
  // TerminalDisplay::hideVisibleCursor() is a no-op (there's no ANSI
  // "hide cursor" sequence wired up there, see its own comment), so on
  // the real terminal backend that call did nothing at all: a real user
  // report found the actual block cursor just sitting wherever the last
  // screen write happened to land (typically the mode-line's own right-
  // aligned category badge, e.g. after item 166/167's "CUT"/"FGREP"
  // watermark), and on an EMPTY list (nothing for renderLine() to
  // highlight at all -- see redraw()'s own "--" placeholder comment)
  // there was no reverse-video row to compete with it either, so the
  // current position wasn't visually indicated by anything. Moving it
  // explicitly to the highlighted row's own right edge keeps the real
  // cursor visually attached to the current selection instead of
  // wandering off to whatever was drawn most recently.
  int row = cursorDisplayRow();
  if (row < 0) {
    display_.hideVisibleCursor();
    return;
  }
  int width = static_cast<int>(emptyPlaceholderLine_.text.size());  // redraw()'s own empty-list placeholder
  if (cursorIndex_ >= 0 && cursorIndex_ < static_cast<int>(lines_.size()))
    width = static_cast<int>(lines_[cursorIndex_].text.size());
  display_.setVisibleCursor(row, width);
}

int ContentView::topIndex() const { return topIndex_; }

void ContentView::setTopIndex(int index) {
  int maxTop = std::max(0, static_cast<int>(lines_.size()) - std::max(height_, 0));
  topIndex_ = std::clamp(index, 0, maxTop);
}

bool ContentView::stepTopIndex(int direction) {
  if (!canStepTopIndex(direction)) return false;
  if (direction > 0) {
    topIndex_++;
    cursorIndex_ = std::min(topIndex_ + height_ - 1, static_cast<int>(lines_.size()) - 1);
  } else if (direction < 0) {
    topIndex_--;
    cursorIndex_ = topIndex_;
  }
  return true;
}

// Non-mutating peek at exactly the same boundary condition
// stepTopIndex() itself enforces -- added 2026-08-10 so a caller can
// check "is there room to move THIS way at all" BEFORE committing to a
// pixel-offset move, rather than only discovering the boundary AFTER
// pixelOffset has already accumulated a full cellHeight's worth (see
// main.cpp's own coast-loop call site comment for the real bug this
// fixes: a few frames of blank space briefly visible past the actual
// top/bottom row during inertial coasting, "空行を表示したのち一行分
// 戻って正規画面になる").
bool ContentView::canStepTopIndex(int direction) const {
  if (height_ <= 0 || lines_.empty()) return false;
  int maxTop = std::max(0, static_cast<int>(lines_.size()) - height_);
  if (direction > 0) return topIndex_ < maxTop;
  if (direction < 0) return topIndex_ > 0;
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
  bool standoutOn = false, boldOn = false, underlineOn = false;
  auto setAttr = [&](bool &state, bool on, Display::Attribute onAttr, Display::Attribute offAttr) {
    if (on == state) return;
    display_.setAttribute(on ? onAttr : offAttr);
    state = on;
  };
  auto setStandout = [&](bool on) {
    setAttr(standoutOn, on, Display::Attribute::Standout, Display::Attribute::StandoutOff);
  };
  // Line::attributes' own Bold/Underline byte-ranges (2026-08-05, see
  // its own comment) -- independent of setStandout() above (a marked/
  // reverse-video line can still carry its own bold/underline runs
  // underneath, same as any other terminal app combining SGR codes).
  auto setBold = [&](bool on) { setAttr(boldOn, on, Display::Attribute::Bold, Display::Attribute::BoldOff); };
  auto setUnderline = [&](bool on) {
    setAttr(underlineOn, on, Display::Attribute::Underline, Display::Attribute::UnderlineOff);
  };

  // `markedNow` starts true only if highlightStart is 0 (the common
  // case, and every caller but DirectoryMode's ls-listing) -- see
  // Line::highlightStartColumn's own comment. It flips on exactly once
  // the loop below reaches column highlightStart.
  bool markedNow = marked && highlightStart <= 0;
  setStandout(markedNow);
  int col = 0;
  // 2026-08-13 fix: a real report (LOCATE results, screenshots =am/=an)
  // -- long absolute paths landing EXACTLY at the terminal's own width
  // (80 columns) left the reverse-video highlight visually one row
  // off (row N's highlight appearing to land on row N+1), and the
  // FOLLOWING row's Enter became unresponsive. Root-caused via a PTY
  // reproduction + temporary instrumentation: ContentView's own
  // cursorIndex_/topIndex_/marked computation was confirmed correct at
  // every step (logged and matched the mode-line's own position text
  // exactly) -- the bug was downstream, in the raw terminal bytes.
  // Writing a character to the terminal's LAST column triggers most
  // terminals' own auto-wrap (the real cursor silently advances to the
  // next row's start), which this class's own escape-sequence emission
  // (setStandout(false)/etc. at the end of THIS function, see below)
  // doesn't know happened -- those trailing "turn the attribute back
  // off" codes end up applied at the START of the NEXT row instead of
  // at the END of THIS one, since non-printing SGR codes don't
  // themselves trigger a wrap and so land wherever the cursor already
  // silently moved to. Confirmed by reproducing with both long
  // (80-column, bug present) and short (<80-column, bug absent) real
  // `locate` results in the same session. Capping at columns()-1
  // (reserving the very last column, a standard terminal-programming
  // convention for exactly this reason) avoids the wrap outright,
  // matching legacy's own line-truncation precedent elsewhere (see
  // CLAUDE.md history item 5's Step 5) -- this is the shared,
  // centralized fix point every renderLine() caller benefits from at
  // once, not a per-Mode workaround.
  // Only applied when THIS call might toggle Standout on at all
  // (`marked`, fixed for the whole call -- not `markedNow`, which can
  // still flip mid-line via highlightStart): a plain marked=false line
  // never calls setStandout(true), so its own trailing resets are
  // already no-ops regardless of any auto-wrap, and truncating it too
  // would be a pure regression with no corresponding fix -- confirmed
  // by handleResize()'s own File-mode-reflow tests (cursorHighlightEnabled_
  // false there, see FileMode's own class comment), which intentionally
  // rely on full-width wrapped lines rendering their LAST column.
  int maxCol = marked ? std::max(0, display_.columns() - 1) : display_.columns();
  // Middle-ellipsis truncation (2026-08-14): a direct user report --
  // LOCATE results sharing one long common prefix all rendered
  // IDENTICAL visible text once naively cut off at the terminal's own
  // width, making individual entries impossible to tell apart
  // ("行末が同じで区別できない"), matching macOS Finder's own
  // convention for long filenames ("macOSのFinderと同じく行の中央を
  // 「...」として行末まで表示して欲しい" -- elide the MIDDLE, keep
  // both the start and the end visible). Scoped to lines with NO
  // highlightStartColumn/attributes (plain, unstyled text) -- the
  // only shape this port's own results-list Lines actually use
  // (LocateMode/DuMode/etc never set either): rewriting a highlighted
  // SUBSTRING's own byte-offset-indexed attribute ranges through a
  // middle-elided string is a genuinely different, harder problem
  // this doesn't attempt to solve here -- Directory's own ls-listing
  // highlightStartColumn, GrepMode's match highlighting, etc. keep
  // their existing simple right-truncation (the loop's own `col >=
  // maxCol` break below) unchanged.
  // 2026-08-14 fix: the ellipsis budget below is columns()-1 whenever
  // Standout can toggle on THIS view at all (cursorHighlightEnabled_),
  // rather than reusing maxCol's own marked-dependent value directly --
  // a real report ("ハイライト時は「...bat Uninstaller.app/...」だった
  // のが「...obat Uninstaller.app/...」", reproduced by pressing ^N to
  // move off the row, then confirmed to flip back by re-hovering it
  // with the mouse) traced to exactly this: maxCol is marked-dependent
  // (columns()-1 vs. columns(), see its own comment just above) for a
  // reason that has nothing to do with ellipsis truncation (avoiding an
  // unrelated auto-wrap bug that only matters when Standout gets
  // toggled). Since ellipsis truncation runs on marked=false plain
  // lines just as often as marked=true ones (LOCATE/DU results scroll
  // past the cursor constantly), reusing maxCol made the SAME row's own
  // rendered text visibly shift by one character depending purely on
  // whether it happened to be the highlighted row at that instant -- a
  // genuine, user-visible flicker, not two independently-valid
  // renderings. Gated on cursorHighlightEnabled_ (not unconditional):
  // when it's false, `marked` is always false for every row regardless
  // of cursor position (see this function's own opening line), so
  // there's no flicker to fix and maxCol already equals the full
  // columns() -- exactly the full-width rendering
  // "redraw: an UNMARKED line exactly as wide as the terminal still
  // renders its own last column" (and handleResize()'s own File-mode-
  // reflow tests) deliberately rely on; forcing columns()-1 here
  // unconditionally regressed that.
  int ellipsisBudget = cursorHighlightEnabled_ ? std::max(0, display_.columns() - 1) : maxCol;
  // widthAware (2026-08-19 fix, same report as middleEllipsisTruncate()'s
  // own comment): the exact same condition that already gates ellipsis
  // eligibility -- a plain line with no highlightStartColumn/attributes,
  // i.e. LOCATE/DU/Archive-style results, the only Lines in this port
  // that reach here effectively unprocessed (Directory's ls-listing and
  // GrepMode's match highlighting, the two real users of
  // highlightStartColumn/attributes, need `col` to stay a precise BYTE
  // offset for their own byte-range comparisons below, so they keep the
  // original per-byte loop entirely unchanged). For THIS category only,
  // `col` becomes a real display-column count instead of a byte count,
  // consuming each UTF-8 sequence as one unit (via Utf8::sequence_length())
  // and advancing by its own Utf8::display_width() -- otherwise a 3-byte,
  // 2-column CJK character would only ever count as "1 byte" against
  // maxCol, silently capping real content at roughly HALF the terminal's
  // actual width for Japanese-heavy lines (a direct user report: ARC's
  // own filenames stopped around 2/3 across, leaving the right side of
  // the row blank).
  bool widthAware = !line.highlightStartColumn.has_value() && line.attributes.empty();
  std::string ellipsisText;
  const std::string *renderedText = &line.text;
  if (widthAware && static_cast<int>(line.text.size()) > ellipsisBudget) {
    ellipsisText = middleEllipsisTruncateByWidth(line.text, ellipsisBudget);
    renderedText = &ellipsisText;
  }
  if (widthAware) {
    const std::string &text = *renderedText;
    std::size_t idx = 0;
    while (idx < text.size()) {
      unsigned char c = static_cast<unsigned char>(text[idx]);
      int len = Utf8::sequence_length(c);
      len = std::min(len, static_cast<int>(text.size() - idx));
      if (len <= 1) {
        // Same tab/control-byte handling as the byte-based loop below,
        // just column-counted by 1 (an ASCII byte is always 1 display
        // column) instead of by raw byte offset.
        if (col >= maxCol) break;
        if (c == '\t') {
          display_.putChar(' ');
        } else if (c && (c < ' ' || c == 0x7f)) {
          setStandout(!markedNow);
          int shown = (c == 0x7f) ? c - 0x80 : c;
          display_.putChar(shown + '@');
          setStandout(markedNow);
        } else {
          display_.putChar(c);
        }
        col += 1;
        idx += 1;
      } else {
        int width = Utf8::display_width(reinterpret_cast<const unsigned char *>(text.data()) + idx, len);
        // Never let a wide (2-column) character straddle the reserved
        // true-last-column boundary -- `col >= maxCol` alone isn't
        // enough here since a 2-wide char starting at maxCol-1 would
        // still pass that check yet finish AT maxCol, exactly the
        // auto-wrap hazard renderLine()'s own maxCol reservation
        // (see its comment above) exists to avoid.
        if (col + std::max(width, 0) > maxCol) break;
        for (int b = 0; b < len; b++) display_.putChar(static_cast<unsigned char>(text[idx + static_cast<std::size_t>(b)]));
        col += width;
        idx += static_cast<std::size_t>(len);
      }
    }
    setStandout(false);
    setBold(false);
    setUnderline(false);
    return;
  }
  for (unsigned char c : *renderedText) {
    if (col >= maxCol) break;
    if (col == highlightStart) {
      markedNow = marked;
      setStandout(markedNow);
    }
    if (!line.attributes.empty()) {
      bool wantBold = false, wantUnderline = false;
      for (const AttributeRun &run : line.attributes) {
        if (col < run.start || col >= run.end) continue;
        if (run.attribute == Display::Attribute::Bold) wantBold = true;
        else if (run.attribute == Display::Attribute::Underline) wantUnderline = true;
      }
      setBold(wantBold);
      setUnderline(wantUnderline);
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
  setBold(false);
  setUnderline(false);
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

std::vector<Display::ModeLineSegment> ContentView::scrollModeLineSegments() const {
  int total = static_cast<int>(lines_.size());
  int bottomVisible = std::min(topIndex_ + std::max(height_, 0), total);
  int current;
  if (total <= 0) {
    current = 0;
  } else if (bottomVisible >= total) {
    current = total;  // see scrollModeLineText()'s own comment on why this check, not topIndex_ itself
  } else {
    current = topIndex_ + 1;
  }
  std::string positionText = ModeLine::format(current, total);

  std::vector<Display::ModeLineSegment> segments;
  if (!modeLineFieldsProvider_) {
    segments.push_back({positionText, true});
    return segments;
  }
  ModeLineFields fields = withTruncatedFields(modeLineFieldsProvider_(cursorIndex_), positionText);
  FieldFit fit = fitModeLineFields(fields, positionText);
  int cols = display_.columns();
  int drawnLen = 0;
  if (fit.showName && !fields.name.empty()) {
    segments.push_back({fields.name, true});
    segments.push_back({" ", false});
    drawnLen += static_cast<int>(fields.name.size()) + 1;
  }
  segments.push_back({positionText, true});
  drawnLen += static_cast<int>(positionText.size());
  // 2026-08-11 fix: see renderModeLine()'s own identical fix -- each
  // field's own emptiness now gates only itself, so trailing2 no
  // longer silently disappears whenever trailing itself is empty.
  if (fit.showTrailing && !fields.trailing.empty()) {
    segments.push_back({" ", false});
    drawnLen += 1;
    std::string trailing = fields.trailing;
    // See renderModeLine()'s own identical trailing2 handling -- the
    // edge-padding applies to trailing2 (the last segment) when
    // present, not to trailing itself.
    if (fields.fillTrailingToEdge && fields.trailing2.empty()) {
      // See renderModeLine()'s own identical padding -- keeps the
      // highlight reaching the screen's right edge in the modes that
      // want that, same as the flat-string version does.
      int remaining = cols - drawnLen - static_cast<int>(trailing.size());
      if (remaining > 0) trailing += std::string(remaining, ' ');
    }
    segments.push_back({trailing, !fields.trailingPlain});  // see ModeLineFields::trailingPlain's own comment
    drawnLen += static_cast<int>(trailing.size());
  }
  if (fit.showTrailing2 && !fields.trailing2.empty()) {
    segments.push_back({" ", false});
    drawnLen += 1;
    std::string trailing2 = fields.trailing2;
    if (fields.fillTrailingToEdge) {
      int remaining = cols - drawnLen - static_cast<int>(trailing2.size());
      if (remaining > 0) trailing2 += std::string(remaining, ' ');
    }
    segments.push_back({trailing2, true});
  }
  return segments;
}

void ContentView::setModeLineFieldsProvider(ModeLineFieldsProvider provider) {
  modeLineFieldsProvider_ = std::move(provider);
}

void ContentView::setModeLineRow(std::optional<int> row) { modeLineRow_ = row; }

std::string ContentView::truncateFieldByKind(const std::string &text, TrailingKind kind, int budget) const {
  if (budget <= 0) return "";
  if (static_cast<int>(text.size()) <= budget) return text;
  if (budget < 4) return "";  // no room even for "..." + one real character
  if (kind == TrailingKind::PathMiddle) return middleEllipsisTruncate(text, budget);
  int tailChars = kind == TrailingKind::QueryEcho ? std::min(16, budget - 3) : budget - 3;
  return "..." + text.substr(text.size() - static_cast<std::size_t>(tailChars));
}

ContentView::ModeLineFields ContentView::withTruncatedFields(ModeLineFields fields,
                                                                  const std::string &positionText) const {
  int cols = display_.columns();
  if (cols <= 0) return fields;
  // 2026-08-15 fix: name itself can be arbitrarily long (e.g.
  // DirectoryMode::currentDirectory() after Application::
  // goToFileSelector() jumps straight into a deeply-nested path) --
  // a real report + screenshot (=ba, "モードラインが不調（←ディレクト
  // リ名長過ぎ？」) found an over-long name corrupted the whole mode-
  // line row instead of just failing to fit (fitModeLineFields()'s own
  // tiers never drop name at all, so it was drawn in full regardless
  // of width). Truncated here, in the one place ALL THREE mode-line
  // render paths already funnel every field through -- reserves room
  // for AT LEAST the position text (the one field this port never
  // drops), same budget-then-truncate shape as trailing2 below.
  //
  // 2026-08-15 fix: the first version of this only reserved room for
  // position, greedily truncating name to fill the ENTIRE remainder --
  // a real report + screenshot (=bd, a deep directory's own mode line
  // missing BOTH "(ls)" and the "usilic v1.5b" rightWatermark, unlike
  // the reference =be) found that left zero room for trailing/
  // rightWatermark, so fitModeLineFields()'s own later tiers correctly
  // (by their own logic) decided neither fit anymore and dropped both.
  // Reserves room for trailing and rightWatermark too when computing
  // name's own budget, so a short, high-value pair like "(ls)"/
  // "usilic v1.5b" survives alongside a truncated name instead of
  // being silently squeezed out. trailing2 isn't reserved for here --
  // its own budget logic just below already decides independently
  // whether it fits in whatever's left.
  //
  // Each field is reserved for only if it's individually short
  // (<= kReservableFieldLength) -- a real regression found this same
  // reservation, applied unconditionally, made a doctest fixture's own
  // "CUT" name vanish entirely: the ^J^C candidate list's own
  // `trailing` is a 70-character candidate-preview string that was
  // NEVER going to fit alongside name+position regardless (and
  // fitModeLineFields()'s own EXISTING tiers already correctly drop it
  // in that case -- see the 2026-08-11 fix), but reserving room for
  // its full length here starved name down to 2 characters before that
  // logic ever got a chance to run. "(ls)" (4)/byte counts (~10)/
  // "usilic v1.5b" (12) all comfortably clear this cap; an arbitrary
  // long preview/annotation does not, and is deliberately left for
  // fitModeLineFields()'s own tiers to decide instead.
  constexpr int kReservableFieldLength = 20;
  // +1: the gap between name and position itself (renderModeLine()
  // always draws this once both are shown, same as every other tier
  // in this function already accounts for via rowLen()'s own "+1").
  int reserved = 1;
  // Trailing is drawn as " " + trailing (renderModeLine()'s own
  // drawnLen tracking): 1 separator + its own length.
  if (!fields.trailing.empty() && static_cast<int>(fields.trailing.size()) <= kReservableFieldLength) {
    reserved += 1 + static_cast<int>(fields.trailing.size());
  }
  // rightWatermark is drawn as " " + rightWatermark + " " (see its own
  // renderModeLine() call site: `watermark = " " + rightWatermark +
  // " "`) -- TWO extra spaces, not one. A real regression (this
  // reservation initially used just "1 +", one short) found the
  // watermark still silently dropped by renderModeLine()'s own
  // `drawnLen + watermark.size() <= cols` check even after this fix's
  // first attempt, landing exactly 1 column short.
  if (!fields.rightWatermark.empty() && static_cast<int>(fields.rightWatermark.size()) <= kReservableFieldLength) {
    reserved += 2 + static_cast<int>(fields.rightWatermark.size());
  }
  // trailing2 as well (e.g. DirectoryMode's own "(ls)" fallback,
  // always populated even without a search annotation) -- a real
  // regression found reserving for trailing/rightWatermark alone still
  // wasn't enough: trailing2's OWN downstream truncation logic (just
  // below) tries to fit it into whatever budget REMAINS after name,
  // but that budget had already been fully spent reserving exactly
  // enough for trailing+rightWatermark and nothing more, so a short
  // trailing2 like "(ls)" pushed the total back over budget anyway,
  // silently dropping the watermark again via the SAME downstream
  // `drawnLen + watermark.size() <= cols` check. Not reserved for if
  // it's the one being middle-ellipsis-truncated (FileMode's own long
  // LOCATE-path annotation) -- those are long by construction, well
  // past kReservableFieldLength, so they're excluded the same way an
  // overly-long trailing already is.
  if (!fields.trailing2.empty() && static_cast<int>(fields.trailing2.size()) <= kReservableFieldLength) {
    reserved += 1 + static_cast<int>(fields.trailing2.size());
  }
  int positionBudget = cols - static_cast<int>(positionText.size()) - reserved;
  if (positionBudget < 0) {
    // Not enough room even reserving for trailing/rightWatermark too --
    // fall back to the ORIGINAL, unconditional guarantee (name fits
    // alongside AT LEAST position); fitModeLineFields()'s own tiers
    // still decide whether trailing/trailing2 actually get shown from
    // here.
    positionBudget = cols - static_cast<int>(positionText.size()) - 1;
  }
  if (positionBudget < 0) positionBudget = 0;
  if (static_cast<int>(fields.name.size()) > positionBudget) {
    fields.name = positionBudget >= 4 ? middleEllipsisTruncate(fields.name, positionBudget)
                                       : fields.name.substr(0, static_cast<std::size_t>(positionBudget));
  }

  // 2026-08-17 addition: truncate `trailing` itself when it opts into
  // truncation (trailingKind != Tag), instead of leaving it entirely to
  // fitModeLineFields()'s own all-or-nothing tiers below -- a real
  // reproduction (=bw) found listBrowserView_'s own FILE/DIR/COMMAND
  // picker `trailing` (a long absolute path, no trailing2 involved)
  // vanishing ENTIRELY once it exceeded the column budget, because
  // `trailing` had no truncation option at all before this (unlike
  // `name`/`trailing2`, both truncated above/below). Every OTHER
  // provider leaves trailingKind at its default (Tag), so this block is
  // a no-op for them. Reserves room for trailing2 too (the same
  // kReservableFieldLength heuristic name's own reservation above
  // already uses) purely as a safety margin -- no current provider
  // combines a truncatable `trailing` with a non-empty `trailing2`.
  if (fields.trailingKind != TrailingKind::Tag && !fields.trailing.empty()) {
    int base = static_cast<int>(positionText.size());
    if (!fields.name.empty()) base += static_cast<int>(fields.name.size()) + 1;
    int trailing2Reserved = 0;
    if (!fields.trailing2.empty() && static_cast<int>(fields.trailing2.size()) <= kReservableFieldLength) {
      trailing2Reserved = 1 + static_cast<int>(fields.trailing2.size());
    }
    int budget = cols - base - 1 - trailing2Reserved;
    fields.trailing = truncateFieldByKind(fields.trailing, fields.trailingKind, budget);
  }

  if (fields.trailing2.empty()) return fields;
  int base = static_cast<int>(positionText.size());
  if (!fields.name.empty()) base += static_cast<int>(fields.name.size()) + 1;
  int trailingLen = fields.trailing.empty() ? 0 : static_cast<int>(fields.trailing.size()) + 1;
  int budgetWith = cols - base - trailingLen - 1;      // room for trailing2 alongside trailing
  int budgetWithout = cols - base - 1;                 // room for trailing2 if trailing were dropped
  int trailing2Len = static_cast<int>(fields.trailing2.size());

  if (trailing2Len <= budgetWith) return fields;  // already fits fully alongside trailing -- nothing to do

  // 2026-08-14 fix: was tail-only ("..." + the very end), which loses
  // the HEAD entirely (e.g. FileMode::setSearchOrigin()'s full
  // absolute path lost its "/Applications/..." start). A direct user
  // request ("戻り値も検索一覧同様「/Applications/Adobe Acrob...
  // source/.../HKSCS.txt」形式で") asked for the SAME middle-ellipsis
  // convention LOCATE's own results-list rows already use (head...tail,
  // see middleEllipsisTruncate()'s own comment) here too -- both ends
  // of a full path carry real information (where it lives, what it's
  // named), unlike this function's own trailing/trailing2 CHOICE logic
  // just above, which is about which FIELD to keep, not how to elide
  // one.
  //
  // 2026-08-15 fix: that middle-ellipsis switch turned out to be too
  // broad -- applied here UNCONDITIONALLY, it also changed every OTHER
  // mode's own trailing2 (e.g. DirectoryMode::searchAnnotation_), which
  // a direct user policy statement clarified should stay tail-only:
  // "なおLOCATE以外では、右下端の戻り値が長すぎる場合「...16文字」で
  // よろしく". Now gated on fields.trailing2Kind (2026-08-17: was
  // trailing2MiddleEllipsis, see TrailingKind's own comment) -- FileMode's
  // own LOCATE-origin case opts in explicitly; every other provider
  // keeps the ORIGINAL tail-only behavior (the QueryEcho default),
  // capped at the SAME 16 characters truncatedTrailingPath() already
  // established elsewhere (but still budget-aware: never shows MORE
  // than what actually fits, unlike that helper's own fixed-width
  // version used where the string is pre-truncated before it ever
  // reaches a ModeLineFields). Delegates to truncateFieldByKind() (see
  // its own comment), the same helper the new `trailing`-truncation
  // block above now shares.
  auto truncate = [&](int budget) -> std::string {
    return truncateFieldByKind(fields.trailing2, fields.trailing2Kind, budget);
  };

  if (trailing2Len <= budgetWithout && !fields.trailing.empty()) {
    // Dropping trailing lets trailing2 show in FULL -- worth it, same
    // preference the 2026-08-11 GREP fix established (a long candidate
    // preview vs. a short destination directory).
    fields.trailing.clear();
    return fields;
  }
  if (budgetWith >= 4) {
    fields.trailing2 = truncate(budgetWith);
    return fields;
  }
  if (budgetWithout >= 4 && !fields.trailing.empty()) {
    fields.trailing.clear();
    fields.trailing2 = truncate(budgetWithout);
    return fields;
  }
  fields.trailing2.clear();
  return fields;
}

ContentView::FieldFit ContentView::fitModeLineFields(const ModeLineFields &fields,
                                                       const std::string &positionText) const {
  int cols = display_.columns();
  auto rowLen = [&](bool withName, bool withTrailing, bool withTrailing2) {
    int len = static_cast<int>(positionText.size());
    if (withName && !fields.name.empty()) len += static_cast<int>(fields.name.size()) + 1;
    if (withTrailing && !fields.trailing.empty()) len += static_cast<int>(fields.trailing.size()) + 1;
    if (withTrailing2 && !fields.trailing2.empty()) len += static_cast<int>(fields.trailing2.size()) + 1;
    return len;
  };
  if (cols <= 0 || rowLen(true, true, true) <= cols) return {true, true, true};
  // 2026-08-11 fix: `trailing` and `trailing2` used to be dropped
  // together as one all-or-nothing unit whenever the combined width
  // overflowed -- a real report found the ^J^C candidate list's own
  // long regex-pattern preview (`trailing`) pushing the row over the
  // column budget, which silently dropped trailing2 (the destination
  // directory) too even though trailing2 ALONE would easily have
  // fit. `trailing` (a short decorative tag like "(fgrep)" for every
  // OTHER provider, or here a long, less essential live-preview) is
  // now dropped first, keeping trailing2 (the more load-bearing
  // context -- a directory path) visible whenever there's room for it
  // on its own.
  if (rowLen(true, false, true) <= cols) return {true, false, true};
  // 2026-08-14 fix: the tier above assumes dropping `trailing` alone
  // is always enough once `trailing2` fits on its own -- true for
  // GREP's own case (a long preview vs. a short destination
  // directory), but FALSE whenever trailing2 itself is the long field
  // (e.g. FileMode's directory, or its new LOCATE-path annotation --
  // see FileMode::setSearchAnnotation()) and trailing is short (e.g.
  // FileMode's byte count). A real report ("モードラインにバイト数
  // 「397723」...が無い") found exactly this: a long directory alone
  // still didn't fit, so BOTH tiers above failed and the fallback below
  // dropped trailing (the byte count) too, even though trailing ALONE
  // would easily have fit without trailing2 in the way. Tried before
  // giving up entirely, so a short, useful trailing is shown whenever
  // it's the long trailing2 that's actually the problem.
  if (rowLen(true, true, false) <= cols) return {true, true, false};
  if (rowLen(true, false, false) <= cols) return {true, false, false};
  return {false, false, false};
}

std::string ContentView::composeModeLineRow(const std::string &positionText) const {
  if (!modeLineFieldsProvider_) return positionText;
  ModeLineFields fields = withTruncatedFields(modeLineFieldsProvider_(cursorIndex_), positionText);
  FieldFit fit = fitModeLineFields(fields, positionText);

  std::string row = positionText;
  // 2026-08-11 fix: trailing2 used to be nested inside `!fields.
  // trailing.empty()`, so a field with a non-empty trailing2 but an
  // EMPTY trailing (e.g. the ^J^C candidate list's own provider, whose
  // `trailing` is the currently-highlighted item and can legitimately
  // be empty on an empty ring while trailing2 -- the destination
  // directory -- still has something to show) silently dropped
  // trailing2 entirely. Each field's own emptiness now gates only
  // itself.
  if (fit.showTrailing && !fields.trailing.empty()) row += " " + fields.trailing;
  if (fit.showTrailing2 && !fields.trailing2.empty()) row += " " + fields.trailing2;
  // See ModeLineFields::trailing3's own comment -- always appended when
  // present (no fit-tier gating, matching renderModeLine()'s own
  // permissive drop-if-no-room policy; this plain-string consumer has no
  // column budget of its own to check against here).
  if (!fields.trailing3.empty()) row += " " + fields.trailing3;
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
  // See ModeLineFields::nameUnderlined's own comment.
  auto drawUnderlineSegment = [&](const std::string &text) {
    display_.setAttribute(Display::Attribute::Underline);
    for (unsigned char c : text) display_.putChar(c);
    display_.setAttribute(Display::Attribute::UnderlineOff);
  };

  if (!modeLineFieldsProvider_) {
    drawSegment(positionText);
    return;
  }

  ModeLineFields rawFields = modeLineFieldsProvider_(cursorIndex_);
  // See ModeLineFields::splitLayoutWhenCrowded's/renderSplitModeLine()'s
  // own comments. Checked against the RAW (untruncated) fields, not
  // withTruncatedFields()'s output -- a naive sum of the actual field
  // lengths is exactly what decides whether the normal layout would
  // have needed to start dropping fields in the first place.
  if (rawFields.splitLayoutWhenCrowded) {
    int cols = display_.columns();
    int naiveLen = static_cast<int>(positionText.size());
    if (!rawFields.name.empty()) naiveLen += static_cast<int>(rawFields.name.size()) + 1;
    if (!rawFields.trailing.empty()) naiveLen += static_cast<int>(rawFields.trailing.size()) + 1;
    if (!rawFields.trailing2.empty()) naiveLen += static_cast<int>(rawFields.trailing2.size()) + 1;
    if (cols > 0 && naiveLen > cols) {
      renderSplitModeLine(rawFields, positionText);
      return;
    }
  }

  ModeLineFields fields = withTruncatedFields(rawFields, positionText);
  FieldFit fit = fitModeLineFields(fields, positionText);
  int cols = display_.columns();

  int drawnLen = 0;
  if (fit.showName && !fields.name.empty()) {
    if (fields.nameUnderlined) drawUnderlineSegment(fields.name);
    else drawSegment(fields.name);
    display_.putChar(' ');
    drawnLen += static_cast<int>(fields.name.size()) + 1;
  }
  drawSegment(positionText);
  drawnLen += static_cast<int>(positionText.size());
  // 2026-08-11 fix: trailing2 used to be nested inside `!fields.
  // trailing.empty()`, silently dropping it whenever trailing itself
  // was empty (e.g. the ^J^C candidate list's own provider, whose
  // `trailing` -- the currently-highlighted item -- can legitimately
  // be empty on an empty ring while trailing2, the destination
  // directory, still has something to show). Each field's own
  // emptiness now gates only itself.
  if (fit.showTrailing && !fields.trailing.empty()) {
    display_.putChar(' ');
    drawnLen += 1;
    std::string trailing = fields.trailing;
    // Edge-padding (see below) applies to trailing2 when present (the
    // LAST field actually drawn) -- see ModeLineFields::trailing2's
    // own comment for why `trailing` itself is never padded once
    // trailing2 exists (padding it too would erase the plain gap
    // between the two that trailing2 exists to create in the first
    // place).
    // 2026-08-19 fix: also defers to trailing3 (see its own comment)
    // when present, same reasoning as trailing2's own identical defer
    // just below -- unreachable for ArchiveMode today (trailing2 and
    // trailing3 are always set together there) but kept consistent so
    // a future trailing2-less, trailing3-only provider doesn't silently
    // lose its own edge-fill to `trailing` eating the whole row first.
    if (fields.fillTrailingToEdge && fields.trailing2.empty() && fields.trailing3.empty()) {
      // Ported from disp_window()'s own `for (...) prev[i] = ' '`
      // loop: pad with spaces (still inside this field's own standout
      // run) so the highlight reaches the screen's right edge instead
      // of stopping right after the text -- see ModeLineFields'
      // fillTrailingToEdge comment for which modes want this.
      // Capped at cols-1, NOT cols (2026-08-18 fix, a real report:
      // RecentFile/RecentDirectory's own candidate-list mode line, the
      // first caller with no rightWatermark to incidentally mask this,
      // still looked unfilled on a real Terminal.app despite
      // byte-for-byte-correct output -- confirmed via a `script`-
      // captured raw session). Same root cause as renderLine()'s own
      // 2026-08-13 fix just above in this file (see its own long
      // comment): writing a Standout-marked character to the
      // terminal's true LAST column triggers auto-wrap, silently
      // moving the real cursor to a row that doesn't exist (this is
      // ROW 24, the very bottom of the screen, deliberately OUTSIDE
      // the DECSTBM scroll region set to rows 1-23) -- real terminals
      // handle "wrap past the bottom-right corner, outside the scroll
      // region" inconsistently, and Terminal.app's own handling here
      // apparently defers repainting the row until some LATER,
      // unrelated write nudges it (matching the exact symptom: fixed
      // the instant ANY further key -- even one that redraws nothing
      // else -- was pressed). DirectoryMode's own "(ls)" trailing field
      // already uses this same fillTrailingToEdge path and hits the
      // identical last-column write, but is masked by its own
      // rightWatermark ("usilic v1.5b") being drawn immediately after,
      // which repositions the cursor via its own moveCursor() call
      // regardless of where auto-wrap silently left it -- coincidental
      // cover, not a real fix, which is exactly why only the
      // watermark-less RecentFile/RecentDirectory case ever surfaced
      // this. Leaving the true last column unfilled is the same
      // pragmatic tradeoff renderLine() already made deliberately (see
      // its own comment) rather than one this function invents fresh.
      int remaining = (cols - 1) - drawnLen - static_cast<int>(trailing.size());
      if (remaining > 0) trailing += std::string(remaining, ' ');
    }
    // See ModeLineFields::trailingPlain's own comment. 2026-08-12 fix:
    // this used to putChar() every byte completely raw, including
    // literal control bytes embedded in a candidate pattern (e.g.
    // usilicrc's own "^  [0-9]\x08[0-9]+\." GREP pattern, ^H = 0x08) --
    // a real terminal receiving a raw 0x08 actually MOVES THE CURSOR
    // BACK one column instead of printing anything, so the bytes that
    // followed silently overwrote what was already there (a real
    // report/screenshot showed "[0-9]\x08[0-9]+." rendering as the
    // garbled "[0-9[0-9]+.", the "]" clobbered by the backspace's own
    // cursor movement plus the next "[" overwriting on top of it).
    // Fixed the same way renderLine() (this same class, just above)
    // already handles control bytes in ordinary CONTENT rows --
    // reverse-video letter+'@' -- for consistency within this port
    // (legacy's own reference rendering uses a different, literal
    // "^"+letter two-character convention here, but this port has
    // already deliberately diverged to the single-reversed-glyph
    // convention everywhere else a control byte can appear -- LineEditor::
    // renderVisible(), renderLine() itself -- so matching THOSE, not
    // legacy verbatim, keeps this one candidate-preview text
    // rendering identically wherever it's shown instead of picking up
    // a third, one-off convention).
    if (fields.trailingPlain) {
      for (unsigned char c : trailing) {
        if (c == '\t') {
          display_.putChar(' ');
          continue;
        }
        bool isControl = c && (c < ' ' || c == 0x7f);
        if (isControl) {
          int shown = (c == 0x7f) ? c - 0x80 : c;
          display_.setAttribute(Display::Attribute::Standout);
          display_.putChar(shown + '@');
          display_.setAttribute(Display::Attribute::StandoutOff);
        } else {
          display_.putChar(c);
        }
      }
    } else {
      drawSegment(trailing);
    }
    drawnLen += static_cast<int>(trailing.size());
    // See setCursorAtModeLineTrailingEnd()'s own comment: cached here
    // (not recomputed by syncVisibleCursor() itself) so it always
    // matches EXACTLY what was actually drawn -- trailing only ends up
    // padded to the row's own edge above when trailing2 is empty (see
    // the fillTrailingToEdge check just above), which never applies to
    // the ^J^S/^J^C candidate list this exists for (it always has a
    // non-empty trailing2, the destination directory), so drawnLen
    // here is genuinely "right after the candidate's own text", not
    // "the row's full width".
    lastModeLineTrailingEndColumn_ = drawnLen;
  }
  if (fit.showTrailing2 && !fields.trailing2.empty()) {
    std::string trailing2 = fields.trailing2;
    // trailing2RightJustified (2026-08-12 fix): see its own comment.
    // Moves the cursor straight to the edge-aligned start column
    // instead of drawing sequentially right after `trailing`'s own
    // gap -- the candidate-preview text's own length varies from
    // search to search, so a purely-sequential trailing2 (this port's
    // ORIGINAL, still-default behavior for every OTHER provider) drifts
    // left and right with it instead of staying pinned to the edge.
    if (fields.trailing2RightJustified) {
      display_.moveCursor(modeLineRow, cols - static_cast<int>(trailing2.size()));
      drawSegment(trailing2);
      drawnLen = cols;
    } else {
      display_.putChar(' ');
      drawnLen += 1;
      // 2026-08-19 fix, same shape as trailing's own "only pad if
      // trailing2 is ALSO empty" gate just above: trailing3 (see its
      // own comment) can now exist as a genuine field AFTER trailing2,
      // so edge-padding here defers to IT when present -- otherwise
      // trailing2 (e.g. ArchiveMode's own "(tar)" badge) silently ate
      // the ENTIRE rest of the row, leaving no room for trailing3 (the
      // path) to ever be drawn at all (a real regression caught by this
      // file's own doctest: modeLineText() still reported the path via
      // composeModeLineRow()'s unconditional append, but the real
      // renderModeLine() row never showed it).
      if (fields.fillTrailingToEdge && fields.trailing3.empty()) {
        int remaining = cols - drawnLen - static_cast<int>(trailing2.size());
        if (remaining > 0) trailing2 += std::string(remaining, ' ');
      }
      drawSegment(trailing2);
      drawnLen += static_cast<int>(trailing2.size());
    }
  }
  // See ModeLineFields::trailing3's own comment. No right-justify/
  // truncation options (unlike trailing/trailing2 above) -- just "show
  // it after its own plain gap if there's room, otherwise drop it".
  // fillTrailingToEdge IS honored here though (2026-08-19 follow-up, a
  // real report =dj: "戻り値が右端まで埋まってない" -- trailing3 is now
  // the actual LAST field drawn for ArchiveMode's own listing, same
  // "pad the row to the edge" treatment trailing/trailing2 already give
  // whichever of THEM is last when trailing3 is empty; same cols-1 cap
  // as those two, for the identical auto-wrap-past-the-true-last-column
  // reason documented on trailing's own fillTrailingToEdge block above).
  if (!fields.trailing3.empty()) {
    std::string trailing3 = fields.trailing3;
    int candidateLen = 1 + static_cast<int>(trailing3.size());
    if (cols > 0 && drawnLen + candidateLen <= cols) {
      display_.putChar(' ');
      drawnLen += 1;
      if (fields.fillTrailingToEdge) {
        int remaining = (cols - 1) - drawnLen - static_cast<int>(trailing3.size());
        if (remaining > 0) trailing3 += std::string(remaining, ' ');
      }
      drawSegment(trailing3);
      drawnLen += static_cast<int>(trailing3.size());
    }
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

// See ModeLineFields::splitLayoutWhenCrowded's own comment for why this
// exists and when renderModeLine() reaches for it instead of the usual
// fitModeLineFields()/withTruncatedFields() tiers. Direct user proposal
// (report =dh follow-up): "『「1/1 END」「111」』をまずは中央へ→残りの
// エリア半分ずつを「モード名」「戻り値」で分け合った上でそれぞれ...".
// `positionText`+`trailing` are centered as a pair (own plain gap
// between the two, same convention as every other field pair in
// renderModeLine()); name is drawn from column 0 and trailing2 from the
// right edge, each independently Finder-style middle-ellipsis truncated
// (middleEllipsisTruncate(), see its own comment) to whatever width is
// actually left once the centered pair claims its own space -- unlike
// fitModeLineFields()'s all-or-nothing drop, this guarantees SOME of
// both name and trailing2 stays visible instead of one vanishing
// entirely, and never drops `trailing` (the byte count) at all, which
// is exactly what the old tiers did whenever name+trailing2 together
// didn't fit (the report this exists for).
void ContentView::renderSplitModeLine(const ModeLineFields &fields, const std::string &positionText) {
  int modeLineRow = modeLineRow_.value_or(row0_ + height_);
  int cols = display_.columns();
  if (cols <= 0) return;

  auto drawSegment = [&](const std::string &text) {
    display_.setAttribute(Display::Attribute::Standout);
    for (unsigned char c : text) display_.putChar(c);
    display_.setAttribute(Display::Attribute::StandoutOff);
  };

  std::string center = positionText;
  if (!fields.trailing.empty()) center += " " + fields.trailing;
  int centerLen = static_cast<int>(center.size());
  // True horizontal centering (not just "cols/2 either side") so the
  // centered pair actually LOOKS centered regardless of how the two
  // side budgets end up splitting -- matches "まずは中央へ" literally.
  int centerStart = std::max(0, (cols - centerLen) / 2);

  int leftBudget = std::max(0, centerStart - 1);      // -1: the plain gap before the center block
  int rightStart = centerStart + centerLen + 1;        // +1: the plain gap after the center block
  int rightBudget = std::max(0, cols - rightStart);

  std::string name = middleEllipsisTruncate(fields.name, leftBudget);
  std::string trailing2 = middleEllipsisTruncate(fields.trailing2, rightBudget);

  if (!name.empty()) {
    display_.moveCursor(modeLineRow, 0);
    drawSegment(name);
  }
  display_.moveCursor(modeLineRow, centerStart);
  drawSegment(positionText);
  if (!fields.trailing.empty()) {
    display_.putChar(' ');
    drawSegment(fields.trailing);
  }
  if (!trailing2.empty()) {
    int trailing2Start = std::max(0, cols - static_cast<int>(trailing2.size()));
    display_.moveCursor(modeLineRow, trailing2Start);
    drawSegment(trailing2);
  }
}

void ContentView::redraw() {
  clampViewport();
  int highlighted = hoverIndex_.has_value() ? *hoverIndex_ : cursorIndex_;
  int rowOffset = bottomAnchorRowOffset();
  bool renderedAny = false;
  for (int row = 0; row < height_; row++) {
    display_.moveCursor(row0_ + row, 0);
    display_.clearToEndOfLine();
    if (row < rowOffset) continue;  // blank padding above the content, bottom-anchored only
    int idx = topIndex_ + (row - rowOffset);
    if (idx < static_cast<int>(lines_.size())) {
      renderLine(lines_[idx], idx == highlighted);
      renderedAny = true;
    }
  }
  // 2026-08-05 fix: with zero lines the loop above never calls
  // renderLine() at all (idx < lines_.size() is never true) -- a real
  // user report found this left an EMPTY highlighted list (e.g. ^R with
  // no history yet) with no on-screen indication of "you are here"
  // whatsoever, compounded by the terminal's own real block cursor
  // wandering off to wherever the mode-line was last drawn (see
  // syncVisibleCursor()'s own comment). This placeholder row stands in
  // for "the (nonexistent) current entry", at the same row a first real
  // entry would occupy -- whether it's ALSO fully reverse-video like a
  // real entry depends on emptyPlaceholderMarked_ (default true; see
  // setEmptyPlaceholder()'s own comment for why grepView_ turns this
  // off).
  if (!renderedAny && cursorHighlightEnabled_ && height_ > rowOffset) {
    display_.moveCursor(row0_ + rowOffset, 0);
    renderLine(emptyPlaceholderLine_, emptyPlaceholderMarked_);
  }
  renderModeLine();
  if (cursorHighlightEnabled_) syncVisibleCursor();
  display_.flush();
}

}  // namespace fm
