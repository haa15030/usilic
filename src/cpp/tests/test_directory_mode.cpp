#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "content_view.hpp"
#include "directory_mode.hpp"
#include "doctest.h"
#include "recording_display.hpp"

namespace fs = std::filesystem;

using fm::Configuration;
using fm::DirectoryMode;
using fm::History;
using fm::ModeStack;
using fm::PositionCategory;
using fm::test::RecordingDisplay;

namespace {

// Real end-to-end verification (a real `ls -alF` against a real
// directory tree), matching this project's established preference
// for real data over synthetic fixtures when practical -- the whole
// point of this class is composing ChildProcess+Configuration's `ls`
// invocation with real filesystem output.
fs::path makeTempDir() {
  char tmpl[] = "/tmp/dirmode_testXXXXXX";
  char *result = mkdtemp(tmpl);
  REQUIRE(result != nullptr);
  return fs::path(result);
}

struct Fixture {
  fs::path root;
  RecordingDisplay display{24, 80};
  fm::ContentView view{display};
  ModeStack modeStack;
  History history;
  Configuration configuration;
  DirectoryMode mode{view, modeStack, history, configuration};

  Fixture() : root(makeTempDir()) {
    fs::create_directory(root / "subdir1");
    fs::create_directory(root / "subdir2");
    { std::ofstream(root / "subdir2" / "innerfile.txt") << "inner"; }
    { std::ofstream(root / "regular.txt") << "hello"; }
    { std::ofstream(root / "file with space.txt") << "spaced"; }
  }

  ~Fixture() { fs::remove_all(root); }

  std::string path() const { return root.string() + "/"; }
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "enterDirectory: lists real directory contents and classifies dirs vs files") {
  REQUIRE(mode.enterDirectory(path()));
  CHECK(mode.currentDirectory() == path());
  REQUIRE_FALSE(mode.entries().empty());

  bool sawSubdir1 = false, sawSubdir2 = false, sawRegular = false, sawSpaced = false;
  for (const auto &e : mode.entries()) {
    if (e.name == "subdir1/") { sawSubdir1 = true; CHECK(e.isDirectory); }
    if (e.name == "subdir2/") { sawSubdir2 = true; CHECK(e.isDirectory); }
    if (e.name == "regular.txt") { sawRegular = true; CHECK_FALSE(e.isDirectory); }
    // Exercises the get_col()-style month-anchor fix: a filename
    // containing a space must not be truncated at that internal
    // space (the historical bug this parsing approach fixes).
    if (e.name == "file with space.txt") { sawSpaced = true; CHECK_FALSE(e.isDirectory); }
  }
  CHECK(sawSubdir1);
  CHECK(sawSubdir2);
  CHECK(sawRegular);
  CHECK(sawSpaced);
}

TEST_CASE_FIXTURE(Fixture, "enterDirectory: a nonexistent directory fails cleanly") {
  CHECK_FALSE(mode.enterDirectory(path() + "does-not-exist/"));
}

TEST_CASE_FIXTURE(Fixture, "selectedEntry: tracks the ContentView cursor") {
  REQUIRE(mode.enterDirectory(path()));
  view.setCursorIndex(0);
  const auto *first = mode.selectedEntry();
  REQUIRE(first != nullptr);

  view.setCursorIndex(static_cast<int>(mode.entries().size()) - 1);
  const auto *last = mode.selectedEntry();
  REQUIRE(last != nullptr);
  CHECK(last->name == mode.entries().back().name);
}

TEST_CASE_FIXTURE(Fixture, "mode-line fields: shows the current directory's full path and \"(ls)\"") {
  REQUIRE(mode.enterDirectory(path()));

  // Ported from disp_window()'s file-selector case: the mode-line's
  // name field is the whole directory path (not the selected entry's
  // name -- that was this port's own earlier, incorrect design), and
  // the listing command appears in parens after the position text.
  std::string text = view.modeLineText();
  CHECK(text.rfind(path() + " ", 0) == 0);
  CHECK(text.substr(text.size() - 5) == " (ls)");

  // Stays the SAME regardless of which entry is selected -- unlike the
  // rejected per-entry-name design, this field isn't cursor-dependent
  // (only the current/total position text in between changes).
  view.setCursorIndex(static_cast<int>(mode.entries().size()) - 1);
  std::string textAtLast = view.modeLineText();
  CHECK(textAtLast.rfind(path() + " ", 0) == 0);
  CHECK(textAtLast.substr(textAtLast.size() - 5) == " (ls)");
}

TEST_CASE_FIXTURE(Fixture, "openSelected: descending into a subdirectory pushes a ModeStack frame") {
  REQUIRE(mode.enterDirectory(path()));
  CHECK_FALSE(mode.canGoBack());

  bool found = false;
  for (std::size_t i = 0; i < mode.entries().size(); i++) {
    if (mode.entries()[i].name == "subdir2/") {
      view.setCursorIndex(static_cast<int>(i));
      found = true;
      break;
    }
  }
  REQUIRE(found);

  REQUIRE(mode.openSelected());
  CHECK(mode.currentDirectory() == path() + "subdir2/");
  CHECK(mode.canGoBack());
  // `ls -alF` includes "./" and "../" alongside real entries.
  REQUIRE(mode.entries().size() == 3);
  bool sawInner = false;
  for (const auto &e : mode.entries())
    if (e.name == "innerfile.txt") sawInner = true;
  CHECK(sawInner);
}

TEST_CASE_FIXTURE(Fixture, "openSelected: a regular file is a no-op (file viewing not yet ported)") {
  REQUIRE(mode.enterDirectory(path()));
  bool found = false;
  for (std::size_t i = 0; i < mode.entries().size(); i++) {
    if (mode.entries()[i].name == "regular.txt") {
      view.setCursorIndex(static_cast<int>(i));
      found = true;
      break;
    }
  }
  REQUIRE(found);

  CHECK_FALSE(mode.openSelected());
  CHECK(mode.currentDirectory() == path());  // unchanged
  CHECK_FALSE(mode.canGoBack());
}

TEST_CASE_FIXTURE(Fixture, "openSelected on '../' navigates to the parent directory") {
  REQUIRE(mode.enterDirectory(path() + "subdir2/"));
  bool found = false;
  for (std::size_t i = 0; i < mode.entries().size(); i++) {
    if (mode.entries()[i].name == "../") {
      view.setCursorIndex(static_cast<int>(i));
      found = true;
      break;
    }
  }
  REQUIRE(found);

  REQUIRE(mode.openSelected());
  CHECK(mode.currentDirectory() == path());
}

TEST_CASE_FIXTURE(Fixture, "goBack: returns to the parent and restores the remembered cursor position") {
  REQUIRE(mode.enterDirectory(path()));

  std::size_t subdirIndex = 0;
  for (std::size_t i = 0; i < mode.entries().size(); i++)
    if (mode.entries()[i].name == "subdir2/") subdirIndex = i;
  view.setCursorIndex(static_cast<int>(subdirIndex));

  REQUIRE(mode.openSelected());  // now inside subdir2/, top-level cursor position for path() is saved
  REQUIRE(mode.goBack());

  CHECK(mode.currentDirectory() == path());
  CHECK_FALSE(mode.canGoBack());
  CHECK(view.cursorIndex() == static_cast<int>(subdirIndex));  // restored, not reset to 0

  auto saved = history.positionFor(PositionCategory::Directory, path());
  REQUIRE(saved.has_value());
  CHECK(saved->row == static_cast<int>(subdirIndex));
}

TEST_CASE_FIXTURE(Fixture, "enterDirectory: consultHistory=false always starts at the top, ignoring a saved position") {
  REQUIRE(mode.enterDirectory(path()));
  view.setCursorIndex(3);
  mode.enterDirectory(std::string(path()) + "subdir1");  // leaves path() unvisited; save a position for it directly
  History::CursorPosition saved;
  saved.row = 3;
  saved.totalRows = static_cast<int>(mode.entries().size());
  history.setPosition(PositionCategory::Directory, path(), saved);

  // With history consulted (the default, e.g. openSelected()/goBack()'s own use): restores row 3.
  REQUIRE(mode.enterDirectory(path()));
  CHECK(view.cursorIndex() == 3);

  // Without it (main()'s own explicit-argument launch case): always the top, same saved entry notwithstanding.
  REQUIRE(mode.enterDirectory(path(), /*consultHistory=*/false));
  CHECK(view.cursorIndex() == 0);
}

TEST_CASE_FIXTURE(Fixture, "goBack: false when there is nothing to go back to") {
  REQUIRE(mode.enterDirectory(path()));
  CHECK_FALSE(mode.goBack());
}

TEST_CASE_FIXTURE(Fixture, "toggleMark: marks and unmarks entries by index") {
  REQUIRE(mode.enterDirectory(path()));
  CHECK(mode.markedCount() == 0);
  CHECK_FALSE(mode.isMarked(0));

  mode.toggleMark(0);
  CHECK(mode.isMarked(0));
  CHECK(mode.markedCount() == 1);

  mode.toggleMark(0);
  CHECK_FALSE(mode.isMarked(0));
  CHECK(mode.markedCount() == 0);
}

TEST_CASE_FIXTURE(Fixture, "toggleMark: an out-of-range index is a safe no-op") {
  REQUIRE(mode.enterDirectory(path()));
  mode.toggleMark(-1);
  mode.toggleMark(static_cast<int>(mode.entries().size()) + 100);
  CHECK(mode.markedCount() == 0);
}

TEST_CASE_FIXTURE(Fixture, "targetEntries: falls back to the selected entry when nothing is marked") {
  REQUIRE(mode.enterDirectory(path()));
  view.setCursorIndex(1);
  auto targets = mode.targetEntries();
  REQUIRE(targets.size() == 1);
  CHECK(targets[0].name == mode.entries()[1].name);
}

TEST_CASE_FIXTURE(Fixture, "targetEntries: returns marked entries in listing order when any are marked") {
  REQUIRE(mode.enterDirectory(path()));
  REQUIRE(mode.entries().size() >= 3);
  mode.toggleMark(2);
  mode.toggleMark(0);

  auto targets = mode.targetEntries();
  REQUIRE(targets.size() == 2);
  CHECK(targets[0].name == mode.entries()[0].name);
  CHECK(targets[1].name == mode.entries()[2].name);
}

TEST_CASE_FIXTURE(Fixture, "clearMarks: removes all marks") {
  REQUIRE(mode.enterDirectory(path()));
  mode.toggleMark(0);
  mode.toggleMark(1);
  REQUIRE(mode.markedCount() == 2);
  mode.clearMarks();
  CHECK(mode.markedCount() == 0);
}

TEST_CASE_FIXTURE(Fixture, "enterDirectory: reloading the listing clears marks from the previous one") {
  REQUIRE(mode.enterDirectory(path()));
  mode.toggleMark(0);
  REQUIRE(mode.markedCount() == 1);

  REQUIRE(mode.enterDirectory(path() + "subdir2/"));
  CHECK(mode.markedCount() == 0);
}
