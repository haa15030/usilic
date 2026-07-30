#pragma once

#include <optional>
#include <string>

namespace fm {

struct MouseSgrEvent {
  int column = 0;  // 0-based (the wire format is 1-based)
  int row = 0;      // 0-based
  int button = 0;   // Cb & 0x3: 0=left, 1=middle, 2=right (meaningless when motionOnly and no button held)
  bool motionOnly = false;  // Cb & 0x20: a drag/hover motion report, not a press/release edge
  bool pressed = false;     // true for a trailing 'M' (press, or motion with a button held), false for 'm' (release)
};

// Parses the parameter+terminator portion of an xterm SGR extended
// mouse report (mode \x1b[?1006h, combined with \x1b[?1003h for
// hover-without-a-button-held reports): the caller has already
// consumed the leading "\x1b[<" and hands this everything from there
// up to and including the final 'M'/'m' byte, e.g. "0;45;12M" for a
// left-button press at column 45, row 12 (1-based on the wire;
// returned 0-based to match Display::moveCursor()'s own coordinate
// space). Returns nullopt if `raw` isn't well-formed -- a caller
// peeking at incoming bytes to decide whether they're a mouse report
// at all (see app/main.cpp's peekMouseSgr()) is then responsible for
// treating those bytes as NOT having been one.
//
// New for the 2026-07-30 real-terminal drag-scroll trigger (see
// CLAUDE.md) -- nothing in legacy tcb parses mouse input at all.
std::optional<MouseSgrEvent> parseMouseSgr(const std::string &raw);

}  // namespace fm
