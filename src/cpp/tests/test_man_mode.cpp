#include "configuration.hpp"
#include "content_view.hpp"
#include "doctest.h"
#include "history.hpp"
#include "man_mode.hpp"
#include "recording_display.hpp"

using fm::Charset;
using fm::Configuration;
using fm::History;
using fm::ManMode;
using fm::PositionCategory;
using fm::test::RecordingDisplay;

namespace {

struct Fixture {
  RecordingDisplay display{24, 80};
  fm::ContentView view{display};
  History history;
  Configuration configuration;
  ManMode mode{view, history, configuration, Charset::Encoding::Ascii, 80};
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "loadPage: an empty name fails cleanly without running man at all") {
  CHECK_FALSE(mode.loadPage(""));
  CHECK(mode.currentPage().empty());
}

// The rest genuinely exercise the real `man`(1) command (this project's
// preference for real end-to-end execution over synthetic fixtures --
// see PipeBuffer/ArchiveReader precedent), but minimal environments
// (e.g. a slim Docker image) sometimes ship no man pages at all, so
// these skip gracefully rather than asserting a specific page exists,
// matching the same pattern used for Shell's real-bash tests and now
// LocateMode's real-locate tests.

TEST_CASE_FIXTURE(Fixture, "loadPage: a real man page loads through PipeBuffer and populates the view") {
  if (!mode.loadPage("ls")) {
    MESSAGE("man not installed or 'ls' has no man page here -- skipping");
    return;
  }
  CHECK(mode.currentPage() == "ls");
  CHECK_FALSE(view.lines().empty());
}

TEST_CASE_FIXTURE(Fixture, "loadPage: a nonexistent page fails cleanly") {
  CHECK_FALSE(mode.loadPage("this-command-does-not-exist-xyz123"));
}

TEST_CASE_FIXTURE(Fixture, "saveCursorPosition/loadPage: cursor position round-trips through History") {
  if (!mode.loadPage("ls")) {
    MESSAGE("man not installed or 'ls' has no man page here -- skipping");
    return;
  }
  if (view.lines().size() < 2) {
    MESSAGE("man page too short to test a nonzero cursor position -- skipping");
    return;
  }
  view.setCursorIndex(1);
  mode.saveCursorPosition();

  ManMode reloaded{view, history, configuration, Charset::Encoding::Ascii, 80};
  REQUIRE(reloaded.loadPage("ls"));
  CHECK(view.cursorIndex() == 1);
}

TEST_CASE_FIXTURE(Fixture, "reflow: a no-op (false) when no page is currently loaded") {
  CHECK_FALSE(mode.reflow(40));
}

TEST_CASE_FIXTURE(Fixture, "reflow: re-wraps the current page at the new width, preserving the session cursor") {
  if (!mode.loadPage("ls")) {
    MESSAGE("man not installed or 'ls' has no man page here -- skipping");
    return;
  }
  int wideLineCount = static_cast<int>(view.lines().size());
  view.setCursorIndex(1);

  // A stale saved position elsewhere in History that reflow() must
  // NOT jump to -- same reasoning as FileMode::reflow()'s own test.
  History::CursorPosition stale;
  stale.row = 0;
  history.setPosition(PositionCategory::ManPage, "ls", stale);

  REQUIRE(mode.reflow(20));
  CHECK(mode.currentPage() == "ls");
  int narrowLineCount = static_cast<int>(view.lines().size());
  CHECK(narrowLineCount >= wideLineCount);  // narrower width can only wrap into MORE lines, never fewer
  CHECK(view.cursorIndex() == 1);           // preserved, not reset to the History-saved row (0) or top
}
