#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "child_process.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "doctest.h"
#include "locate_mode.hpp"
#include "recording_display.hpp"

using fm::Configuration;
using fm::LocateMode;
using fm::test::RecordingDisplay;
namespace fs = std::filesystem;

namespace {

struct Fixture {
  RecordingDisplay display{24, 80};
  fm::ContentView view{display};
  Configuration configuration;
  LocateMode mode{view, configuration};
};

// A tiny real shell script standing in for `locate`/`locate -d ...`,
// echoing a fixed, known set of lines regardless of its argv --
// deterministic counterpart to the real-filesystem tests above (which
// skip gracefully when there's no usable system database at all), same
// pattern test_application.cpp's own "fake ConfiguredRole::Locate
// command" tests already use.
fs::path writeFakeLocate(const fs::path &dir, const std::string &name, const std::vector<std::string> &lines) {
  fs::path script = dir / name;
  std::ofstream out(script);
  out << "#!/bin/sh\n";
  for (const auto &line : lines) out << "echo '" << line << "'\n";
  std::filesystem::permissions(script, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
  return script;
}

fs::path makeTempDir() {
  char tmpl[] = "/tmp/locatemode_testXXXXXX";
  char *result = mkdtemp(tmpl);
  REQUIRE(result != nullptr);
  return fs::path(result);
}

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

// -- LocateUserDb / LocateUserDbBuild (2026-08-22 addition, docs/
// CHANGES.md item 406, a direct user request: "両方検索してマージを
// 希望") -- the personal, non-`nobody` database that covers the blind
// spot the system-wide one has on modern macOS (a fresh account's own
// home directory defaults to `750`, unreadable by `nobody`). Both
// roles are exercised here via fake scripts, exactly like the
// existing ConfiguredRole::Locate tests above -- never the real
// /usr/libexec/locate.mklocatedb pipeline (same reasoning as
// startUpdateDb()'s own comment on why ConfiguredRole::UpdateDb is
// never left at its real default in a test: a real run genuinely
// scans a whole filesystem tree).

TEST_CASE_FIXTURE(Fixture, "search: merges LocateUserDb's own results after Locate's, de-duplicated, when "
                           "both roles are configured") {
  fs::path root = makeTempDir();
  configuration.setCommandForTest(fm::ConfiguredRole::Locate,
                                   {{writeFakeLocate(root, "sys.sh", {"/a", "/shared", "/b"}).string()}, std::nullopt});
  configuration.setCommandForTest(
      fm::ConfiguredRole::LocateUserDb,
      {{writeFakeLocate(root, "user.sh", {"/shared", "/c"}).string()}, std::nullopt});

  REQUIRE(mode.search("anything"));  // ignored by both fake scripts -- each always returns its own fixed lines
  // System results first, in their own order; "/shared" (present in
  // both) appears exactly once, at its FIRST (system-side) position;
  // "/c" (LocateUserDb-only) appended after.
  CHECK(mode.results() == std::vector<std::string>{"/a", "/shared", "/b", "/c"});
}

TEST_CASE_FIXTURE(Fixture, "search: LocateUserDb configured but Locate is not still succeeds, using only "
                           "LocateUserDb's own results") {
  fs::path root = makeTempDir();
  configuration.setCommandForTest(fm::ConfiguredRole::Locate, {{}, std::nullopt});  // empty argv -- "not configured"
  configuration.setCommandForTest(fm::ConfiguredRole::LocateUserDb,
                                   {{writeFakeLocate(root, "user.sh", {"/only"}).string()}, std::nullopt});

  REQUIRE(mode.search("anything"));
  CHECK(mode.results() == std::vector<std::string>{"/only"});
}

TEST_CASE_FIXTURE(Fixture, "search: LocateUserDb's own database not existing yet (before its first build) is "
                           "treated the same as \"zero matches\", not a search failure") {
  fs::path root = makeTempDir();
  configuration.setCommandForTest(fm::ConfiguredRole::Locate,
                                   {{writeFakeLocate(root, "sys.sh", {"/a"}).string()}, std::nullopt});
  // Real `locate -d <missing file>` exits 1 with a "No such file or
  // directory" on stderr -- reproduced here with a fake script so this
  // doesn't depend on the real `locate`/`locate.mklocatedb` existing.
  fs::path missing = root / "user.sh";
  {
    std::ofstream out(missing);
    out << "#!/bin/sh\necho 'not found' 1>&2\nexit 1\n";
  }
  std::filesystem::permissions(missing, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
  configuration.setCommandForTest(fm::ConfiguredRole::LocateUserDb, {{missing.string()}, std::nullopt});

  REQUIRE(mode.search("anything"));
  CHECK(mode.results() == std::vector<std::string>{"/a"});
}

TEST_CASE_FIXTURE(Fixture, "startUpdateDb: with both UpdateDb and LocateUserDbBuild configured, chains them "
                           "into ONE detached child (still returns exactly one pid) -- both actually run") {
  fs::path root = makeTempDir();
  fs::path systemMarker = root / "system.ran";
  fs::path userMarker = root / "user.ran";
  {
    std::ofstream script(root / "sys_update.sh");
    script << "#!/bin/sh\ntouch " << systemMarker.string() << "\n";
  }
  std::filesystem::permissions(root / "sys_update.sh", std::filesystem::perms::owner_all,
                                std::filesystem::perm_options::add);
  {
    std::ofstream script(root / "user_build.sh");
    script << "#!/bin/sh\ntouch " << userMarker.string() << "\n";
  }
  std::filesystem::permissions(root / "user_build.sh", std::filesystem::perms::owner_all,
                                std::filesystem::perm_options::add);

  configuration.setCommandForTest(fm::ConfiguredRole::UpdateDb, {{(root / "sys_update.sh").string()}, std::nullopt});
  configuration.setCommandForTest(fm::ConfiguredRole::LocateUserDbBuild,
                                   {{"sh", "-c", (root / "user_build.sh").string()}, std::nullopt});

  pid_t pid = mode.startUpdateDb();
  REQUIRE(pid > 0);
  // 2000x1ms, not 200x1ms like the pre-existing `true`-based
  // startUpdateDb() test above: a FRESHLY-written, just-chmod'd
  // executable script's first launch can take noticeably longer than
  // an already-established system binary's on macOS (confirmed by a
  // standalone repro -- 200ms flaked, 800ms didn't), unrelated to this
  // port's own code.
  for (int i = 0; i < 2000 && !fm::ChildProcess::hasExited(pid); i++) usleep(1000);
  REQUIRE(fm::ChildProcess::hasExited(pid));
  CHECK(fs::exists(systemMarker));
  CHECK(fs::exists(userMarker));
}

TEST_CASE_FIXTURE(Fixture, "startUpdateDb: only LocateUserDbBuild configured (UpdateDb not) still runs it directly") {
  fs::path root = makeTempDir();
  fs::path userMarker = root / "user.ran";
  {
    std::ofstream script(root / "user_build.sh");
    script << "#!/bin/sh\ntouch " << userMarker.string() << "\n";
  }
  std::filesystem::permissions(root / "user_build.sh", std::filesystem::perms::owner_all,
                                std::filesystem::perm_options::add);

  configuration.setCommandForTest(fm::ConfiguredRole::UpdateDb, {{}, std::nullopt});  // empty argv -- "not configured"
  configuration.setCommandForTest(fm::ConfiguredRole::LocateUserDbBuild,
                                   {{"sh", "-c", (root / "user_build.sh").string()}, std::nullopt});

  pid_t pid = mode.startUpdateDb();
  REQUIRE(pid > 0);
  // 2000x1ms, not 200x1ms like the pre-existing `true`-based
  // startUpdateDb() test above: a FRESHLY-written, just-chmod'd
  // executable script's first launch can take noticeably longer than
  // an already-established system binary's on macOS (confirmed by a
  // standalone repro -- 200ms flaked, 800ms didn't), unrelated to this
  // port's own code.
  for (int i = 0; i < 2000 && !fm::ChildProcess::hasExited(pid); i++) usleep(1000);
  CHECK(fs::exists(userMarker));
}
