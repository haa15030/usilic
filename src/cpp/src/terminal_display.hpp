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
  // tc_load()/get_winsize() (tc.c/vt.c). Exits the process on failure
  // (no TERM set, or a terminfo entry missing cursor_address), matching
  // legacy behavior exactly -- both were already fatal startup errors
  // there.
  static std::unique_ptr<TerminalDisplay> fromEnvironment();

  void write(const std::string &text) override;
  void putChar(int c) override;
  void flush() override;

  void moveCursor(int row, int col) override;
  void cursorLeft() override;
  void saveCursor() override;
  void restoreCursor() override;

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

  // See Display::renderScrolled()'s own comment: a real, row-granular
  // implementation (pixelOffsetY has no effect beyond its sign, since
  // a terminal has no sub-row addressing), kept correct and usable
  // even though the real-terminal drag-scroll fallback in main.cpp
  // currently drives ContentView::redraw() directly instead of
  // calling this.
  void renderScrolled(const std::vector<std::string> &lines, int topIndex, int pixelOffsetY,
                       const std::string &modeLineText) override;

  // Row/column arguments for the change-scroll-region capability,
  // given the current size and smooth-scroll setting. Pure (no I/O).
  // Not part of Display's abstract interface -- exposed publicly here
  // mainly so it can be unit-tested directly without needing a real
  // terminal. Ported from the row arithmetic legacy callers repeated
  // at every vt_cs() call site (see display.hpp).
  std::pair<int, int> scrollRegionArgs() const;

 private:
  void applyScrollRegion();

  TerminalCapabilities caps_;
  int rows_, columns_;
  bool outputEnabled_ = false;
  bool smoothScroll_ = false;
};

}  // namespace fm
