#include "drag_scroll_speed.hpp"

#include <algorithm>
#include <cstdlib>

namespace fm {

const std::vector<ScrollStep> kScrollSpeedLevels = {
    {false, 0, 0},          // 0: unused, "not coasting"
    {false, 90000, 0},      // 1: slowest -- usleep, ~11 dots/sec
    {false, 35000, 0},      // 2: usleep, ~28 dots/sec
    {true, 0, 400000},      // 3: busy-loop begins here
    {true, 0, 120000},      // 4
    {true, 0, 30000},       // 5
    {true, 0, 5000},        // 6
    {true, 0, 0},           // 7: fastest -- no delay between dots at all
};

int levelForReleaseDistance(int pixels, int cellHeightPixels) {
  int height = std::max(cellHeightPixels, 1);
  int magnitude = std::abs(pixels);
  if (magnitude < height / 4) return 0;
  if (magnitude < height * 1) return 1;
  if (magnitude < height * 2) return 2;
  if (magnitude < height * 4) return 3;
  if (magnitude < height * 8) return 4;
  if (magnitude < height * 16) return 5;
  if (magnitude < height * 32) return 6;
  return 7;
}

ScrollStep scaledForRow(const ScrollStep &pixelStep, int dotsPerRow) {
  int factor = std::max(dotsPerRow, 1);
  return {pixelStep.busyLoop, pixelStep.microseconds * factor, pixelStep.busyIterations * factor};
}

}  // namespace fm
