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

TEST_CASE_FIXTURE(Fixture, "goBack: false when there is nothing to go back to") {
  REQUIRE(mode.enterDirectory(path()));
  CHECK_FALSE(mode.goBack());
}
