#include "configuration.hpp"
#include "content_view.hpp"
#include "cut_mode.hpp"
#include "doctest.h"
#include "mode_stack.hpp"
#include "recording_display.hpp"

using fm::Configuration;
using fm::CutMode;
using fm::GrepSyntax;
using fm::ModeStack;
using fm::test::RecordingDisplay;

namespace {

struct Fixture {
  RecordingDisplay display{24, 80};
  fm::ContentView view{display};
  ModeStack modeStack;
  Configuration configuration;
  CutMode mode{view, modeStack, configuration};
};

// A LOG-mode-shaped fixture: repeated prompt lines (the delimiter),
// each followed by a command and its output -- the exact scenario the
// user described (the "tcb(usilic)-1.5b" prompt string as the search
// string splitting a shell log into per-command segments).
std::vector<std::string> logLines() {
  return {
      "usilic$ ls",
      "a.txt  b.txt",
      "usilic$ pwd",
      "/home/user",
      "usilic$ echo done",
      "done",
  };
}

}  // namespace

TEST_CASE_FIXTURE(Fixture, "split: divides content into segments at each matching (delimiter) line") {
  REQUIRE(mode.split(logLines(), "usilic$", GrepSyntax::Fixed));
  REQUIRE(mode.segments().size() == 3);
  CHECK(mode.segments()[0].label == "usilic$ ls");
  CHECK(mode.segments()[1].label == "usilic$ pwd");
  CHECK(mode.segments()[2].label == "usilic$ echo done");
}

TEST_CASE_FIXTURE(Fixture, "segmentContent: a segment spans from its delimiter line up to (not including) the next") {
  REQUIRE(mode.split(logLines(), "usilic$", GrepSyntax::Fixed));
  auto content = mode.segmentContent(mode.segments()[0]);
  REQUIRE(content.size() == 2);
  CHECK(content[0] == "usilic$ ls");
  CHECK(content[1] == "a.txt  b.txt");
}

TEST_CASE_FIXTURE(Fixture, "segmentContent: the last segment runs to the end of the content") {
  REQUIRE(mode.split(logLines(), "usilic$", GrepSyntax::Fixed));
  auto content = mode.segmentContent(mode.segments()[2]);
  REQUIRE(content.size() == 2);
  CHECK(content[0] == "usilic$ echo done");
  CHECK(content[1] == "done");
}

TEST_CASE_FIXTURE(Fixture, "split: content before the first match becomes an unlabeled leading segment") {
  std::vector<std::string> lines = {"(session banner)", "usilic$ ls", "a.txt"};
  REQUIRE(mode.split(lines, "usilic$", GrepSyntax::Fixed));
  REQUIRE(mode.segments().size() == 2);
  CHECK(mode.segments()[0].label == "");
  auto preamble = mode.segmentContent(mode.segments()[0]);
  REQUIRE(preamble.size() == 1);
  CHECK(preamble[0] == "(session banner)");
}

TEST_CASE_FIXTURE(Fixture, "split (Regex/^J^C): supports extended regex delimiters") {
  std::vector<std::string> lines = {"=== entry 1 ===", "content A", "=== entry 2 ===", "content B"};
  REQUIRE(mode.split(lines, "^=== entry [0-9]+ ===$", GrepSyntax::Regex));
  REQUIRE(mode.segments().size() == 2);
  CHECK(mode.segments()[0].label == "=== entry 1 ===");
  CHECK(mode.segments()[1].label == "=== entry 2 ===");
}

TEST_CASE_FIXTURE(Fixture, "split: no matches at all means the whole input is a single segment") {
  std::vector<std::string> lines = {"one", "two", "three"};
  REQUIRE(mode.split(lines, "nonexistent", GrepSyntax::Fixed));
  REQUIRE(mode.segments().size() == 1);
  CHECK(mode.segmentContent(mode.segments()[0]).size() == 3);
}

TEST_CASE_FIXTURE(Fixture, "split: an empty pattern fails cleanly") {
  CHECK_FALSE(mode.split(logLines(), "", GrepSyntax::Fixed));
}

TEST_CASE_FIXTURE(Fixture, "selectedSegment: tracks the ContentView cursor over the segment list") {
  REQUIRE(mode.split(logLines(), "usilic$", GrepSyntax::Fixed));
  view.setCursorIndex(1);
  const auto *selected = mode.selectedSegment();
  REQUIRE(selected != nullptr);
  CHECK(selected->label == "usilic$ pwd");
}

TEST_CASE_FIXTURE(Fixture, "splitSelected: recursively re-splits the selected segment's own content") {
  std::vector<std::string> lines = {
      "=== chapter 1 ===", "--- section A ---", "textA1", "--- section B ---", "textB1",
      "=== chapter 2 ===", "--- section A ---", "textA2",
  };
  REQUIRE(mode.split(lines, "^=== chapter", GrepSyntax::Regex));
  REQUIRE(mode.segments().size() == 2);
  CHECK(mode.canGoBack() == false);

  view.setCursorIndex(0);  // chapter 1
  REQUIRE(mode.splitSelected("^--- section", GrepSyntax::Regex));
  CHECK(mode.canGoBack());
  // The chapter's own title line precedes the first "--- section"
  // match, so it becomes its own unlabeled leading segment (matching
  // split()'s own "content before the first match isn't dropped"
  // behavior) ahead of the two real section segments.
  REQUIRE(mode.segments().size() == 3);
  CHECK(mode.segments()[0].label == "");
  CHECK(mode.segments()[1].label == "--- section A ---");
  CHECK(mode.segments()[2].label == "--- section B ---");
  CHECK(mode.segmentContent(mode.segments()[1])[1] == "textA1");
}

TEST_CASE_FIXTURE(Fixture, "goBack: restores the parent split level exactly, including cursor position") {
  std::vector<std::string> lines = {
      "=== chapter 1 ===", "--- section A ---", "textA1", "--- section B ---", "textB1",
      "=== chapter 2 ===", "--- section A ---", "textA2",
  };
  REQUIRE(mode.split(lines, "^=== chapter", GrepSyntax::Regex));
  view.setCursorIndex(1);  // chapter 2, so the restored cursor is verifiably non-zero

  REQUIRE(mode.splitSelected("^--- section", GrepSyntax::Regex));
  // chapter 2's own title line + one "section A" -- see the previous
  // test case for why the title line becomes its own leading segment.
  REQUIRE(mode.segments().size() == 2);

  REQUIRE(mode.goBack());
  CHECK_FALSE(mode.canGoBack());
  CHECK(mode.pattern() == "^=== chapter");
  REQUIRE(mode.segments().size() == 2);
  CHECK(mode.segments()[0].label == "=== chapter 1 ===");
  CHECK(view.cursorIndex() == 1);  // restored, not reset to 0
}

TEST_CASE_FIXTURE(Fixture, "splitSelected: nests arbitrarily deep, like opening an archive inside an archive") {
  std::vector<std::string> lines = {"A", "AA", "AAA", "content", "AA", "content2"};
  REQUIRE(mode.split(lines, "A", GrepSyntax::Fixed));
  view.setCursorIndex(0);
  REQUIRE(mode.splitSelected("AA", GrepSyntax::Fixed));
  view.setCursorIndex(0);
  REQUIRE(mode.splitSelected("AAA", GrepSyntax::Fixed));
  CHECK(mode.pattern() == "AAA");

  REQUIRE(mode.goBack());
  CHECK(mode.pattern() == "AA");
  REQUIRE(mode.goBack());
  CHECK(mode.pattern() == "A");
  CHECK_FALSE(mode.canGoBack());
}

TEST_CASE_FIXTURE(Fixture, "splitSelected: a failed re-split leaves the current level untouched") {
  REQUIRE(mode.split(logLines(), "usilic$", GrepSyntax::Fixed));
  view.setCursorIndex(0);
  CHECK_FALSE(mode.splitSelected("", GrepSyntax::Fixed));  // empty pattern always fails
  CHECK_FALSE(mode.canGoBack());                            // the failed attempt's push was undone
  CHECK(mode.pattern() == "usilic$");                        // state unchanged
}

TEST_CASE_FIXTURE(Fixture, "goBack: false when already at the top level") {
  REQUIRE(mode.split(logLines(), "usilic$", GrepSyntax::Fixed));
  CHECK_FALSE(mode.goBack());
}
