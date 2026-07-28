#include "configuration.hpp"
#include "content_view.hpp"
#include "doctest.h"
#include "grep_mode.hpp"
#include "recording_display.hpp"

using fm::Configuration;
using fm::GrepMode;
using fm::GrepSyntax;
using fm::test::RecordingDisplay;

namespace {

struct Fixture {
  RecordingDisplay display{24, 80};
  fm::ContentView view{display};
  Configuration configuration;
  GrepMode mode{view, configuration};
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "search (Fixed/fgrep): real fgrep finds matching lines with correct source indices") {
  std::vector<std::string> lines = {"apple", "banana", "apple pie", "cherry"};
  REQUIRE(mode.search(lines, "apple", GrepSyntax::Fixed));
  CHECK(mode.pattern() == "apple");
  REQUIRE(mode.matches().size() == 2);
  CHECK(mode.matches()[0].sourceLineIndex == 0);
  CHECK(mode.matches()[0].text == "apple");
  CHECK(mode.matches()[1].sourceLineIndex == 2);
  CHECK(mode.matches()[1].text == "apple pie");
}

TEST_CASE_FIXTURE(Fixture, "search (Regex/egrep): real egrep supports extended regex patterns") {
  std::vector<std::string> lines = {"foo123", "foobar", "123456", "baz"};
  REQUIRE(mode.search(lines, "^[a-z]+[0-9]+$", GrepSyntax::Regex));
  REQUIRE(mode.matches().size() == 1);
  CHECK(mode.matches()[0].text == "foo123");
  CHECK(mode.matches()[0].sourceLineIndex == 0);
}

TEST_CASE_FIXTURE(Fixture, "search: fgrep treats the pattern as a literal fixed string, not a regex") {
  std::vector<std::string> lines = {"a.b", "axb", "a.b again"};
  REQUIRE(mode.search(lines, "a.b", GrepSyntax::Fixed));
  // "." must match only a literal '.', not "any character" -- "axb" is not a match.
  REQUIRE(mode.matches().size() == 2);
  CHECK(mode.matches()[0].text == "a.b");
  CHECK(mode.matches()[1].text == "a.b again");
}

TEST_CASE_FIXTURE(Fixture, "search: zero matches is a successful (not failed) search") {
  std::vector<std::string> lines = {"one", "two", "three"};
  REQUIRE(mode.search(lines, "nonexistent-pattern-xyz", GrepSyntax::Fixed));
  CHECK(mode.matches().empty());
  CHECK(view.lines().empty());
}

TEST_CASE_FIXTURE(Fixture, "search: an empty pattern fails cleanly rather than matching everything") {
  std::vector<std::string> lines = {"one", "two"};
  CHECK_FALSE(mode.search(lines, "", GrepSyntax::Fixed));
}

TEST_CASE_FIXTURE(Fixture, "selectedMatch: tracks the ContentView cursor over the filtered results") {
  std::vector<std::string> lines = {"match1", "skip", "match2", "skip", "match3"};
  REQUIRE(mode.search(lines, "match", GrepSyntax::Fixed));
  REQUIRE(mode.matches().size() == 3);

  view.setCursorIndex(1);
  const auto *selected = mode.selectedMatch();
  REQUIRE(selected != nullptr);
  CHECK(selected->text == "match2");
  CHECK(selected->sourceLineIndex == 2);  // maps back to the ORIGINAL line index, not the filtered index
}

TEST_CASE_FIXTURE(Fixture, "search: a second search replaces the previous results") {
  std::vector<std::string> lines = {"alpha", "beta", "gamma"};
  REQUIRE(mode.search(lines, "alpha", GrepSyntax::Fixed));
  REQUIRE(mode.matches().size() == 1);

  REQUIRE(mode.search(lines, "a", GrepSyntax::Fixed));
  CHECK(mode.pattern() == "a");
  CHECK(mode.matches().size() == 3);  // alpha, beta, gamma all contain "a"
}
