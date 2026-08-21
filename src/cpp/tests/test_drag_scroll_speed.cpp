#include "drag_scroll_speed.hpp"
#include "doctest.h"

using fm::applyReleaseDistanceOverride;
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

TEST_CASE("levelForReleaseDistance: the upper (originally busy-loop) tier grows +33% per raw level instead of "
          "doubling, so a hard flick reaches the fastest levels with far less distance than before") {
  int H = 20;  // pixel values below are this function's own actual integer-truncated output for H=20
  // Raw-level thresholds with the compressed upper tier (levels 1-4
  // still double, see this function's own comment): 1:20 2:40 3:80
  // 4:160 5:213 6:284 7:378 8:504 9:672. Output level always adds
  // kReleaseLevelBias(2), clamped to the table's own last index.
  CHECK(levelForReleaseDistance(350, H) == 9);   // 284 <= 350 < 378 -> raw level 7, +2 = 9
  CHECK(levelForReleaseDistance(378, H) == 10);  // 378 <= x < 504 -> raw level 8, +2 = 10 (clamped)
  CHECK(levelForReleaseDistance(672, H) == 10);  // raw level 10 already, +2 still clamps to 10
}

TEST_CASE("levelForReleaseDistance: a real flick is biased 2 levels faster than the raw distance-doubling "
          "table alone would pick, but a too-small movement still doesn't coast at all") {
  int cellHeight = 20;
  // magnitude in [cellHeight, 2*cellHeight) is the raw table's own
  // level-2 bucket (see the class comment's own worked-out ranges) --
  // with the +2 bias this should land on level 4, not 2.
  CHECK(levelForReleaseDistance(cellHeight + 1, cellHeight) == 4);
  // Below cellHeight/4 is still "not a flick at all" -- the bias must
  // never turn a resting/near-zero movement into an active coast.
  CHECK(levelForReleaseDistance(cellHeight / 8, cellHeight) == 0);
  // A huge flick still clamps to the last table entry, not past it.
  CHECK(levelForReleaseDistance(100000, cellHeight) == static_cast<int>(kScrollSpeedLevels.size()) - 1);
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

TEST_CASE("applyScrollSpeedOverride: exactly 10 values (kScrollSpeedLevels.size()-1) tunes every level, "
          "including the fastest") {
  std::vector<fm::ScrollStep> original = kScrollSpeedLevels;
  REQUIRE(kScrollSpeedLevels.size() == 11);  // level 0 (placeholder) + 10 tunable levels
  applyScrollSpeedOverride({100000, 90000, 80000, 70000, 60000, 50000, 40000, 30000, 20000, 10000});
  for (int i = 1; i <= 10; i++) {
    CHECK_FALSE(kScrollSpeedLevels[i].busyLoop);
    CHECK(kScrollSpeedLevels[i].microseconds == 110000 - i * 10000);
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

// applyReleaseDistanceOverride() -- the "-i" CLI flag (2026-08-21, a
// direct user clarification that "-u" only tunes per-level SPEED, not
// which level a given release DISTANCE selects in the first place:
// "-uオプションはマウスホイールの回転角度とスクロール速度との関係。
// それは解決済みだが、今イチなのはマウスドラッグを解除したときの
// カーソル移動幅とスクロール速度との関係"). Mutates the same kind of
// shared global state applyScrollSpeedOverride() does -- every case
// below resets it back to empty (the built-in-formula default)
// afterward for the same reason (not leaking into other test cases).
TEST_CASE("applyReleaseDistanceOverride: explicit thresholds replace the built-in formula entirely, no "
          "kReleaseLevelBias stacked on top") {
  applyReleaseDistanceOverride({20, 40, 80, 160, 320, 480, 640, 800, 960, 1120});
  CHECK(levelForReleaseDistance(10, 20) == 0);    // below the first threshold: not a flick at all
  CHECK(levelForReleaseDistance(20, 20) == 1);    // exactly at threshold 1
  CHECK(levelForReleaseDistance(39, 20) == 1);    // still level 1, just under threshold 2
  CHECK(levelForReleaseDistance(40, 20) == 2);    // exactly at threshold 2
  CHECK(levelForReleaseDistance(1120, 20) == 10);  // exactly at the last threshold
  CHECK(levelForReleaseDistance(5000, 20) == 10);  // far past the last threshold -- still clamps, doesn't grow past it
  applyReleaseDistanceOverride({});
}

TEST_CASE("applyReleaseDistanceOverride: cellHeightPixels is ignored once an override is active -- thresholds "
          "are raw pixels, not cellHeight-relative like the built-in formula") {
  applyReleaseDistanceOverride({100});
  CHECK(levelForReleaseDistance(150, 10) == levelForReleaseDistance(150, 500));
  applyReleaseDistanceOverride({});
}

TEST_CASE("applyReleaseDistanceOverride: fewer thresholds than kScrollSpeedLevels' own tunable count still "
          "clamps sensibly at the highest reachable level") {
  applyReleaseDistanceOverride({50});
  CHECK(levelForReleaseDistance(10, 20) == 0);
  CHECK(levelForReleaseDistance(50, 20) == 1);
  CHECK(levelForReleaseDistance(100000, 20) == 1);  // only one threshold given -- level 1 is as high as it gets
  applyReleaseDistanceOverride({});
}

TEST_CASE("applyReleaseDistanceOverride: empty thresholds falls back to the built-in formula, unaffected") {
  int before = levelForReleaseDistance(350, 20);
  applyReleaseDistanceOverride({100, 200});  // set, then clear -- must not leave any residual effect
  applyReleaseDistanceOverride({});
  CHECK(levelForReleaseDistance(350, 20) == before);
}
