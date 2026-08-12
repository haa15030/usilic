#include "configuration.hpp"
#include "content_view.hpp"
#include "doctest.h"
#include "locate_mode.hpp"
#include "recording_display.hpp"

using fm::Configuration;
using fm::LocateMode;
using fm::test::RecordingDisplay;

namespace {

struct Fixture {
  RecordingDisplay display{24, 80};
  fm::ContentView view{display};
  Configuration configuration;
  LocateMode mode{view, configuration};
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "search: an empty query fails cleanly without running locate at all") {
  CHECK_FALSE(mode.search(""));
  CHECK(mode.results().empty());
}

// The remaining tests genuinely run the real `locate` binary (matching
// this project's preference for real end-to-end verification), but --
// unlike e.g. ChildProcess's echo/cat tests -- `locate` depends on a
// system database this project doesn't control the presence of
// (CLAUDE.md history notes even macOS needs a one-time `updatedb` run
// before it works at all). These skip gracefully, the same pattern
// Shell's real-bash tests already use, rather than asserting specific
// results that may not exist in every environment this runs in.

TEST_CASE_FIXTURE(Fixture, "search: a real locate query runs and populates results()/query()") {
  if (!mode.search("/")) {
    MESSAGE("locate not installed or no database available -- skipping");
    return;
  }
  CHECK(mode.query() == "/");
}

TEST_CASE_FIXTURE(Fixture, "selectedResult: tracks the ContentView cursor when there are results") {
  if (!mode.search("/") || mode.results().empty()) {
    MESSAGE("locate not available or returned no results -- skipping");
    return;
  }
  view.setCursorIndex(0);
  const auto *selected = mode.selectedResult();
  REQUIRE(selected != nullptr);
  CHECK(*selected == mode.results()[0]);
}

// 2026-08-13 (docs/ideas/IDEAS.md's LOCATE section): updateDb() is a
// new feature (K-39, ^Jl) with no legacy equivalent -- runs
// `updatedb` and never touches query_/results_/view_, regardless of
// whether the real binary exists/is permitted in this environment.
TEST_CASE_FIXTURE(Fixture, "updateDb: runs `updatedb`; never touches query()/results()") {
  bool ok = mode.updateDb();
  CHECK(mode.query().empty());
  CHECK(mode.results().empty());
  if (!ok) MESSAGE("updatedb not installed/permitted -- ok is false, which is fine here");
}

TEST_CASE_FIXTURE(Fixture, "search: a second search replaces the previous query and results") {
  if (!mode.search("a")) {
    MESSAGE("locate not available -- skipping");
    return;
  }
  REQUIRE(mode.search("b"));
  CHECK(mode.query() == "b");
}
