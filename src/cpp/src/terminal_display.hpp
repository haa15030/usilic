#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "display.hpp"

namespace fm {

// Capability strings as terminfo reports them for the current $TERM.
// An empty string means "this terminal doesn't support the
// capability" -- callers must already tolerate that (mirrors legacy
// tc.c, which null-checked Tc.sc/Tc.rc/Tc.cs before using them).
// Field names follow the termcap two-letter names tc.c used, in
// comments, so this can be cross-checked against that file directly.
struct TerminalCapabilities {
  std::string clearScreen;                    // cl
  std::string clearToEndOfLine;                // ce
  std::string cursorAddress;                   // cm (required; see fromEnvironment())
  std::string cursorLeft;                       // le
  std::string standoutOn, standoutOff;          // so, se
  std::string underlineOn, underlineOff;        // us, ue (fall back to so/se)
  std::string boldOn, boldOff;                  // md, me
  std::string saveCursor, restoreCursor;        // sc, rc (optional)
  std::string changeScrollRegion;               // cs (optional)
  std::string scrollForward, scrollReverse;     // sf, sr
  std::string enterAltScreen, exitAltScreen;    // ti, te
  std::string keyUp, keyDown, keyLeft, keyRight; // ku, kd, kr, kl
  std::string keyPageUp, keyPageDown;            // kP, kN
};

// termcap/terminfo-backed Display, using the same tputs/tgoto/
// setupterm approach as the legacy tc.c. Ported faithfully; behavior
// differences from the legacy C are called out explicitly in comments
// (there are a few small, deliberate ones -- see close(), and the
// output-enabled guard note on display.hpp).
//
// Testing note: there's no headless way to exercise real terminal I/O
// from a unit test (tputs/setupterm need an actual terminfo database
// and, for meaningful output, a real tty -- the legacy tc.c was never
// unit-tested either, only verified via PTY scripts). To keep the
// parts that CAN be tested honestly separated from the parts that
// can't: every method that performs real I/O guards on
// outputEnabled() as its very first statement, so constructing a
// TerminalDisplay via the injectable constructor below (outputEnabled
// defaults to false) makes every I/O method a guaranteed no-op --
// safe to call from a test without touching stdout or any ncurses
// global state. That lets tests exercise the pure state (attribute/
// key capability lookup, geometry, the enabled/smooth-scroll flags
// themselves) with real confidence, while the actual escape-sequence
// output is build-verified only, same as the legacy code's coverage.
class TerminalDisplay : public Display {
 public:
  TerminalDisplay(TerminalCapabilities caps, int rows, int columns);

  // Real construction path: queries $TERM via setupterm() and the
  // controlling tty's window size via ioctl(TIOCGWINSZ). Ported from
  // tc_load()/get_winsize() (tc.c/vt.c). By default, exits the process
  // on failure (no TERM set, an unrecognized terminal, or a terminfo
  // entry missing cursor_address), matching legacy behavior exactly --
  // all three were already fatal startup errors there, back when this
  // was the ONLY possible display backend.
  //
  // `exitOnFailure = false` (2026-08-24 fix, docs/CHANGES.md item 423,
  // a direct user report: a real double-click on `make app`'s new
  // .app bundle did nothing at all -- confirmed via a temporary
  // stderr-to-file redirect that this exact fprintf+exit(1) was firing
  // on "TERM environment variable is not set", every single time):
  // this class's own legacy-inherited assumption -- "a real terminal
  // MUST exist, or the whole program can't run" -- stopped being true
  // the moment GUI mode (GraphicalDisplay, SDL2-backed) existed, since
  // that backend needs no terminfo/tty of its own at all. A process
  // launched via LaunchServices/Finder (not from any shell) has NO
  // controlling terminal and, critically, no $TERM in its environment
  // either -- unlike this port's own `open`-from-a-shell testing,
  // which happened to inherit the invoking shell's $TERM and never hit
  // this path, masking the bug for a long time. Passed `false` from
  // main() specifically when it already knows (from argv/-c/-g/
  // platform-default logic, decided BEFORE this call) that the session
  // is GUI-only and will never actually render through this backend's
  // real terminal capabilities -- returns a degraded, empty-
  // capabilities placeholder instead of exiting, sized to a
  // conventional 80-column x 24-row terminal (see fromEnvironment()'s
  // own .cpp-side comment on why this is its own value, deliberately
  // NOT reusing windowSize()'s different 80x30 fallback elsewhere in
  // this file). A genuine
  // CUI-only launch (forceCui, or the non-macOS platform default) is
  // completely unaffected -- still defaults to `true`, still exits
  // with the same diagnostic message, since THERE a working real
  // terminal genuinely is required and "silently proceed with no
  // capabilities" would just trade one confusing failure for another.
  static std::unique_ptr<TerminalDisplay> fromEnvironment(bool exitOnFailure = true);

  void write(const std::string &text) override;
  void putChar(int c) override;
  void flush() override;

  void moveCursor(int row, int col) override;
  void cursorLeft() override;
  void saveCursor() override;
  void restoreCursor() override;
  // Just moveCursor(row, col) -- see Display::setVisibleCursor()'s own
  // comment for why a real terminal doesn't need to track this
  // separately.
  void setVisibleCursor(int row, int col) override;
  void hideVisibleCursor() override;  // no-op, see Display's own comment

  void clearScreen() override;
  void clearToEndOfLine() override;
  void scrollForward() override;
  void scrollReverse() override;

  void setAttribute(Attribute attr) override;
  std::string attributeEscape(Attribute attr) const override;

  int rows() const override;
  int columns() const override;
  void setSize(int rows, int columns) override;

  std::string keySequence(Key key) const override;

  bool outputEnabled() const override;
  void setOutputEnabled(bool enabled) override;

  bool smoothScrollEnabled() const override;
  void setSmoothScrollEnabled(bool enabled) override;

  void open() override;
  void close() override;

  void leaveForExternalProgram() override;
  void reenterFromExternalProgram() override;

  // See Display::renderScrolled()'s own comment: a real, row-granular
  // implementation (pixelOffsetY has no effect beyond its sign, since
  // a terminal has no sub-row addressing), kept correct and usable
  // even though the real-terminal drag-scroll fallback in main.cpp
  // currently drives ContentView::redraw() directly instead of
  // calling this.
  void renderScrolled(const std::vector<ScrolledLine> &lines, int topIndex, int pixelOffsetY,
                       const std::vector<Display::ModeLineSegment> &modeLineSegments) override;

  // Row/column arguments for the change-scroll-region capability,
  // given the current size and smooth-scroll setting. Pure (no I/O).
  // Not part of Display's abstract interface -- exposed publicly here
  // mainly so it can be unit-tested directly without needing a real
  // terminal. Ported from the row arithmetic legacy callers repeated
  // at every vt_cs() call site (see display.hpp).
  std::pair<int, int> scrollRegionArgs() const;

 private:
  void applyScrollRegion();
  // See its own .cpp comment.
  void writeAttributedLine(const std::string &text, const std::vector<Display::AttributeRun> &runs);

  TerminalCapabilities caps_;
  int rows_, columns_;
  bool outputEnabled_ = false;
  bool smoothScroll_ = false;
};

}  // namespace fm
