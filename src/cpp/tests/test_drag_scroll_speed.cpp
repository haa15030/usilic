#include "drag_scroll_speed.hpp"
#include "doctest.h"

using fm::kScrollSpeedLevels;
using fm::levelForReleaseDistance;

TEST_CASE("levelForReleaseDistance: a near-zero flick doesn't coast") {
  CHECK(levelForReleaseDistance(0, 20) == 0);
  CHECK(levelForReleaseDistance(2, 20) == 0);  // well under cellHeight/4
}

TEST_CASE("levelForReleaseDistance: magnitude only, sign doesn't matter") {
  CHECK(levelForReleaseDistance(50, 20) == levelForReleaseDistance(-50, 20));
}

TEST_CASE("levelForReleaseDistance: increases monotonically with distance") {
  int cellHeight = 20;
  int previous = 0;
  for (int pixels : {0, 5, 25, 45, 85, 165, 325, 700}) {
    int level = levelForReleaseDistance(pixels, cellHeight);
    CHECK(level >= previous);
    previous = level;
  }
}

TEST_CASE("levelForReleaseDistance: a huge flick tops out at the last table entry") {
  int level = levelForReleaseDistance(100000, 20);
  CHECK(level == static_cast<int>(kScrollSpeedLevels.size()) - 1);
}

TEST_CASE("levelForReleaseDistance: thresholds scale with cellHeightPixels") {
  // The same absolute pixel distance should read as a *faster* level on
  // a small font (few pixels per row) than on a large one, since the
  // mapping is expressed in cellHeight units, not raw pixels.
  CHECK(levelForReleaseDistance(100, 10) > levelForReleaseDistance(100, 200));
}

TEST_CASE("kScrollSpeedLevels: level 0 is inert, levels get faster (less delay) as level increases") {
  CHECK(kScrollSpeedLevels[0].microseconds == 0);
  CHECK(kScrollSpeedLevels[0].busyIterations == 0);
  // usleep-based levels (1,2) should be slower (larger delay) than the level before them
  CHECK(kScrollSpeedLevels[1].microseconds > kScrollSpeedLevels[2].microseconds);
  // busy-loop levels should have decreasing iteration counts (faster) as level rises
  for (size_t i = 3; i + 1 < kScrollSpeedLevels.size(); i++) {
    CHECK(kScrollSpeedLevels[i].busyIterations >= kScrollSpeedLevels[i + 1].busyIterations);
  }
}
