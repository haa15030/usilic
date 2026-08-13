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

// 2026-08-15 addition: a direct user request ("全モードのモードライン
// に「画面TOPからENDまでのバイト数」を追加してほしい") specifically
// named Directory as one of the modes still missing this -- trailing
// changed from the "(ls)" program indicator to the listing's own
// total byte count (see ContentView::totalByteCount()'s own comment),
// matching every other mode's shared convention; "(ls)" moved to
// trailing2 as a fallback (see the provider's own comment on this
// judgment call).
TEST_CASE_FIXTURE(Fixture, "enterDirectory: mode line shows the listing's own total byte count "
                           "(trailing) and \"(ls)\" (trailing2, when no search annotation is active), "
                           "AND the \"usilic v1.5b\" rightWatermark still shows alongside both") {
  REQUIRE(mode.enterDirectory(path()));
  std::string modeLine = view.modeLineText();
  CHECK(modeLine.find(std::to_string(view.totalByteCount())) != std::string::npos);
  CHECK(modeLine.find("(ls)") != std::string::npos);
  // 2026-08-15 regression check: fillTrailingToEdge must stay keyed on
  // searchAnnotation_ specifically, NOT trailing2's own emptiness (see
  // the provider's own comment) -- a real regression found "(ls)"
  // itself getting padded to the row's right edge, which silently
  // dropped the watermark via renderModeLine()'s own drawnLen check.
  // modeLineText() doesn't include rightWatermark at all (see the
  // ContentView tests' own identical comment) -- check the real
  // rendered row instead.
  view.redraw();
  bool sawWatermark = false;
  for (int row = 0; row < 24; row++)
    if (display.rowText(row).find("usilic v1.5b") != std::string::npos) sawWatermark = true;
  CHECK(sawWatermark);
}

TEST_CASE_FIXTURE(Fixture, "enterDirectory: nameStartColumn locates the real name, even with an internal space") {
  // Ported from scroll.c's get_col()/init_col() -- ContentView::Line::
  // highlightStartColumn (fed from this field) is what makes the
  // cursor's reverse-video start at the filename instead of column 0,
  // fixing a real screenshot-reported bug (2026-08-03: the whole
  // permissions/size/date prefix was wrongly highlighted too).
  REQUIRE(mode.enterDirectory(path()));
  for (const auto &e : mode.entries()) {
    REQUIRE(e.nameStartColumn >= 0);
    REQUIRE(static_cast<std::size_t>(e.nameStartColumn) <= e.lsLine.size());
    CHECK(e.lsLine.substr(e.nameStartColumn) == e.name);
  }
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

TEST_CASE_FIXTURE(Fixture, "goToParent: jumps to the parent directory without moving the cursor onto '../' first (K-19, ^O^K)") {
  REQUIRE(mode.enterDirectory(path() + "subdir2/"));
  view.setCursorIndex(0);  // deliberately NOT on "../" -- goToParent() shouldn't need it

  REQUIRE(mode.goToParent());
  CHECK(mode.currentDirectory() == path());
  CHECK(mode.canGoBack());  // pushed a frame just like openSelected() on "../" does
}

TEST_CASE_FIXTURE(Fixture, "goToParent: pushes a ModeStack frame so goBack() returns to the child directory") {
  REQUIRE(mode.enterDirectory(path() + "subdir2/"));
  REQUIRE(mode.goToParent());
  REQUIRE(mode.goBack());
  CHECK(mode.currentDirectory() == path() + "subdir2/");
}

TEST_CASE_FIXTURE(Fixture, "goToParent: at the root, is a safe no-op (matching legacy's own truncation-is-a-no-op behavior)") {
  REQUIRE(mode.enterDirectory("/"));
  CHECK_FALSE(mode.goToParent());
  CHECK(mode.currentDirectory() == "/");
  CHECK_FALSE(mode.canGoBack());
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

TEST_CASE_FIXTURE(Fixture, "setFilenameOnlyMode: switches displayed rows to bare filenames without re-running ls") {
  REQUIRE(mode.enterDirectory(path()));
  int idx = -1;
  for (std::size_t i = 0; i < mode.entries().size(); i++)
    if (mode.entries()[i].name == "regular.txt") idx = static_cast<int>(i);
  REQUIRE(idx >= 0);

  CHECK(view.lines()[idx].text == mode.entries()[idx].lsLine);  // default: full `ls -alF` line
  CHECK(view.lines()[idx].text != "regular.txt");

  view.setCursorIndex(idx);
  mode.setFilenameOnlyMode(true);
  CHECK(mode.filenameOnlyMode());
  CHECK(view.lines()[idx].text == "regular.txt");  // now just the bare name
  CHECK(mode.entries()[idx].name == "regular.txt");  // entries_ itself is unchanged (same list, same order)
  CHECK(view.cursorIndex() == idx);  // selection/cursor untouched by the display-only rebuild

  mode.setFilenameOnlyMode(false);
  CHECK_FALSE(mode.filenameOnlyMode());
  CHECK(view.lines()[idx].text == mode.entries()[idx].lsLine);  // switches back cleanly
}

TEST_CASE_FIXTURE(Fixture, "goBack: false when there is nothing to go back to") {
  REQUIRE(mode.enterDirectory(path()));
  CHECK_FALSE(mode.goBack());
}

// 2026-08-13 (CLAUDE.md history item 292's ^G policy): resetNavigationToFloor()
// discards a prior descent chain and establishes a fresh, TOGGLING
// current<->previous pair, matching the ^X^D "Select directory" picker's
// own required behavior (see the method's own declaration comment).
TEST_CASE_FIXTURE(Fixture, "resetNavigationToFloor: discards a prior multi-level descent chain") {
  REQUIRE(mode.enterDirectory(path()));
  std::size_t subdirIndex = 0;
  for (std::size_t i = 0; i < mode.entries().size(); i++)
    if (mode.entries()[i].name == "subdir2/") subdirIndex = i;
  view.setCursorIndex(static_cast<int>(subdirIndex));
  REQUIRE(mode.openSelected());  // descend into subdir2/
  CHECK(mode.canGoBack());       // one real descent frame on the stack

  // Simulate the ^X^D picker landing somewhere unrelated to that chain.
  REQUIRE(mode.enterDirectory(std::string(path()) + "subdir1"));
  mode.resetNavigationToFloor(path());

  CHECK(mode.canGoBack());  // the floor itself still counts as "something to go back to"
  REQUIRE(mode.goBack());
  // NOT the subdir2/ descent frame's own parent (path()) via the OLD
  // chain -- goBack() lands on path() either way here, so the real
  // proof is the NEXT assertion: a stack that still remembered the old
  // subdir2/ frame would have nothing left to pop now (canGoBack()
  // false), but the floor's own re-push (this function's own
  // toggle behavior) means there's still exactly one more level.
  CHECK(mode.currentDirectory() == path());
  CHECK(mode.canGoBack());
}

TEST_CASE_FIXTURE(Fixture, "resetNavigationToFloor + goBack: toggles back and forth indefinitely") {
  REQUIRE(mode.enterDirectory(path()));
  REQUIRE(mode.enterDirectory(std::string(path()) + "subdir1"));
  mode.resetNavigationToFloor(path());

  REQUIRE(mode.goBack());
  CHECK(mode.currentDirectory() == path());
  REQUIRE(mode.goBack());
  CHECK(mode.currentDirectory() == std::string(path()) + "subdir1/");
  REQUIRE(mode.goBack());
  CHECK(mode.currentDirectory() == path());
  CHECK(mode.canGoBack());  // never exhausts -- a genuine toggle, not a one-shot pop
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

TEST_CASE_FIXTURE(Fixture, "markAll: marks every entry in the listing (K-72, ^X^Y)") {
  REQUIRE(mode.enterDirectory(path()));
  mode.toggleMark(1);  // a pre-existing single mark shouldn't matter -- markAll() replaces it wholesale
  mode.markAll();
  CHECK(mode.markedCount() == mode.entries().size());
  for (int i = 0; i < static_cast<int>(mode.entries().size()); i++) CHECK(mode.isMarked(i));

  mode.clearMarks();  // K-73, ^X^N
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
