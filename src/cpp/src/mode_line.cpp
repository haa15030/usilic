#include "mode_line.hpp"

#include <algorithm>
#include <cstdio>

namespace fm {

std::string ModeLine::format(int currentLine, int totalLines) {
  std::string total = std::to_string(std::max(totalLines, 0));
  std::string current = std::to_string(std::max(currentLine, 0));
  if (current.size() < total.size()) current.insert(0, total.size() - current.size(), '0');

  // Checked before the TOP case so a single-line list (currentLine ==
  // totalLines == 1) reads END, not TOP -- "you've seen everything"
  // takes priority over "you're at the start" when they're the same
  // line.
  if (currentLine >= totalLines) return current + "/" + total + " END";
  if (currentLine <= 1) return current + "/" + total + " TOP";

  int percent = totalLines > 0 ? (currentLine * 100 / totalLines) : 0;
  char percentBuf[8];
  std::snprintf(percentBuf, sizeof(percentBuf), "%2d%%", percent);
  return current + "/" + total + " " + percentBuf;
}

// Ported from print_line()'s (disp.c) own len<=0 branch -- the ONLY
// caller that ever passes a negative len is disp_shell(), so this
// fixed 6-digit zero-pad ("000002/000002 END") is a SHELL-specific
// legacy convention, distinct from format()'s own "pad current to
// TOTAL's own digit width" default every other mode uses. Same TOP/
// END/% suffix logic as format() (this port's own 2026-07-31 GUI-
// drag-scroll-driven departure from legacy, applied consistently
// here too rather than reverting just this one caller to legacy's own
// plain "END or %, no TOP" shape -- no counter-example against that
// choice was given).
std::string ModeLine::formatFixedWidth(int currentLine, int totalLines) {
  constexpr std::size_t kWidth = 6;
  std::string total = std::to_string(std::max(totalLines, 0));
  std::string current = std::to_string(std::max(currentLine, 0));
  if (total.size() < kWidth) total.insert(0, kWidth - total.size(), '0');
  if (current.size() < kWidth) current.insert(0, kWidth - current.size(), '0');

  if (currentLine >= totalLines) return current + "/" + total + " END";
  if (currentLine <= 1) return current + "/" + total + " TOP";

  int percent = totalLines > 0 ? (currentLine * 100 / totalLines) : 0;
  char percentBuf[8];
  std::snprintf(percentBuf, sizeof(percentBuf), "%2d%%", percent);
  return current + "/" + total + " " + percentBuf;
}

}  // namespace fm
