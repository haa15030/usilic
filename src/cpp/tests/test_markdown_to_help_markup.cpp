#include "doctest.h"
#include "help_text.hpp"
#include "markdown_to_help_markup.hpp"

// 2026-09-22追加、直接のユーザー依頼("せっかく.mdフォーマットで書かれているので
// usilicに文字装飾機能を実装してほしい"): markdownToHelpMarkup()自身の単体テスト。
// 出力するタグ(<b>/<u>/<i>/<indentN>)自体の制御バイトへの展開は、既存の
// expandHelpMarkup()のテスト(test_help_text.cpp)が既に確認済みなので、ここでは
// 「正しいタグの文字列を生成するか」だけを確認する(design.md自身の対応表参照)。

using fm::markdownToHelpMarkup;

TEST_CASE("markdownToHelpMarkup: heading level 1 gets bold+underline combined, level 2 gets "
          "bold alone, level 3+ gets bold plus indent (2026-09-22: combining bold+underline "
          "for H1 is now safe -- see pipe_buffer.hpp's own currentBold_/currentUnderline_ "
          "comment for why nesting <b>/<u> no longer breaks)") {
  CHECK(markdownToHelpMarkup("# Title") == "<u><b>Title</b></u>");
  CHECK(markdownToHelpMarkup("## Section") == "<b>Section</b>");
  CHECK(markdownToHelpMarkup("### Sub") == "<indent02><b>Sub</b></indent>");
  CHECK(markdownToHelpMarkup("###### Deep") == "<indent08><b>Deep</b></indent>");
}

TEST_CASE("markdownToHelpMarkup: heading text now supports inline emphasis too (2026-09-22, "
          "since <b>/<u> can safely nest other tags)") {
  CHECK(markdownToHelpMarkup("## Section with *italic*") == "<b>Section with <i>italic</i></b>");
}

TEST_CASE("markdownToHelpMarkup: a bare '#word' (no space) is NOT a heading") {
  CHECK(markdownToHelpMarkup("#hashtag") == "#hashtag");
}

TEST_CASE("markdownToHelpMarkup: bold (** and __) and italic (* and _)") {
  CHECK(markdownToHelpMarkup("**bold**") == "<b>bold</b>");
  CHECK(markdownToHelpMarkup("__bold__") == "<b>bold</b>");
  CHECK(markdownToHelpMarkup("*italic*") == "<i>italic</i>");
  CHECK(markdownToHelpMarkup("_italic_") == "<i>italic</i>");
  CHECK(markdownToHelpMarkup("**bold** and *italic*") == "<b>bold</b> and <i>italic</i>");
}

TEST_CASE("markdownToHelpMarkup: inline code is not interpreted as markdown inside") {
  CHECK(markdownToHelpMarkup("use `**not bold**` here") == "use <u>**not bold**</u> here");
}

TEST_CASE("markdownToHelpMarkup: fenced code blocks are indented and NOT inline-parsed") {
  std::string in = "```cpp\nint *p = a_b__c;\n```";
  CHECK(markdownToHelpMarkup(in) == "<indent04>int *p = a_b__c;</indent>");
}

TEST_CASE("markdownToHelpMarkup: unordered and ordered list items keep their marker and are "
          "indented by nesting level") {
  CHECK(markdownToHelpMarkup("- item") == "<indent00>- item</indent>");
  CHECK(markdownToHelpMarkup("  - nested") == "<indent02>- nested</indent>");
  CHECK(markdownToHelpMarkup("1. first") == "<indent00>1. first</indent>");
}

TEST_CASE("markdownToHelpMarkup: blockquotes are indented and italicized, nesting via '>' count") {
  CHECK(markdownToHelpMarkup("> quoted") == "<indent02><i>quoted</i></indent>");
  CHECK(markdownToHelpMarkup(">> nested quote") == "<indent04><i>nested quote</i></indent>");
}

TEST_CASE("markdownToHelpMarkup: horizontal rules become a fixed-width dash line") {
  CHECK(markdownToHelpMarkup("---") == std::string(60, '-'));
  CHECK(markdownToHelpMarkup("***") == std::string(60, '-'));
  CHECK(markdownToHelpMarkup("- - -") == std::string(60, '-'));
}

TEST_CASE("markdownToHelpMarkup: links and images") {
  CHECK(markdownToHelpMarkup("[text](http://example.com)") == "<u>text</u> (http://example.com)");
  CHECK(markdownToHelpMarkup("![alt](pic.png)") == "[image] <u>alt</u> (pic.png)");
}

TEST_CASE("markdownToHelpMarkup: backslash-escaped markdown characters are NOT interpreted") {
  CHECK(markdownToHelpMarkup("\\*not italic\\*") == "*not italic*");
  CHECK(markdownToHelpMarkup("\\# not a heading") == "# not a heading");
}

TEST_CASE("markdownToHelpMarkup: an unterminated emphasis marker is passed through literally "
          "(no hang, no crash)") {
  CHECK(markdownToHelpMarkup("*never closed") == "*never closed");
  CHECK(markdownToHelpMarkup("**never closed either") == "**never closed either");
}

TEST_CASE("markdownToHelpMarkup: emphasis spanning a soft-wrapped paragraph line is still "
          "recognized (lines are joined with a space)") {
  CHECK(markdownToHelpMarkup("this is **bold\ntext** across lines") == "this is <b>bold text</b> across lines");
}

TEST_CASE("markdownToHelpMarkup: a trailing double-space at end of line is a hard break, kept "
          "as a real newline inside the joined paragraph") {
  CHECK(markdownToHelpMarkup("line one  \nline two") == "line one\nline two");
}

TEST_CASE("markdownToHelpMarkup: blank lines separate paragraphs and are preserved") {
  CHECK(markdownToHelpMarkup("para one\n\npara two") == "para one\n\npara two");
}

TEST_CASE("markdownToHelpMarkup: the produced tags are understood by the existing "
          "expandHelpMarkup() (integration between the two conversion stages) -- "
          "2026-09-22: <b>/<u> now emit \\x03/\\x04 toggle bytes, not per-character "
          "overstrike (see help_text.cpp's own comment)") {
  std::string expanded = fm::expandHelpMarkup(markdownToHelpMarkup("# Title\n\n**bold**"));
  CHECK(expanded.find("\x04" "1") != std::string::npos);  // 見出しの下線トグル開始
  CHECK(expanded.find("\x03" "1") != std::string::npos);  // "bold"の太字トグル開始
}

TEST_CASE("markdownToHelpMarkup: bold MAY now contain italic, thanks to the CommonMark-style "
          "delimiter-stack emphasis resolution (2026-09-22) plus PipeBuffer's own "
          "currentBold_/currentUnderline_ toggle support -- both nesting directions work") {
  CHECK(markdownToHelpMarkup("**bold *and italic* still bold**") == "<b>bold <i>and italic</i> still bold</b>");
}

TEST_CASE("markdownToHelpMarkup: italic may contain bold, using either the same or a "
          "different delimiter character -- the CommonMark delimiter-stack algorithm "
          "correctly distinguishes '*' runs used for emphasis vs. strong emphasis even "
          "when mixed on the same line (previously a known limitation of the naive "
          "greedy single-pass matcher)") {
  CHECK(markdownToHelpMarkup("_italic **bold** italic_") == "<i>italic <b>bold</b> italic</i>");
  CHECK(markdownToHelpMarkup("*italic **bold** italic*") == "<i>italic <b>bold</b> italic</i>");
}

TEST_CASE("markdownToHelpMarkup: CommonMark's underscore intraword rule -- '_' inside a word "
          "(letters on both sides, no adjacent whitespace/punctuation) does NOT trigger "
          "emphasis, matching real-world identifiers like foo_bar_baz") {
  CHECK(markdownToHelpMarkup("foo_bar_baz") == "foo_bar_baz");
}

TEST_CASE("markdownToHelpMarkup: the CommonMark 'rule of 3' resolves an otherwise-ambiguous "
          "mix of single and double asterisks correctly") {
  CHECK(markdownToHelpMarkup("**foo*bar***") == "<b>foo<i>bar</i></b>");
}

// markdownToHelpMarkup()自身の2026-09-25のコメント参照("<lt>"エスケープ)。
TEST_CASE("markdownToHelpMarkup: literal <b>/<u>/<i> in the source (plain or in a code span) stay literal text") {
  std::string expanded = fm::expandHelpMarkup(fm::markdownToHelpMarkup("tags <b>/<u> and `<i>` here\n"));
  CHECK(expanded.find("<b>/<u>") != std::string::npos);
  CHECK(expanded.find("<i>") != std::string::npos);
  CHECK(expanded.find('\x03') == std::string::npos);  // 太字の切り替えバイトが出ていない
}
