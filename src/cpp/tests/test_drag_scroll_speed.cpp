#include "drag_scroll_speed.hpp"
#include "doctest.h"

using fm::applyScrollSpeedOverride;
using fm::kScrollSpeedLevels;
using fm::levelForReleaseDistance;
using fm::scaledForRow;

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

TEST_CASE("scaledForRow: multiplies usleep and busy-loop delays by dotsPerRow") {
  fm::ScrollStep sleepStep{false, 90000, 0};
  auto scaled = scaledForRow(sleepStep, 16);
  CHECK(scaled.busyLoop == false);
  CHECK(scaled.microseconds == 90000 * 16);

  fm::ScrollStep busyStep{true, 0, 30000};
  auto scaledBusy = scaledForRow(busyStep, 16);
  CHECK(scaledBusy.busyLoop == true);
  CHECK(scaledBusy.busyIterations == 30000 * 16);
}

TEST_CASE("scaledForRow: a dotsPerRow of 1 is a no-op, and non-positive values clamp to 1") {
  fm::ScrollStep step{false, 5000, 100};
  CHECK(scaledForRow(step, 1).microseconds == 5000);
  CHECK(scaledForRow(step, 0).microseconds == 5000);
  CHECK(scaledForRow(step, -3).microseconds == 5000);
}

TEST_CASE("kScrollSpeedLevels: level 0 is inert, levels get faster (less delay) as level increases") {
  CHECK(kScrollSpeedLevels[0].microseconds == 0);
  CHECK(kScrollSpeedLevels[0].busyIterations == 0);
  // usleep-based levels (1-4) should each be strictly faster (less delay) than the one before
  for (size_t i = 1; i + 1 < 5; i++) {
    CHECK_FALSE(kScrollSpeedLevels[i].busyLoop);
    CHECK(kScrollSpeedLevels[i].microseconds > kScrollSpeedLevels[i + 1].microseconds);
  }
  // busy-loop levels (5 onward) should have decreasing iteration counts (faster) as level rises
  for (size_t i = 5; i + 1 < kScrollSpeedLevels.size(); i++) {
    CHECK(kScrollSpeedLevels[i].busyLoop);
    CHECK(kScrollSpeedLevels[i].busyIterations >= kScrollSpeedLevels[i + 1].busyIterations);
  }
}

// applyScrollSpeedOverride() mutates the shared global table in place
// (see its own comment on why) -- every case below restores the
// original values afterward so this doesn't leak into other test
// cases/files sharing the same doctest binary.
TEST_CASE("applyScrollSpeedOverride: overrides usleep levels 1..4 in order, leaves busy-loop levels untouched") {
  std::vector<fm::ScrollStep> original = kScrollSpeedLevels;
  applyScrollSpeedOverride({111, 222, 333, 444});
  CHECK(kScrollSpeedLevels[1].microseconds == 111);
  CHECK(kScrollSpeedLevels[2].microseconds == 222);
  CHECK(kScrollSpeedLevels[3].microseconds == 333);
  CHECK(kScrollSpeedLevels[4].microseconds == 444);
  for (size_t i = 5; i < kScrollSpeedLevels.size(); i++) {
    CHECK(kScrollSpeedLevels[i].busyIterations == original[i].busyIterations);
  }
  kScrollSpeedLevels = original;
}

TEST_CASE("applyScrollSpeedOverride: fewer values than usleep levels leaves the trailing ones at default") {
  std::vector<fm::ScrollStep> original = kScrollSpeedLevels;
  applyScrollSpeedOverride({111});
  CHECK(kScrollSpeedLevels[1].microseconds == 111);
  CHECK(kScrollSpeedLevels[2].microseconds == original[2].microseconds);
  CHECK(kScrollSpeedLevels[3].microseconds == original[3].microseconds);
  CHECK(kScrollSpeedLevels[4].microseconds == original[4].microseconds);
  kScrollSpeedLevels = original;
}

TEST_CASE("applyScrollSpeedOverride: values reaching into the default busy-loop range (5+) convert those levels "
          "to usleep-driven too") {
  std::vector<fm::ScrollStep> original = kScrollSpeedLevels;
  REQUIRE(original[5].busyLoop);  // sanity: level 5 defaults to busy-loop
  applyScrollSpeedOverride({90000, 70000, 50000, 30000, 10000});  // 5 values, the user's own real example
  CHECK_FALSE(kScrollSpeedLevels[5].busyLoop);
  CHECK(kScrollSpeedLevels[5].microseconds == 10000);
  CHECK(kScrollSpeedLevels[1].microseconds == 90000);
  CHECK(kScrollSpeedLevels[4].microseconds == 30000);
  // levels beyond the 5 values given stay at their compiled-in default
  for (size_t i = 6; i < kScrollSpeedLevels.size(); i++) {
    CHECK(kScrollSpeedLevels[i].busyLoop == original[i].busyLoop);
    CHECK(kScrollSpeedLevels[i].busyIterations == original[i].busyIterations);
  }
  kScrollSpeedLevels = original;
}

TEST_CASE("applyScrollSpeedOverride: values beyond the table's own last index are ignored, not out-of-bounds") {
  std::vector<fm::ScrollStep> original = kScrollSpeedLevels;
  std::vector<long> tooMany(kScrollSpeedLevels.size() + 5, 12345);  // more values than there are levels at all
  applyScrollSpeedOverride(tooMany);
  CHECK(kScrollSpeedLevels.size() == original.size());  // no growth, no crash
  CHECK(kScrollSpeedLevels[0].microseconds == 0);        // index 0 (the inert placeholder) is never touched
  for (size_t i = 1; i < kScrollSpeedLevels.size(); i++) CHECK(kScrollSpeedLevels[i].microseconds == 12345);
  kScrollSpeedLevels = original;
}

TEST_CASE("applyScrollSpeedOverride: a non-positive value in the middle is skipped, leaving that level at default") {
  std::vector<fm::ScrollStep> original = kScrollSpeedLevels;
  applyScrollSpeedOverride({111, 0, 333});
  CHECK(kScrollSpeedLevels[1].microseconds == 111);
  CHECK(kScrollSpeedLevels[2].microseconds == original[2].microseconds);
  CHECK(kScrollSpeedLevels[3].microseconds == 333);
  kScrollSpeedLevels = original;
}

TEST_CASE("applyScrollSpeedOverride: empty values is a no-op") {
  std::vector<fm::ScrollStep> original = kScrollSpeedLevels;
  applyScrollSpeedOverride({});
  for (size_t i = 0; i < kScrollSpeedLevels.size(); i++) {
    CHECK(kScrollSpeedLevels[i].microseconds == original[i].microseconds);
    CHECK(kScrollSpeedLevels[i].busyIterations == original[i].busyIterations);
  }
}
