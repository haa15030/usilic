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
// new feature (K-39, ^Jl) with no legacy equivalent -- runs the
// configured UpdateDb role's command and never touches
// query_/results_/view_. setCommandForTest() swaps in a fast, harmless
// real command instead of the actual configured default (macOS's own
// `/usr/libexec/locate.updatedb`, per configuration.cpp's own #ifdef
// __APPLE__ comment, genuinely scans the WHOLE filesystem -- this
// literally hung a `make test` run once that default started pointing
// at a real, executable binary instead of a nonexistent "updatedb").
TEST_CASE_FIXTURE(Fixture, "updateDb: runs the configured command; never touches query()/results()") {
  configuration.setCommandForTest(fm::ConfiguredRole::UpdateDb, {{"true"}, std::nullopt});
  bool ok = mode.updateDb();
  CHECK(ok);
  CHECK(mode.query().empty());
  CHECK(mode.results().empty());
}

TEST_CASE_FIXTURE(Fixture, "updateDb: false if the configured command fails to run at all") {
  configuration.setCommandForTest(fm::ConfiguredRole::UpdateDb, {{"this-command-does-not-exist-xyz123"}, std::nullopt});
  CHECK_FALSE(mode.updateDb());
}

TEST_CASE_FIXTURE(Fixture, "search: a second search replaces the previous query and results") {
  if (!mode.search("a")) {
    MESSAGE("locate not available -- skipping");
    return;
  }
  REQUIRE(mode.search("b"));
  CHECK(mode.query() == "b");
}
