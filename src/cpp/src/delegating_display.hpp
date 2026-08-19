#pragma once

#include <string>
#include <vector>

#include "display.hpp"

namespace fm {

// A Display that does nothing itself except forward every call to
// whichever REAL backend it's currently pointed at -- exists purely
// so ^Og's "seamlessly swap between TerminalDisplay and
// GraphicalDisplay mid-session" (see docs/ideas/IDEAS.md and
// Application's own class comment) doesn't require Application, every
// *Mode class, ContentView, and LineEditor to each be rebuilt against
// a new Display reference: they're all constructed ONCE against a
// single DelegatingDisplay (owned by app/main.cpp) at startup, and
// swapping which real backend is active is just one setBackend() call
// on this one object, invisible to everyone already holding a
// `Display &`.
//
// This is NOT a literal port of anything (no such indirection exists
// or is needed in legacy tcb, which never had more than one Display
// implementation). The alternative -- tearing down and reconstructing
// the entire Application object (and all its owned ContentViews)
// against a new Display every time GUI mode is toggled -- would lose
// or need to painstakingly re-thread every mode's in-memory state
// (the current directory listing, an open file's cursor position, a
// live Shell session's pty, ...) across the swap; this way, none of
// that state ever needs to move at all.
//
// Deliberately excluded: this class has no opinion about WHICH
// backend should be active or when to switch -- that's app/main.cpp's
// job entirely (see its own comment on the ^Og run-loop handling).
class DelegatingDisplay : public Display {
 public:
  // backend must outlive this object, or until a later setBackend()
  // replaces it -- DelegatingDisplay never owns/destroys a backend
  // itself (matching how it doesn't own the initial one either).
  explicit DelegatingDisplay(Display &backend);

  void setBackend(Display &backend);
  Display &backend() const;

  void write(const std::string &text) override;
  void putChar(int c) override;
  void flush() override;

  void moveCursor(int row, int col) override;
  void cursorLeft() override;
  void saveCursor() override;
  void restoreCursor() override;
  void setVisibleCursor(int row, int col) override;
  void hideVisibleCursor() override;

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

  void renderScrolled(const std::vector<std::string> &lines, int topIndex, int pixelOffsetY,
                       const std::vector<ModeLineSegment> &modeLineSegments) override;

 private:
  Display *backend_;
};

}  // namespace fm
