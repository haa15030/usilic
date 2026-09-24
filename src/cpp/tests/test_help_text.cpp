#include "doctest.h"
#include "help_text.hpp"

// 2026-09-04追加、直接のユーザー依頼("行頭にパディングする全角
// スペースの個数を指定するだけのusilic特化マークアップ書式"):
// expandHelpMarkup()自身の<indentN>...</indent>展開のテスト。
// PipeBuffer::wrapLine()側の実際の消費(折り返し継続行が本当に
// 半角スペースで字下げされるか)はtest_pipe_buffer.cppで確認する
// ――ここではexpandHelpMarkup()が正しい制御バイト列を生成するかだけ
// を単体で確認する。
//
// 2026-09-12修正、直接のユーザー依頼("<indentN>の設定を<indentNN>
// （二桁対応）にして"): \x01の直後は、元のタグの数字を1〜2文字その
// ままASCII数字文字として転写したもの(help_text.cpp自身のコメント
// 参照、本物のCtrl-A(0x01)との誤認を避けるため)――このindentMarker()
// ヘルパーは値を10進数の文字列へ変換して連結するだけの薄いラッパー。
namespace {
// 2026-09-21修正、直接のユーザー報告("<indent?>タグにバグがある。
// 直後に数字があると正しく字下げしない...特に<indent8>0x7fでは
// 暴走"): help_text.cpp自身の同日の修正コメント参照――転写する数字を
// 固定2桁(0埋め)にしたので、このテストヘルパーも同じ0埋め形式に
// 合わせる。
std::string indentMarker(int value) {
  std::string digits = std::to_string(value);
  if (digits.size() == 1) digits = "0" + digits;
  return "\x01" + digits;
}
}  // namespace

TEST_CASE("expandHelpMarkup: <indentN>...</indent> becomes \\x01<value>...\\x01\\x00, leaving "
          "the content between the tags untouched") {
  std::string out = fm::expandHelpMarkup("a<indent2>bcd</indent>e");
  CHECK(out == "a" + indentMarker(2) + "bcd" + indentMarker(0) + "e");
}

TEST_CASE("expandHelpMarkup: <indentN> nests correctly with <b>/<u> inside its span "
          "(2026-09-22: <b>/<u> now use the same toggle-marker encoding as <i>, see "
          "pipe_buffer.hpp's own currentBold_/currentUnderline_ comment for why)") {
  std::string out = fm::expandHelpMarkup("<indent3>x<b>y</b>z</indent>");
  CHECK(out == indentMarker(3) + "x" "\x03" "1y\x03" "0" "z" + indentMarker(0));
}

TEST_CASE("expandHelpMarkup: a digit 1-9 is accepted, and unrelated text around the tags "
          "passes through unchanged") {
  std::string out = fm::expandHelpMarkup("head <indent9>mid</indent> tail");
  CHECK(out == "head " + indentMarker(9) + "mid" + indentMarker(0) + " tail");
}

// 2026-09-12追加、直接のユーザー依頼("<indentN>の設定を<indentNN>
// （二桁対応）にして"): 2桁(10-99)も正しく1バイトの値として運ばれる
// ことを確認する。
TEST_CASE("expandHelpMarkup: a TWO-digit N (10-99) is accepted, encoded as a single raw byte "
          "carrying the value itself (not two ASCII digit characters)") {
  std::string out = fm::expandHelpMarkup("<indent16>mid</indent>");
  CHECK(out == indentMarker(16) + "mid" + indentMarker(0));
  // 3桁目があると、2桁ちょうどの直後に">"が来ないためタグとして認識
  // されず(numEnd自身の"2桁まで"上限参照)、"<indent123>"はそのまま
  // 素通りする(一致しなかったタグは単なるプレーンテキスト)――
  // </indent>自体は独立してマッチするので、そちらは通常通り閉じる。
  std::string threeDigits = fm::expandHelpMarkup("<indent123>x</indent>");
  CHECK(threeDigits == "<indent123>x" + indentMarker(0));
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
          "without interfering with each other's control bytes (2026-09-22: <b> now uses "
          "\\x03 toggle bytes, same shape as <i>'s \\x02)") {
  std::string out = fm::expandHelpMarkup("<i>x</i><b>y</b>");
  CHECK(out == "\x02" "1x\x02" "0" "\x03" "1y\x03" "0");
}

TEST_CASE("expandHelpMarkup: <b> may now safely nest <i>/<u>/<indentN> inside it (2026-09-22, "
          "direct user request \"CommonMarkの強調解決アルゴリズム対応が普通なら実装して"
          "ほしい\" -- <b>/<u> switched from a raw-substring, non-recursive per-character "
          "overstrike encoding to the same non-consuming toggle-marker approach as <i>, so "
          "nested tags inside <b>/<u> are no longer swallowed as literal text)") {
  std::string out = fm::expandHelpMarkup("<b>bold <i>and italic</i> still bold</b>");
  CHECK(out == "\x03" "1bold \x02" "1and italic\x02" "0 still bold\x03" "0");
}

// 2026-09-04追加、実機での実際の再現(初回のsrc/HELP.txt全面改稿を
// 実際に流し込んで発覚): 複数行の<indentN>...</indent>を読みやすく
// 書く実際の著者の書き方("<indentN>\n本文...\n</indent>")では、
// タグ自身の直後/直前の改行は純粋にソース側の可読性のためのもので
// あり、意図した本文の改行ではない――これをそのまま素通りさせると
// PipeBuffer::wrapLine()側で見せかけの空行を生む(pipe_buffer.hpp自身の
// currentIndent_のコメント参照)。開始タグ直後・終了タグ直前の改行は
// それぞれ1個だけ読み飛ばす。
TEST_CASE("expandHelpMarkup: the newline immediately after an <indentN> OPEN tag, and the "
          "newline immediately before a </indent> CLOSE tag, are both trimmed (pure "
          "source-readability newlines, not intended content)") {
  std::string out = fm::expandHelpMarkup("<indent4>\nbody\n</indent>");
  CHECK(out == indentMarker(4) + "body" + indentMarker(0));
}

// 2026-09-21追加、直接のユーザー報告("<indent?>タグにバグがある。
// 直後に数字があると正しく字下げしない。<indent2>1. Introductionとか。
// 特に<indent8>0x7fでは暴走"): 単一桁の値(0-9)を0埋めせずそのまま
// 転写していたため、デコーダ(pipe_buffer.cpp自身のreadIndentDigits())
// の「\x01の直後、最大2文字の連続した数字を読む」処理が、本文側の
// 最初の数字文字を値の2桁目だと誤読んでいた――本文の先頭の数字が
// 消えて字下げ値も誤った大きな値になる。0埋め修正後は、本文側の
// 数字がいくつ続いていても、常にちょうど2文字だけがマーカーとして
// 消費され、本文自体は一切変質しないことを確認する。
TEST_CASE("expandHelpMarkup: a SINGLE-digit indent value immediately followed by body text that "
          "itself starts with a digit does not swallow that digit into the encoded value "
          "(regression: <indent2>1. Introduction used to decode as value 21)") {
  std::string out = fm::expandHelpMarkup("<indent2>1. Introduction</indent>");
  CHECK(out == indentMarker(2) + "1. Introduction" + indentMarker(0));
}

TEST_CASE("expandHelpMarkup: same fix for a larger single-digit-prone case (<indent8>0x7f), "
          "which used to be misread as value 80 and hang PipeBuffer's word-wrap") {
  std::string out = fm::expandHelpMarkup("<indent8>0x7f</indent>");
  CHECK(out == indentMarker(8) + "0x7f" + indentMarker(0));
}

TEST_CASE("expandHelpMarkup: only the newline DIRECTLY adjacent to the tag is trimmed -- a "
          "second (genuinely intentional) blank line right after the open tag survives") {
  std::string out = fm::expandHelpMarkup("<indent2>\n\nbody</indent>");
  CHECK(out == indentMarker(2) + "\nbody" + indentMarker(0));
}

TEST_CASE("expandHelpMarkup: when </indent> is NOT preceded by a newline (it closes on the "
          "same line as the content), nothing is trimmed") {
  std::string out = fm::expandHelpMarkup("<indent2>body</indent>");
  CHECK(out == indentMarker(2) + "body" + indentMarker(0));
}
