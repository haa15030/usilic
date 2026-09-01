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
