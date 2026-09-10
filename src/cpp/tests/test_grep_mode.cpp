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

// 2026-09-05追加、直接のユーザー報告("行末にtabコードがある行を
// CUT一覧に載せたい...^Q^Iでは入ってもgrepでヒットせず")のエンドツー
// エンド確認: PipeBuffer::wrapLine()側の修正(test_pipe_buffer.cpp)で
// 行末のタブがバッファに生のまま残るようになった――ここではその
// 「buffer側に生のタブが残っている」状態を再現し、パターン側にも
// 生のタブバイトを含めた実際の`grep`検索が本当に一致することを確認
// する(GrepMode::search()自体は実物のgrep/egrep/fgrepバイナリを
// forkするだけで、シェルを経由しないargv直接渡しなので、パターン中の
// 生のタブバイトがエスケープ問題で失われる心配も無い)。
TEST_CASE_FIXTURE(Fixture, "search (Basic/plain grep): a literal tab byte in the pattern matches a "
                           "trailing tab byte in a source line (the real end-to-end scenario PipeBuffer's "
                           "own trailing-tab preservation exists for)") {
  std::vector<std::string> lines = {"heading\t", "plain text", "another\t"};
  std::string pattern = "\t";  // a raw tab byte, as ^Q^I would insert into the prompt
  REQUIRE(mode.search(lines, pattern, GrepSyntax::Basic));
  REQUIRE(mode.matches().size() == 2);
  CHECK(mode.matches()[0].text == "heading\t");
  CHECK(mode.matches()[1].text == "another\t");
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

// 2026-09-10追加、直接のユーザー指摘("『マッチした文字列』だけを
// ハイライト表示には")。
TEST_CASE_FIXTURE(Fixture, "search: matchSpans give the exact byte range of the match within each line, "
                           "not the whole line") {
  std::vector<std::string> lines = {"alpha", "marker one", "beta", "marker two"};
  REQUIRE(mode.search(lines, "marker", GrepSyntax::Basic));
  REQUIRE(mode.matches().size() == 2);
  REQUIRE(mode.matches()[0].matchSpans.size() == 1);
  CHECK(mode.matches()[0].matchSpans[0].first == 0);
  CHECK(mode.matches()[0].matchSpans[0].second == 6);  // "marker" is 6 bytes, not the full "marker one" (10)
  REQUIRE(mode.matches()[1].matchSpans.size() == 1);
  CHECK(mode.matches()[1].matchSpans[0].first == 0);
  CHECK(mode.matches()[1].matchSpans[0].second == 6);
}

TEST_CASE_FIXTURE(Fixture, "search: matchSpans work with an end-of-line anchor (the exact in-place GREP "
                           "repro pattern, 'json$' against ls-style lines)") {
  std::vector<std::string> lines = {
      "-rw-r--r--@   1 haa15030  staff      3 Sep  9 20:17 one.json",
      "-rw-r--r--@   1 haa15030  staff      3 Sep  9 20:17 two.json",
  };
  REQUIRE(mode.search(lines, "json$", GrepSyntax::Basic));
  REQUIRE(mode.matches().size() == 2);
  REQUIRE(mode.matches()[0].matchSpans.size() == 1);
  CHECK(mode.matches()[0].matchSpans[0].first == static_cast<int>(lines[0].size()) - 4);
  CHECK(mode.matches()[0].matchSpans[0].second == static_cast<int>(lines[0].size()));
}

TEST_CASE_FIXTURE(Fixture, "search: matchSpans handle multiple matches on the SAME line") {
  std::vector<std::string> lines = {"cat and cat and dog"};
  REQUIRE(mode.search(lines, "cat", GrepSyntax::Basic));
  REQUIRE(mode.matches().size() == 1);
  REQUIRE(mode.matches()[0].matchSpans.size() == 2);
  CHECK(mode.matches()[0].matchSpans[0] == std::make_pair(0, 3));
  CHECK(mode.matches()[0].matchSpans[1] == std::make_pair(8, 11));
}

TEST_CASE_FIXTURE(Fixture, "search: a second search replaces the previous results") {
  std::vector<std::string> lines = {"alpha", "beta", "gamma"};
  REQUIRE(mode.search(lines, "alpha", GrepSyntax::Fixed));
  REQUIRE(mode.matches().size() == 1);

  REQUIRE(mode.search(lines, "a", GrepSyntax::Fixed));
  CHECK(mode.pattern() == "a");
  CHECK(mode.matches().size() == 3);  // alpha, beta, gamma all contain "a"
}

// 2026-08-23 fix (docs/CHANGES.md item 407, a real user report:
// "口座テスト"はFGREPでヒットするのに"マイナカード"はしない, both
// typed via the same IME into the same prompt against the same
// directory listing). Root cause: the real file "マイナカード表.png"
// stores its "ド" DECOMPOSED on disk (NFD: "ト" U+30C8 + a combining
// dakuten U+3099), while a normal IME always emits the single
// PRECOMPOSED codepoint (NFC, U+30C9) for "ド" -- a byte-literal
// fgrep never matches across that boundary even though the two forms
// are Unicode-canonically identical and render pixel-identical.
// "口座テスト" only happened to work because none of its characters
// are decomposable (no dakuten/handakuten kana), so its NFC and NFD
// forms are byte-identical -- see unicodeNormalizationCandidates()'s
// own comment for the full account, including why APFS genuinely
// stores files in EITHER form on the SAME machine (it's
// normalization-preserving, not -forcing).
TEST_CASE_FIXTURE(Fixture, "search (Fixed/fgrep, 2026-08-23 fix, item 407): matches a filename whose "
                           "Unicode-decomposed (NFD) bytes differ from the normally-precomposed (NFC) "
                           "query an IME actually types") {
  // "マイナカード表.png" with "ド" stored decomposed -- byte-for-byte
  // how the real file on the reporting user's machine was confirmed
  // stored on disk.
  std::string nfdFilename =
      "\xe3\x83\x9e\xe3\x82\xa4\xe3\x83\x8a\xe3\x82\xab\xe3\x83\xbc\xe3\x83\x88\xe3\x82\x99\xe8\xa1\xa8.png";
  // The same word, as any normal IME actually types it -- precomposed.
  std::string nfcQuery = "\xe3\x83\x9e\xe3\x82\xa4\xe3\x83\x8a\xe3\x82\xab\xe3\x83\xbc\xe3\x83\x89";
  std::vector<std::string> lines = {nfdFilename};
#ifdef __APPLE__
  REQUIRE(mode.search(lines, nfcQuery, GrepSyntax::Fixed));
  CHECK(mode.pattern() == nfcQuery);  // history/display still shows exactly what was typed
  REQUIRE(mode.matches().size() == 1);
  CHECK(mode.matches()[0].text == nfdFilename);  // the line's ORIGINAL bytes, not a normalized copy
#else
  // unicodeNormalizationCandidates() is a documented no-op off macOS
  // (see its own comment: APFS's normalization-preserving behavior,
  // and CFStringNormalize() itself, are both macOS-specific) -- this
  // cross-form match isn't expected to work there. Every other test in
  // this file (plain byte-identical patterns) is unaffected.
  CHECK_FALSE(mode.search(lines, nfcQuery, GrepSyntax::Fixed));
#endif
}

// -- searchInProcess() (2026-09-10, incremental search's own in-process
// matcher) -- same-day revision, a direct user report after trying it
// on a real directory (usilic -c ~/git/usilic): typing ".claude" made
// the leading "." match every character as a regex wildcard, turning
// the whole screen yellow. searchInProcess() is fgrep-style literal
// substring matching now, not regex -- see its own declaration comment
// in grep_mode.hpp.

TEST_CASE_FIXTURE(Fixture, "searchInProcess: a '.' is matched as a literal character, not a regex "
                           "wildcard (the exact user-reported bug)") {
  std::vector<std::string> lines = {".claude", "plain", "other.txt"};
  REQUIRE(mode.searchInProcess(lines, "."));
  // A real regex "." would match every line (each has at least one
  // character); fgrep-style literal "." only matches the two lines
  // that actually contain a literal dot.
  REQUIRE(mode.matches().size() == 2);
  CHECK(mode.matches()[0].sourceLineIndex == 0);
  CHECK(mode.matches()[0].matchSpans == std::vector<std::pair<int, int>>{{0, 1}});
  CHECK(mode.matches()[1].sourceLineIndex == 2);
  CHECK(mode.matches()[1].matchSpans == std::vector<std::pair<int, int>>{{5, 6}});
}

TEST_CASE_FIXTURE(Fixture, "searchInProcess: matches the exact literal substring, including regex "
                           "metacharacters other than '.' (e.g. '$', '*'), with no special meaning") {
  std::vector<std::string> lines = {"price: $5", "a*b", "plain"};
  REQUIRE(mode.searchInProcess(lines, "$5"));
  REQUIRE(mode.matches().size() == 1);
  CHECK(mode.matches()[0].text == "price: $5");
  CHECK(mode.matches()[0].matchSpans == std::vector<std::pair<int, int>>{{7, 9}});

  REQUIRE(mode.searchInProcess(lines, "a*b"));
  REQUIRE(mode.matches().size() == 1);
  CHECK(mode.matches()[0].text == "a*b");
}

TEST_CASE_FIXTURE(Fixture, "searchInProcess: finds multiple non-overlapping occurrences on the same line") {
  std::vector<std::string> lines = {"abcabcabc"};
  REQUIRE(mode.searchInProcess(lines, "abc"));
  REQUIRE(mode.matches().size() == 1);
  REQUIRE(mode.matches()[0].matchSpans.size() == 3);
  CHECK(mode.matches()[0].matchSpans[0] == std::pair<int, int>{0, 3});
  CHECK(mode.matches()[0].matchSpans[1] == std::pair<int, int>{3, 6});
  CHECK(mode.matches()[0].matchSpans[2] == std::pair<int, int>{6, 9});
}
