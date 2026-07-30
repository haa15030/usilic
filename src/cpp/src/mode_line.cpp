#include "mode_line.hpp"

#include <algorithm>
#include <cstdio>

namespace fm {

std::string ModeLine::format(int currentLine, int totalLines, int bottomVisibleLine) {
  std::string total = std::to_string(std::max(totalLines, 0));
  std::string current = std::to_string(std::max(currentLine, 0));
  if (current.size() < total.size()) current.insert(0, total.size() - current.size(), '0');

  if (bottomVisibleLine >= totalLines) return current + "/" + total + " END";

  int percent = totalLines > 0 ? (bottomVisibleLine * 100 / totalLines) : 0;
  char percentBuf[8];
  std::snprintf(percentBuf, sizeof(percentBuf), "%2d%%", percent);
  return current + "/" + total + " " + percentBuf;
}

}  // namespace fm
