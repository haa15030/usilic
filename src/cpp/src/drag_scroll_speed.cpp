#include "drag_scroll_speed.hpp"

#include <algorithm>
#include <cstdlib>

namespace fm {

// Widened from 7 usable levels to 10 (2026-07-30) after a real-machine
// report that the curve "changed abruptly three levels up from the
// minimum" -- the old table jumped straight from a precisely-timed
// 35ms usleep() (level 2) to an UNCALIBRATED 400,000-iteration busy
// spin (level 3), with no way to know how those two actually compare
// in wall-clock time on any given machine without running it for
// real. Widening adds two more usleep() steps (still exactly timed,
// no ambiguity) before the busy-loop range starts, and more, more
// evenly-spaced busy-loop steps within it -- but the busy-loop values
// themselves are still hand-picked, not measured against real
// hardware (this whole project's sandbox has no way to do that,
// see CLAUDE.md's repeated notes on the same limitation for GUI
// verification generally). USILICSCROLLDEBUG (see app/main.cpp's
// loadScrollDebugLevelFromEnvironment()) exists specifically so this
// can be tuned further against a real machine, one level at a time,
// without needing an actual flick/repeated keypress fast enough to
// reliably land on the level under test.
std::vector<ScrollStep> kScrollSpeedLevels = {
    {false, 0, 0},          // 0: unused, "not coasting"
    {false, 90000, 0},      // 1: slowest -- usleep, ~11 dots/sec
    {false, 55000, 0},      // 2: usleep
    {false, 30000, 0},      // 3: usleep
    {false, 15000, 0},      // 4: usleep -- last precisely-timed step before the busy-loop range
    {true, 0, 500000},      // 5: busy-loop begins here
    {true, 0, 250000},      // 6
    {true, 0, 120000},      // 7
    {true, 0, 50000},       // 8
    {true, 0, 15000},       // 9
    {true, 0, 0},           // 10: fastest -- no delay between dots at all
};

void applyScrollSpeedOverride(const std::vector<long> &values) {
  size_t vi = 0;
  // Start at index 1: index 0 is the inert "not coasting" placeholder
  // (also !busyLoop, but never an active speed -- see kScrollSpeedLevels'
  // own comment), not a real tunable level.
  for (size_t i = 1; i < kScrollSpeedLevels.size(); i++) {
    ScrollStep &step = kScrollSpeedLevels[i];
    if (step.busyLoop) continue;  // busy-loop levels: not usleep-driven, nothing to override
    if (vi >= values.size()) break;
    if (values[vi] > 0) step.microseconds = values[vi];
    vi++;
  }
}

int levelForReleaseDistance(int pixels, int cellHeightPixels) {
  int height = std::max(cellHeightPixels, 1);
  int magnitude = std::abs(pixels);
  int maxLevel = static_cast<int>(kScrollSpeedLevels.size()) - 1;
  if (magnitude < height / 4) return 0;
  // Same doubling-threshold shape as before, just carried one level
  // further (height*1, *2, *4, ... *64) to reach the wider table --
  // magnitude thresholds, not the per-step delays above, so this part
  // needed no real-hardware calibration to extend correctly.
  int level = 1;
  int threshold = height;
  while (level < maxLevel && magnitude >= threshold) {
    level++;
    threshold *= 2;
  }
  return level;
}

ScrollStep scaledForRow(const ScrollStep &pixelStep, int dotsPerRow) {
  int factor = std::max(dotsPerRow, 1);
  return {pixelStep.busyLoop, pixelStep.microseconds * factor, pixelStep.busyIterations * factor};
}

}  // namespace fm
