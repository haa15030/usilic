#include "doctest.h"
#include "help_text.hpp"

// 2026-09-04追加、直接のユーザー依頼("行頭にパディングする全角
// スペースの個数を指定するだけのusilic特化マークアップ書式"):
// expandHelpMarkup()自身の<indentN>...</indent>展開のテスト。
// PipeBuffer::wrapLine()側の実際の消費(折り返し継続行が本当に
// 全角スペースで字下げされるか)はtest_pipe_buffer.cppで確認する
// ――ここではexpandHelpMarkup()が正しい制御バイト列を生成するかだけ
// を単体で確認する。

TEST_CASE("expandHelpMarkup: <indentN>...</indent> becomes \\x01<N>...\\x010, leaving the "
          "content between the tags untouched") {
  std::string out = fm::expandHelpMarkup("a<indent2>bcd</indent>e");
  CHECK(out == "a\x01" "2bcd\x01" "0e");
}

TEST_CASE("expandHelpMarkup: <indentN> nests correctly with <b>/<u> inside its span") {
  std::string out = fm::expandHelpMarkup("<indent3>x<b>y</b>z</indent>");
  // <b>y</b> still expands to its own per-character overstrike "y\by" as usual;
  // only the indent tags themselves become the \x01 control bytes.
  CHECK(out == "\x01" "3x" "y\by" "z\x01" "0");
}

TEST_CASE("expandHelpMarkup: a digit 1-9 is accepted, and unrelated text around the tags "
          "passes through unchanged") {
  std::string out = fm::expandHelpMarkup("head <indent9>mid</indent> tail");
  CHECK(out == "head \x01" "9mid\x01" "0 tail");
}

// 2026-09-04追加、直接のユーザー報告("src/HELP.txtは冒頭でちゃんと
// 分割文字列がマッチしているのに`--`エリアがあるのはおかしい"):
// kHelpTextJa等の生文字列リテラルは`R"HELPTXT(`の直後で改行してから
// 本文を書き始める慣習のせいで常に先頭に1個の余分な空行を持っている
// ――CutMode::split()自身の"--"区間(kHelpPattern自身の最初のマッチ
// より前に何か内容があると生成される)から見ると、これが「本物の、
// 章1より前の内容」に見えてしまい、見せかけの空エントリを作って
// いた。この1個の先頭改行だけを読み飛ばすことで解消する。
TEST_CASE("expandHelpMarkup: strips exactly one leading newline (the raw-string-literal "
          "authoring artifact), so text starting with \"\\n  1.\" behaves identically to "
          "text starting with \"  1.\" -- fixes a spurious leading \"--\" CutMode segment") {
  CHECK(fm::expandHelpMarkup("\n  1.  Intro") == fm::expandHelpMarkup("  1.  Intro"));
  CHECK(fm::expandHelpMarkup("\n  1.  Intro") == "  1.  Intro");
}

TEST_CASE("expandHelpMarkup: only ONE leading newline is stripped -- a second, presumably "
          "intentional, blank line is left alone") {
  std::string out = fm::expandHelpMarkup("\n\n  1.  Intro");
  CHECK(out == "\n  1.  Intro");
}

TEST_CASE("expandHelpMarkup: a leading newline elsewhere than the very first byte is left "
          "alone (only byte 0 is special-cased)") {
  std::string out = fm::expandHelpMarkup("a\nb");
  CHECK(out == "a\nb");
}

// 2026-09-04追加、直接のユーザー依頼("今後は<i>プログラム名</i>で
// 記載したいので対応よろしく"): <i>...</i>(斜体)自身のexpandHelpMarkup()
// テスト。PipeBuffer::wrapLine()側の実消費(実際にDisplay::
// Attribute::Italicが付くか)はtest_pipe_buffer.cppで確認する。
TEST_CASE("expandHelpMarkup: <i>...</i> becomes \\x021...\\x020, leaving the content between "
          "the tags untouched (no per-character doubling, unlike <b>/<u>)") {
  std::string out = fm::expandHelpMarkup("a<i>bcd</i>e");
  CHECK(out == "a\x02" "1bcd\x02" "0e");
}

TEST_CASE("expandHelpMarkup: <i> and <b>/<u> can be used independently in the same text "
          "without interfering with each other's control bytes") {
  std::string out = fm::expandHelpMarkup("<i>x</i><b>y</b>");
  CHECK(out == "\x02" "1x\x02" "0" "y\by");
}
