#include "terminal_display.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <curses.h>
#ifdef __CYGWIN32__
#include <ncurses/term.h>
#else
#include <term.h>
#endif
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

// <term.h> #defines `columns` and `lines` as macros (CUR Numbers[0]/[2],
// terminfo's own numeric-capability accessors) -- textually corrupts
// any later use of those identifiers as ordinary variable/parameter/
// method names in this file (this is why the legacy tc.c named its
// analogous accessors tc_row()/tc_col() rather than anything spelled
// "rows"/"columns"). Undefine both immediately after the term.h
// include so the rest of this file -- including the Display::columns()
// override -- can use those names normally.
#undef columns
#undef lines

namespace fm {

namespace {

// tputs()'s per-character output callback has to be a plain function
// pointer (no captures), so the "which display is this for, and is
// its output currently enabled" state has to live outside the
// callback itself. This mirrors the legacy tc.c exactly: it had the
// same constraint and solved it the same way, with a single global
// `vt` pointer checked via vt_mode('D') inside outc(). usilic has
// only ever run as a single foreground display at a time, so this
// carries no new limitation.
TerminalDisplay *g_active = nullptr;

int outc(int c) {
  if (!g_active || !g_active->outputEnabled()) return 0;
  std::putchar(c);
  return c;
}

int windowSize(int *rowsp, int *colsp) {
  struct winsize ws;
  ws.ws_row = 0;
  int fd = open("/dev/tty", O_RDONLY);
  if (fd < 0 || ioctl(fd, TIOCGWINSZ, &ws) < 0) {
    // Matches legacy get_winsize()'s fallback exactly (including the
    // row/column values looking swapped -- this was investigated and
    // confirmed harmless in practice: every real terminal sets
    // TIOCSWINSZ at startup, so this path is essentially untaken. See
    // CLAUDE.md's UTF-8 Step 3.5 history for the prior investigation).
    *rowsp = 80;
    *colsp = 30;
  } else if (!ws.ws_row) {
    *rowsp = 80;
    *colsp = 30;
  } else {
    *rowsp = ws.ws_row;
    *colsp = ws.ws_col;
  }
  if (fd >= 0) ::close(fd);
  return 0;
}

std::string capOrEmpty(char *cap) { return cap ? std::string(cap) : std::string(); }

}  // namespace

TerminalDisplay::TerminalDisplay(TerminalCapabilities caps, int rows, int columns)
    : caps_(std::move(caps)), rows_(rows), columns_(columns) {
  g_active = this;
}

std::unique_ptr<TerminalDisplay> TerminalDisplay::fromEnvironment() {
  const char *term = std::getenv("TERM");
  if (!term) {
    std::fprintf(stderr, "usilic: TERM environment variable is not set.\n");
    std::exit(1);
  }
  // Ported from tc_load()'s TERM overrides.
  if (!std::strcmp(term, "screen")) setenv("TERM", "vt100", 1);
  else if (!std::strcmp(term, "cygwin")) setenv("TERM", "kterm", 1);

  int state = 0;
  setupterm(nullptr, 1, &state);
  if (state != 1) {
    std::fprintf(stderr, "usilic: Invalid terminal.\n");
    std::exit(1);
  }

  TerminalCapabilities caps;
  caps.clearScreen = capOrEmpty(clear_screen);
  caps.clearToEndOfLine = capOrEmpty(clr_eol);
  caps.cursorAddress = capOrEmpty(cursor_address);
  caps.cursorLeft = capOrEmpty(cursor_left);
  caps.standoutOn = capOrEmpty(enter_standout_mode);
  caps.standoutOff = capOrEmpty(exit_standout_mode);
  caps.underlineOn = capOrEmpty(enter_underline_mode);
  caps.underlineOff = capOrEmpty(exit_underline_mode);
  caps.boldOn = capOrEmpty(enter_bold_mode);
  caps.boldOff = capOrEmpty(exit_attribute_mode);
  caps.saveCursor = capOrEmpty(save_cursor);
  caps.restoreCursor = capOrEmpty(restore_cursor);
  caps.changeScrollRegion = capOrEmpty(change_scroll_region);
  caps.scrollForward = capOrEmpty(scroll_forward);
  caps.scrollReverse = capOrEmpty(scroll_reverse);
  caps.enterAltScreen = capOrEmpty(enter_ca_mode);
  caps.exitAltScreen = capOrEmpty(exit_ca_mode);
  caps.keyUp = capOrEmpty(key_up);
  caps.keyDown = capOrEmpty(key_down);
  caps.keyLeft = capOrEmpty(key_left);
  caps.keyRight = capOrEmpty(key_right);
  caps.keyPageUp = capOrEmpty(key_ppage);
  caps.keyPageDown = capOrEmpty(key_npage);

  if (caps.cursorAddress.empty()) {
    std::fprintf(stderr, "usilic: Invalid terminal.\n");
    std::exit(1);
  }

  int rows, columns;
  windowSize(&rows, &columns);
  if (!std::getenv("LINES") || !std::getenv("COLUMNS")) {
    setenv("LINES", std::to_string(rows).c_str(), 1);
    setenv("COLUMNS", std::to_string(columns).c_str(), 1);
  }

  return std::make_unique<TerminalDisplay>(std::move(caps), rows, columns);
}

void TerminalDisplay::write(const std::string &text) {
  if (!outputEnabled_) return;
  std::fputs(text.c_str(), stdout);
}

void TerminalDisplay::putChar(int c) {
  // Ctrl-G (BEL) always rings, even while output is otherwise
  // suppressed -- ported from tc_putchar()'s K_g special case.
  if (c == 7) {
    std::putchar(c);
    std::fflush(stdout);
    return;
  }
  if (!outputEnabled_) return;
  std::putchar(c);
}

void TerminalDisplay::flush() {
  if (!outputEnabled_) return;
  std::fflush(stdout);
}

void TerminalDisplay::moveCursor(int row, int col) {
  if (!outputEnabled_) return;
  tputs(tgoto(caps_.cursorAddress.c_str(), col, row), 1, outc);
  std::fflush(stdout);
}

void TerminalDisplay::setVisibleCursor(int row, int col) { moveCursor(row, col); }

void TerminalDisplay::hideVisibleCursor() {}

void TerminalDisplay::cursorLeft() {
  if (!outputEnabled_) return;
  tputs(caps_.cursorLeft.c_str(), 1, outc);
  std::fflush(stdout);
}

void TerminalDisplay::saveCursor() {
  if (!outputEnabled_ || caps_.saveCursor.empty()) return;
  tputs(caps_.saveCursor.c_str(), 1, outc);
  std::fflush(stdout);
}

void TerminalDisplay::restoreCursor() {
  if (!outputEnabled_ || caps_.restoreCursor.empty()) return;
  tputs(caps_.restoreCursor.c_str(), 1, outc);
  std::fflush(stdout);
}

void TerminalDisplay::clearScreen() {
  if (!outputEnabled_) return;
  tputs(caps_.clearScreen.c_str(), rows_, outc);
  std::fflush(stdout);
}

void TerminalDisplay::clearToEndOfLine() {
  if (!outputEnabled_) return;
  tputs(caps_.clearToEndOfLine.c_str(), 1, outc);
  std::fflush(stdout);
}

void TerminalDisplay::scrollForward() {
  // Legacy tc_sf()/tc_sr() have no explicit outputEnabled early-return
  // (unlike most other primitives) -- they relied solely on outc()'s
  // own per-character check. This class guards explicitly instead (see
  // display.hpp's note on outputEnabled()): tightens that
  // inconsistency without changing observable behavior, since outc()
  // would have suppressed the same bytes either way.
  if (!outputEnabled_) return;
  tputs(caps_.scrollForward.c_str(), 1, outc);
}

void TerminalDisplay::scrollReverse() {
  if (!outputEnabled_) return;
  tputs(caps_.scrollReverse.c_str(), 1, outc);
}

void TerminalDisplay::setAttribute(Attribute attr) {
  if (!outputEnabled_) return;
  std::string esc = attributeEscape(attr);
  if (esc.empty()) return;
  tputs(esc.c_str(), 1, outc);
  std::fflush(stdout);
}

std::string TerminalDisplay::attributeEscape(Attribute attr) const {
  switch (attr) {
    case Attribute::Underline:
      return !caps_.underlineOn.empty() ? caps_.underlineOn : caps_.standoutOn;
    case Attribute::UnderlineOff:
      return !caps_.underlineOff.empty() ? caps_.underlineOff : caps_.standoutOff;
    case Attribute::Bold:
      return caps_.boldOn;
    case Attribute::BoldOff:
      return caps_.boldOff;
    case Attribute::Standout:
      return caps_.standoutOn;
    case Attribute::StandoutOff:
      return caps_.standoutOff;
  }
  return "";
}

int TerminalDisplay::rows() const { return rows_; }
int TerminalDisplay::columns() const { return columns_; }

void TerminalDisplay::setSize(int rows, int columns) {
  rows_ = rows;
  columns_ = columns;
  applyScrollRegion();
}

std::string TerminalDisplay::keySequence(Key key) const {
  switch (key) {
    case Key::Up: return caps_.keyUp;
    case Key::Down: return caps_.keyDown;
    case Key::Left: return caps_.keyLeft;
    case Key::Right: return caps_.keyRight;
    case Key::PageUp: return caps_.keyPageUp;
    case Key::PageDown: return caps_.keyPageDown;
  }
  return "";
}

bool TerminalDisplay::outputEnabled() const { return outputEnabled_; }

void TerminalDisplay::setOutputEnabled(bool enabled) { outputEnabled_ = enabled; }

bool TerminalDisplay::smoothScrollEnabled() const { return smoothScroll_; }

void TerminalDisplay::setSmoothScrollEnabled(bool enabled) {
  smoothScroll_ = enabled;
  applyScrollRegion();
}

std::pair<int, int> TerminalDisplay::scrollRegionArgs() const {
  return {rows_ - (smoothScroll_ ? 1 : 2), 0};
}

void TerminalDisplay::applyScrollRegion() {
  if (!outputEnabled_ || caps_.changeScrollRegion.empty()) return;
  auto [m, n] = scrollRegionArgs();
  tputs(tgoto(caps_.changeScrollRegion.c_str(), m, n), 1, outc);
  std::fflush(stdout);
}

void TerminalDisplay::open() {
  if (!outputEnabled_) return;
  tputs(caps_.enterAltScreen.c_str(), 1, outc);
  std::fflush(stdout);
  applyScrollRegion();
}

void TerminalDisplay::close() {
  // Legacy tc.c captured exit_ca_mode (te) into Tc.te but never once
  // called it -- usilic always left the terminal's alternate screen
  // active on exit. This is a deliberate correction, not a straight
  // port: a Display that opens the alternate screen should also be
  // the one to leave it.
  if (!outputEnabled_ || caps_.exitAltScreen.empty()) return;
  tputs(caps_.exitAltScreen.c_str(), 1, outc);
  std::fflush(stdout);
}

// See Display::leaveForExternalProgram()'s own comment for the full
// story (docs/HISTORY.md:9023, ported from legacy vt.c's vt_init(-2)
// case): applyScrollRegion() constrains the scroll region to
// rows_-2 (or rows_-1 under smooth scroll) for usilic's OWN drawing,
// reserving the last row for the mode line -- an external program
// (`less`, a PROGRAM-list entry) inherits that same real terminal and
// has no idea usilic ever narrowed it, so it needs the FULL screen
// back for the duration of its own run.
void TerminalDisplay::leaveForExternalProgram() {
  if (!outputEnabled_ || caps_.changeScrollRegion.empty()) return;
  tputs(tgoto(caps_.changeScrollRegion.c_str(), rows_ - 1, 0), 1, outc);
  std::fflush(stdout);
}

// The other half of the pair above: re-constrains the scroll region
// back to usilic's own normal (smooth-scroll-dependent) range once
// control returns -- just applyScrollRegion() itself, already
// computing exactly that from smoothScroll_/rows_.
void TerminalDisplay::reenterFromExternalProgram() { applyScrollRegion(); }

// See ContentView::renderLine()'s own identical byte-range logic
// (col < run.start || col >= run.end) -- this class's own convention
// (byte offset == screen column, see ContentView's own class comment)
// applies here too. Writes byte-by-byte (rather than one write() call
// for the whole line) so Bold/Underline can toggle exactly at each
// run's own boundary, same shape modeLineSegments' own Standout
// toggling below already uses for its own (single, whole-segment)
// runs.
void TerminalDisplay::writeAttributedLine(const std::string &text, const std::vector<Display::AttributeRun> &runs) {
  bool boldOn = false, underlineOn = false;
  for (std::size_t col = 0; col < text.size(); col++) {
    bool wantBold = false, wantUnderline = false;
    for (const auto &run : runs) {
      if (static_cast<int>(col) < run.start || static_cast<int>(col) >= run.end) continue;
      if (run.attribute == Attribute::Bold) wantBold = true;
      else if (run.attribute == Attribute::Underline) wantUnderline = true;
    }
    if (wantBold != boldOn) {
      setAttribute(wantBold ? Attribute::Bold : Attribute::BoldOff);
      boldOn = wantBold;
    }
    if (wantUnderline != underlineOn) {
      setAttribute(wantUnderline ? Attribute::Underline : Attribute::UnderlineOff);
      underlineOn = wantUnderline;
    }
    putChar(static_cast<unsigned char>(text[col]));
  }
  if (boldOn) setAttribute(Attribute::BoldOff);
  if (underlineOn) setAttribute(Attribute::UnderlineOff);
}

void TerminalDisplay::renderScrolled(const std::vector<ScrolledLine> &lines, int topIndex, int pixelOffsetY,
                                      const std::vector<ModeLineSegment> &modeLineSegments) {
  (void)pixelOffsetY;  // no sub-row addressing on a real terminal -- see this method's own header comment
  if (!outputEnabled_) return;
  int contentRows = std::max(rows_ - 1, 0);
  for (int row = 0; row < contentRows; row++) {
    int lineIndex = topIndex + row;
    moveCursor(row, 0);
    clearToEndOfLine();
    if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) {
      const ScrolledLine &line = lines[lineIndex];
      if (line.attributes.empty()) write(line.text);
      else writeAttributedLine(line.text, line.attributes);
    }
  }
  if (rows_ > 0) {
    // Per-segment Standout runs, same as ContentView::renderModeLine()
    // itself -- see Display::ModeLineSegment's own comment for why (a
    // non-standout segment, the plain gaps between fields, is written
    // with no attribute at all, same as renderModeLine()'s own plain
    // putChar(' ') between drawSegment() calls).
    moveCursor(contentRows, 0);
    clearToEndOfLine();
    for (const auto &segment : modeLineSegments) {
      if (segment.standout) setAttribute(Attribute::Standout);
      write(segment.text);
      if (segment.standout) setAttribute(Attribute::StandoutOff);
    }
  }
  flush();
}

}  // namespace fm
