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
  // Start at index 1: index 0 is the inert "not coasting" placeholder
  // (also !busyLoop, but never an active speed -- see kScrollSpeedLevels'
  // own comment), not a real tunable level. Unlike the first cut of
  // this function, values are assigned to indices 1..values.size()
  // UNCONDITIONALLY -- including levels that default to busy-loop
  // (5+) -- converting each one to usleep-driven as it's overridden
  // (busyLoop set false, that level's own microseconds set from the
  // caller's value). Found necessary after a real user report that
  // "-u 10000 20000 30000" (3 values, only reaching the pre-existing
  // usleep range of levels 1-4) produced no perceptible change: the
  // user specifically wants a 5-level, all-usleep, precisely-timed
  // curve ("usleep最低速から5段上まで"), and the busy-loop levels'
  // whole reason for existing (OS sleep granularity can't reliably
  // hit sub-few-ms delays, see kScrollSpeedLevels' own comment) is a
  // concern only for the DEFAULT table, not a hard rule -- a caller
  // who explicitly asks for a 5th precisely-timed step, knowing the
  // usleep-granularity tradeoff, gets exactly that. Levels beyond
  // values.size() are left at their compiled-in default (busy-loop or
  // not, whichever they already were).
  for (size_t i = 1; i < kScrollSpeedLevels.size() && i - 1 < values.size(); i++) {
    long value = values[i - 1];
    if (value <= 0) continue;  // non-positive: leave this level at its compiled-in default, see header comment
    kScrollSpeedLevels[i].busyLoop = false;
    kScrollSpeedLevels[i].microseconds = value;
  }
}

// See applyReleaseDistanceOverride()'s own header comment. Empty by
// default (built-in formula active); set once at startup by "-i", same
// lifetime/ownership shape as kScrollSpeedLevels itself.
std::vector<long> gReleaseDistanceOverride;

void applyReleaseDistanceOverride(const std::vector<long> &thresholds) { gReleaseDistanceOverride = thresholds; }

int levelForReleaseDistance(int pixels, int cellHeightPixels) {
  int magnitude = std::abs(pixels);
  int maxLevel = static_cast<int>(kScrollSpeedLevels.size()) - 1;
  if (!gReleaseDistanceOverride.empty()) {
    int level = 0;
    for (long threshold : gReleaseDistanceOverride) {
      if (magnitude < threshold) break;
      level++;
    }
    return std::min(level, maxLevel);
  }
  int height = std::max(cellHeightPixels, 1);
  if (magnitude < height / 4) return 0;
  // Levels 1-4 (this table's own originally usleep-calibrated,
  // precisely-timed tier -- see kScrollSpeedLevels' own comment) keep
  // doubling per level (height*1, *2, *4, *8); level 5 onward (the
  // tier that was always an uncalibrated busy-loop) grows more gently
  // -- starting 2026-08-06 after a real user report that even a hard,
  // fast flick's coast still visibly "dropped" speed at release: the
  // raw doubling curve made the fastest raw levels require
  // magnitude*'s of several HUNDRED times cellHeightPixels within just
  // kReleaseWindow (150ms) -- e.g. level 10 needed
  // 256*cellHeightPixels, thousands of real pixels in 150ms, an
  // unreachable release velocity for an actual human hand -- so no
  // realistic flick, however hard, could ever land past roughly the
  // middle of the table. Tuned twice already, both still 2026-08-06:
  // +50% (kUpperTierGrowthDivisor 2) still couldn't reach top speed on
  // a real hard flick; +25% (divisor 4) DID reach top speed but a
  // real-hardware follow-up report found it now felt UNDER-
  // differentiated ("同期感が今ひとつ" -- flick effort no longer felt
  // proportional to resulting speed) -- the same narrow band that
  // makes max speed reachable also makes most any moderately-hard
  // flick land near the top, losing the wide, gradual feel levels 1-4
  // (still doubling) have. +33% (divisor 3) is the midpoint between
  // those two data points: still comfortably reachable (top speed
  // needs magnitude >= 378 at cellHeightPixels=20, vs. divisor 4's
  // 312 and divisor 2's 540) while widening each level's own bucket
  // back out somewhat for more perceptible steps. Levels 1-4's
  // doubling logic is untouched throughout all of this (the same user
  // separately confirmed that part already felt right after
  // kReleaseLevelBias below was added).
  constexpr int kUpperTierGrowthDivisor = 3;
  int level = 1;
  int threshold = height;
  while (level < maxLevel && magnitude >= threshold) {
    level++;
    threshold += (level <= 4) ? threshold : threshold / kUpperTierGrowthDivisor;
  }
  // +kReleaseLevelBias, a flat shift toward faster (2026-08-06, the
  // user's own proposed fix): after two failed attempts at a deeper
  // root-cause diagnosis for "coast speed at release feels too slow"
  // (boosting every level's own per-dot speed via -u didn't help --
  // ruling out "the chosen level is just configured too slow" --
  // and reordering the coast physics loop to remove an initial-dot
  // startup pause didn't help either -- ruling out "there's a dead
  // pause before the first visible dot"), with no way to test real
  // mouse-drag data from this sandbox to dig further, the user
  // proposed simply shifting the DISTANCE-TO-LEVEL mapping itself by
  // a couple of levels toward faster as a pragmatic compensation,
  // regardless of the exact underlying reason typical flicks land on
  // a lower level than they feel like they should. Only applied to
  // an already-coasting result (level 0, "too small to count as a
  // flick at all," is untouched -- deliberately still requires a real
  // flick before this bias ever applies) and clamped to maxLevel so
  // it can never select a level past the end of the table.
  constexpr int kReleaseLevelBias = 2;
  return std::min(level + kReleaseLevelBias, maxLevel);
}

ScrollStep scaledForRow(const ScrollStep &pixelStep, int dotsPerRow) {
  int factor = std::max(dotsPerRow, 1);
  return {pixelStep.busyLoop, pixelStep.microseconds * factor, pixelStep.busyIterations * factor};
}

}  // namespace fm
