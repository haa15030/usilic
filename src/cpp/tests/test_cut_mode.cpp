#include "configuration.hpp"
#include "content_view.hpp"
#include "cut_mode.hpp"
#include "doctest.h"
#include "mode_stack.hpp"
#include "pipe_buffer.hpp"
#include "recording_display.hpp"

using fm::Configuration;
using fm::CutMode;
using fm::GrepSyntax;
using fm::ModeStack;
using fm::StyledLine;
using fm::test::RecordingDisplay;

namespace {

struct Fixture {
  RecordingDisplay display{24, 80};
  fm::ContentView view{display};
  ModeStack modeStack;
  Configuration configuration;
  CutMode mode{view, modeStack, configuration};
};

// 2026-08-05: split()/pushSplit() now take StyledLine (carrying
// PipeBuffer's own Bold/Underline AttributeRuns alongside each line's
// text -- see ContentView::Line::attributes' own comment) instead of
// plain std::string, so CutContent's own displayed segment content can
// preserve HELP.jp's bold section headings instead of silently losing
// them. Every test here still only cares about plain text, so this
// helper does the wrapping (empty attributes) once instead of at every
// call site.
std::vector<StyledLine> toStyled(const std::vector<std::string> &lines) {
  std::vector<StyledLine> out;
  out.reserve(lines.size());
  for (const auto &line : lines) out.push_back({line, {}});
  return out;
}

// A LOG-mode-shaped fixture: repeated prompt lines (the delimiter),
// each followed by a command and its output -- the exact scenario the
// user described (the "usilic(usilic)-1.5b" prompt string as the search
// string splitting a shell log into per-command segments).
std::vector<StyledLine> logLines() {
  return toStyled({
      "usilic$ ls",
      "a.txt  b.txt",
      "usilic$ pwd",
      "/home/user",
      "usilic$ echo done",
      "done",
  });
}

}  // namespace

TEST_CASE_FIXTURE(Fixture, "split: divides content into segments at each matching (delimiter) line") {
  REQUIRE(mode.split(logLines(), "usilic$", GrepSyntax::Fixed));
  REQUIRE(mode.segments().size() == 3);
  CHECK(mode.segments()[0].label == "usilic$ ls");
  CHECK(mode.segments()[1].label == "usilic$ pwd");
  CHECK(mode.segments()[2].label == "usilic$ echo done");
}

// 2026-09-10追加、直接のユーザー提案("検索可能ならCUTもできるはず"):
// GrepModeと全く同じ理由でCutModeもconvertTabsForSearch()を複製して
// いる(クラスコメント参照)。PipeBuffer::wrapLine()が残すU+2060
// マーカー(test_pipe_buffer.cpp参照)を、^Q^Iが挿入する生のタブ
// バイトのパターンで実際に分割できることをここで確認する
// (test_grep_mode.cppの対になるテストと同じ形)。
TEST_CASE_FIXTURE(Fixture, "split: a literal tab byte in the pattern (as ^Q^I would insert) is "
                           "converted to U+2060 before matching, so it splits at the U+2060 "
                           "marker PipeBuffer leaves behind wherever a tab was") {
  auto lines = toStyled({"heading\xE2\x81\xA0", "plain text", "another\xE2\x81\xA0"});
  std::string pattern = "\t";  // a raw tab byte, as ^Q^I would insert into the prompt
  REQUIRE(mode.split(lines, pattern, GrepSyntax::Fixed));
  REQUIRE(mode.segments().size() == 2);
  CHECK(mode.segments()[0].label == "heading\xE2\x81\xA0");
  CHECK(mode.segments()[1].label == "another\xE2\x81\xA0");
}

TEST_CASE_FIXTURE(Fixture, "segmentContent: a segment spans from its delimiter line up to (not including) the next") {
  REQUIRE(mode.split(logLines(), "usilic$", GrepSyntax::Fixed));
  auto content = mode.segmentContent(mode.segments()[0]);
  REQUIRE(content.size() == 2);
  CHECK(content[0].text == "usilic$ ls");
  CHECK(content[1].text == "a.txt  b.txt");
}

TEST_CASE_FIXTURE(Fixture, "segmentContent: a source line's own AttributeRuns (bold/underline, e.g. HELP.jp's "
                           "own nroff-overstrike section headings) survive the split/segmentContent() "
                           "round-trip, not just its plain text (2026-08-05 -- see StyledLine's own "
                           "parameter comment on split())") {
  std::vector<StyledLine> lines = {
      {"usilic$ ls", {{0, 4, fm::Display::Attribute::Bold}}},  // "usil" bold, just to have SOMETHING to check
      {"a.txt", {}},
  };
  REQUIRE(mode.split(lines, "usilic$", GrepSyntax::Fixed));
  auto content = mode.segmentContent(mode.segments()[0]);
  REQUIRE(content.size() == 2);
  REQUIRE(content[0].attributes.size() == 1);
  CHECK(content[0].attributes[0].attribute == fm::Display::Attribute::Bold);
  CHECK(content[0].attributes[0].start == 0);
  CHECK(content[0].attributes[0].end == 4);
  CHECK(content[1].attributes.empty());
}

TEST_CASE_FIXTURE(Fixture, "segmentContent: the last segment runs to the end of the content") {
  REQUIRE(mode.split(logLines(), "usilic$", GrepSyntax::Fixed));
  auto content = mode.segmentContent(mode.segments()[2]);
  REQUIRE(content.size() == 2);
  CHECK(content[0].text == "usilic$ echo done");
  CHECK(content[1].text == "done");
}

TEST_CASE_FIXTURE(Fixture, "split: content before the first match becomes a leading segment labeled \"--\" "
                           "(2026-08-12 fix: this used to be an empty string, rendering as a completely "
                           "blank top row instead of ContentView's own established \"empty/unlabeled "
                           "region\" convention -- a real report + reference screenshot found exactly that "
                           "blank row, tracing back to this session's own much earlier item ㉜/㊱)") {
  auto lines = toStyled({"(session banner)", "usilic$ ls", "a.txt"});
  REQUIRE(mode.split(lines, "usilic$", GrepSyntax::Fixed));
  REQUIRE(mode.segments().size() == 2);
  CHECK(mode.segments()[0].label == "--");
  auto preamble = mode.segmentContent(mode.segments()[0]);
  REQUIRE(preamble.size() == 1);
  CHECK(preamble[0].text == "(session banner)");
}

TEST_CASE_FIXTURE(Fixture, "split (Regex/^J^C): supports extended regex delimiters") {
  auto lines = toStyled({"=== entry 1 ===", "content A", "=== entry 2 ===", "content B"});
  REQUIRE(mode.split(lines, "^=== entry [0-9]+ ===$", GrepSyntax::Regex));
  REQUIRE(mode.segments().size() == 2);
  CHECK(mode.segments()[0].label == "=== entry 1 ===");
  CHECK(mode.segments()[1].label == "=== entry 2 ===");
}

TEST_CASE_FIXTURE(Fixture, "split: no matches at all means the whole input is a single segment") {
  auto lines = toStyled({"one", "two", "three"});
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
  auto lines = toStyled({
      "=== chapter 1 ===", "--- section A ---", "textA1", "--- section B ---", "textB1",
      "=== chapter 2 ===", "--- section A ---", "textA2",
  });
  REQUIRE(mode.split(lines, "^=== chapter", GrepSyntax::Regex));
  REQUIRE(mode.segments().size() == 2);
  CHECK(mode.canGoBack() == false);

  view.setCursorIndex(0);  // chapter 1
  REQUIRE(mode.splitSelected("^--- section", GrepSyntax::Regex));
  CHECK(mode.canGoBack());
  // The chapter's own title line precedes the first "--- section"
  // match, so it becomes its own "--"-labeled leading segment
  // (matching split()'s own "content before the first match isn't
  // dropped" behavior) ahead of the two real section segments.
  REQUIRE(mode.segments().size() == 3);
  CHECK(mode.segments()[0].label == "--");
  CHECK(mode.segments()[1].label == "--- section A ---");
  CHECK(mode.segments()[2].label == "--- section B ---");
  CHECK(mode.segmentContent(mode.segments()[1])[1].text == "textA1");
}

TEST_CASE_FIXTURE(Fixture, "goBack: restores the parent split level exactly, including cursor position") {
  auto lines = toStyled({
      "=== chapter 1 ===", "--- section A ---", "textA1", "--- section B ---", "textB1",
      "=== chapter 2 ===", "--- section A ---", "textA2",
  });
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
  auto lines = toStyled({"A", "AA", "AAA", "content", "AA", "content2"});
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

TEST_CASE_FIXTURE(Fixture, "pushSplit: splits an independent source, not the currently selected segment") {
  REQUIRE(mode.split(logLines(), "usilic$", GrepSyntax::Fixed));
  view.setCursorIndex(1);  // "usilic$ pwd" -- pushSplit() must ignore this entirely

  auto independent = toStyled({"=== A ===", "one", "=== B ===", "two"});
  REQUIRE(mode.pushSplit(independent, "^===", GrepSyntax::Regex));
  CHECK(mode.canGoBack());
  REQUIRE(mode.segments().size() == 2);
  CHECK(mode.segments()[0].label == "=== A ===");
  CHECK(mode.segments()[1].label == "=== B ===");
  CHECK(mode.pattern() == "^===");
}

TEST_CASE_FIXTURE(Fixture, "pushSplit: goBack() restores exactly what was pushed, like splitSelected()") {
  REQUIRE(mode.split(logLines(), "usilic$", GrepSyntax::Fixed));
  view.setCursorIndex(2);

  auto independent = toStyled({"=== A ===", "one"});
  REQUIRE(mode.pushSplit(independent, "^===", GrepSyntax::Regex));

  REQUIRE(mode.goBack());
  CHECK_FALSE(mode.canGoBack());
  CHECK(mode.pattern() == "usilic$");
  REQUIRE(mode.segments().size() == 3);
  CHECK(view.cursorIndex() == 2);  // restored, not reset
}

TEST_CASE_FIXTURE(Fixture, "pushSplit: works from a completely fresh CutMode with nothing split yet") {
  auto independent = toStyled({"=== A ===", "one"});
  REQUIRE(mode.pushSplit(independent, "^===", GrepSyntax::Regex));
  CHECK(mode.canGoBack());
  REQUIRE(mode.segments().size() == 1);

  REQUIRE(mode.goBack());
  CHECK_FALSE(mode.canGoBack());
  CHECK(mode.segments().empty());  // restored to the original empty state
}

TEST_CASE_FIXTURE(Fixture, "pushSplit: a failed split leaves the current level untouched, like splitSelected()") {
  REQUIRE(mode.split(logLines(), "usilic$", GrepSyntax::Fixed));
  CHECK_FALSE(mode.pushSplit(toStyled({"a", "b"}), "", GrepSyntax::Fixed));  // empty pattern always fails
  CHECK_FALSE(mode.canGoBack());                                   // the failed attempt's push was undone
  CHECK(mode.pattern() == "usilic$");                               // state unchanged
}

TEST_CASE_FIXTURE(Fixture, "pushSplit: nests on top of an existing splitSelected() level too") {
  auto lines = toStyled({
      "=== chapter 1 ===", "--- section A ---", "textA1", "=== chapter 2 ===", "--- section A ---", "textA2",
  });
  REQUIRE(mode.split(lines, "^=== chapter", GrepSyntax::Regex));
  view.setCursorIndex(0);
  REQUIRE(mode.splitSelected("^--- section", GrepSyntax::Regex));

  auto independent = toStyled({"=== A ===", "one"});
  REQUIRE(mode.pushSplit(independent, "^===", GrepSyntax::Regex));
  CHECK(mode.pattern() == "^===");

  REQUIRE(mode.goBack());
  CHECK(mode.pattern() == "^--- section");  // back to the splitSelected() level
  REQUIRE(mode.goBack());
  CHECK(mode.pattern() == "^=== chapter");  // back to the original top level
  CHECK_FALSE(mode.canGoBack());
}

// 2026-08-23 fix (docs/CHANGES.md item 407): CUT's fixed/regex split
// shells out to the exact same fgrep/egrep GrepMode::search() does, so
// it has the exact same byte-literal-matching blind spot -- see that
// file's own identically-shaped test (and unicodeNormalizationCandidates()'s
// own comment) for the full account.
TEST_CASE_FIXTURE(Fixture, "split (Fixed/fgrep, 2026-08-23 fix, item 407): matches a delimiter line whose "
                           "Unicode-decomposed (NFD) bytes differ from the normally-precomposed (NFC) "
                           "pattern an IME actually types") {
  // "ド" (U+30C9) stored decomposed: "ト" (U+30C8) + combining dakuten (U+3099).
  std::string nfdDelimiter = "\xe3\x83\x88\xe3\x82\x99===";
  std::string nfcPattern = "\xe3\x83\x89===";  // same word, precomposed -- what an IME actually types
  auto lines = toStyled({nfdDelimiter, "content"});
#ifdef __APPLE__
  REQUIRE(mode.split(lines, nfcPattern, GrepSyntax::Fixed));
  CHECK(mode.pattern() == nfcPattern);
  REQUIRE(mode.segments().size() == 1);
  CHECK(mode.segments()[0].label == nfdDelimiter);  // the ORIGINAL line's bytes, not a normalized copy
#else
  // See test_grep_mode.cpp's own identical #else branch -- macOS-only
  // fix. Unlike GrepMode::search(), CutMode::split() still returns
  // true with zero delimiter matches (a single unlabeled segment
  // spanning everything -- see its own "matchLines.empty()" branch),
  // so the observable difference here is the segment count/label, not
  // the return value.
  REQUIRE(mode.split(lines, nfcPattern, GrepSyntax::Fixed));
  REQUIRE(mode.segments().size() == 1);
  CHECK(mode.segments()[0].label.empty());  // no delimiter matched -- one big unlabeled segment
#endif
}
