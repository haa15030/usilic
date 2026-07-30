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

}  // namespace fm
