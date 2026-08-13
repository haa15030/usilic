#include <unistd.h>

#include "child_process.hpp"
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

// 2026-08-13 (docs/ideas/IDEAS.md's LOCATE section): startUpdateDb()
// is a new feature (K-39, ^Jl) with no legacy equivalent -- runs the
// configured UpdateDb role's command and never touches
// query_/results_/view_. setCommandForTest() swaps in a fast, harmless
// real command instead of the actual configured default (macOS's own
// `/usr/libexec/locate.updatedb`, per configuration.cpp's own #ifdef
// __APPLE__ comment, genuinely scans the WHOLE filesystem -- this
// literally hung a `make test` run once that default started pointing
// at a real, executable binary instead of a nonexistent "updatedb").
// 2026-08-14: renamed from updateDb() and changed to run DETACHED
// (returns the child's pid immediately, doesn't wait for it to exit) --
// a real report ("フリーズ中ではないメッセージなので...常時^Gで...
// 移行してほしい") found a real updatedb run can take MINUTES, during
// which the OLD blocking version left Application::runLocateUpdateDb()
// unable to respond to ^G at all until it finally returned. See
// ChildProcess::runDetached()/hasExited() for the primitive this now
// builds on, and Application::runLocateUpdateDb()'s own comment for
// how the caller polls for completion.
TEST_CASE_FIXTURE(Fixture, "startUpdateDb: launches the configured command and returns its pid; "
                           "never touches query()/results()") {
  configuration.setCommandForTest(fm::ConfiguredRole::UpdateDb, {{"true"}, std::nullopt});
  pid_t pid = mode.startUpdateDb();
  CHECK(pid > 0);
  CHECK(mode.query().empty());
  CHECK(mode.results().empty());
  // Reap it (ChildProcess::hasExited() polls WNOHANG) so this test
  // doesn't leak a zombie -- `true` exits almost immediately.
  for (int i = 0; i < 100 && !fm::ChildProcess::hasExited(pid); i++) usleep(1000);
}

TEST_CASE_FIXTURE(Fixture, "startUpdateDb: -1 if the configured command doesn't exist at all") {
  configuration.setCommandForTest(fm::ConfiguredRole::UpdateDb, {{}, std::nullopt});  // empty argv -- "not configured"
  CHECK(mode.startUpdateDb() == -1);
}

TEST_CASE_FIXTURE(Fixture, "search: a second search replaces the previous query and results") {
  if (!mode.search("a")) {
    MESSAGE("locate not available -- skipping");
    return;
  }
  REQUIRE(mode.search("b"));
  CHECK(mode.query() == "b");
}
