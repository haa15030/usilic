#include "charset.hpp"
#include "doctest.h"
#include "pipe_buffer.hpp"

using fm::Charset;
using fm::Display;
using fm::PipeBuffer;

TEST_CASE("plain ASCII: single short line") {
  // Ported from legacy PP::cs: the trailing newline byte itself is
  // never stored in a line's text -- endsWithNewline (legacy
  // PP::f.nl) conveys that separately, matching how put_str() only
  // ever renders one line's worth of content at a time.
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  buf.feed("hello\n");
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "hello");
  CHECK(buf.lines()[0].endsWithNewline == true);
  CHECK(buf.lines()[0].attributes.empty());
}

TEST_CASE("plain ASCII: wraps at the configured width") {
  PipeBuffer buf(Charset::Encoding::Ascii, 5, 1 << 20);
  buf.feed("abcdefgh\n");
  REQUIRE(buf.lines().size() == 2);
  CHECK(buf.lines()[0].text == "abcde");
  CHECK(buf.lines()[0].endsWithNewline == false);
  CHECK(buf.lines()[1].text == "fgh");
  CHECK(buf.lines()[1].endsWithNewline == true);
}

TEST_CASE("feed() can be called with arbitrarily small chunks (streaming)") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  for (char c : std::string("hello\n")) buf.feed(std::string(1, c));
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "hello");
}

TEST_CASE("flush() finalizes a trailing line with no newline") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  buf.feed("no newline here");
  CHECK(buf.lines().empty());  // still pending, nothing finalized yet
  buf.flush();
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "no newline here");
  CHECK(buf.lines()[0].endsWithNewline == false);
}

TEST_CASE("tab advances to the next 8-column stop, expanded to spaces") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  buf.feed("a\tb\n");
  REQUIRE(buf.lines().size() == 1);
  // The tab byte is expanded to literal spaces here, NOT kept raw --
  // ported from disp.c's own "convert '\t' to ' '" comment. Unlike a
  // real terminal, ContentView renders each Line's bytes glyph-by-
  // glyph itself rather than forwarding raw control bytes to an
  // actual terminal, so a surviving raw tab byte would hit its
  // control-code (K_at/K_dl) reverse-video path instead of expanding.
  CHECK(buf.lines()[0].text == "a       b");
}

TEST_CASE("tab that reaches the wrap width forces a wrap") {
  PipeBuffer buf(Charset::Encoding::Ascii, 8, 1 << 20);
  buf.feed("ab\tcd\n");  // "ab" (col 2) + tab -> col 8 (the field width) -> wrap
  REQUIRE(buf.lines().size() == 2);
  CHECK(buf.lines()[0].text == "ab      ");
  CHECK(buf.lines()[0].endsWithNewline == false);
  CHECK(buf.lines()[1].text == "cd");
}

TEST_CASE("overBudget() trips once total content exceeds maxBytes") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 5);
  CHECK(buf.overBudget() == false);
  buf.feed("abcdefghij\n");
  CHECK(buf.overBudget() == true);
}

// The overstrike fixtures below are real bytes extracted from this
// repository's own src/doc/README.jp (EUC-JP, nroff-style overstrike
// bold/underline -- see CLAUDE.md history item 8), not hand-written
// guesses. The heading being decoded is "1. 概要" ("1. Overview")
// with "1", ".", and the two kanji each individually bolded via
// backspace overstrike, and the underlined word is "tcb" (README.jp's
// own historical prose still says the legacy program's old name here;
// deliberately left untouched by the 2026-08-30 tcb->usilic rename).

TEST_CASE("EUC overstrike: ASCII bold via single backspace (real README.jp bytes)") {
  PipeBuffer buf(Charset::Encoding::Euc, 80, 1 << 20);
  // "1" BS "1" "." BS "." -- bold "1", bold "."
  std::string raw = "1" "\x08" "1" "." "\x08" "." "\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "1.");
  REQUIRE(buf.lines()[0].attributes.size() == 2);
  CHECK(buf.lines()[0].attributes[0].start == 0);
  CHECK(buf.lines()[0].attributes[0].end == 1);
  CHECK(buf.lines()[0].attributes[0].attribute == Display::Attribute::Bold);
  CHECK(buf.lines()[0].attributes[1].start == 1);
  CHECK(buf.lines()[0].attributes[1].end == 2);
  CHECK(buf.lines()[0].attributes[1].attribute == Display::Attribute::Bold);
}

TEST_CASE("EUC overstrike: 2-byte kanji bold via double backspace (real README.jp bytes)") {
  PipeBuffer buf(Charset::Encoding::Euc, 80, 1 << 20);
  // "\xb3\xb5" (概) BS BS "\xb3\xb5", "\xcd\xd7" (要) BS BS "\xcd\xd7"
  std::string raw = "\xb3\xb5" "\x08\x08" "\xb3\xb5" "\xcd\xd7" "\x08\x08" "\xcd\xd7" "\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "\xb3\xb5\xcd\xd7");  // 概要
  REQUIRE(buf.lines()[0].attributes.size() == 2);
  CHECK(buf.lines()[0].attributes[0].start == 0);
  CHECK(buf.lines()[0].attributes[0].end == 2);
  CHECK(buf.lines()[0].attributes[0].attribute == Display::Attribute::Bold);
  CHECK(buf.lines()[0].attributes[1].start == 2);
  CHECK(buf.lines()[0].attributes[1].end == 4);
  CHECK(buf.lines()[0].attributes[1].attribute == Display::Attribute::Bold);
}

TEST_CASE("EUC overstrike: underline via single backspace + underscore (real README.jp bytes)") {
  PipeBuffer buf(Charset::Encoding::Euc, 80, 1 << 20);
  // "_" BS "t", "_" BS "c", "_" BS "b" -- underlined "tcb" (src/README.jp
  // still spells the legacy program's old name here; unrelated to the
  // 2026-08-30 tcb->usilic source-level rename, since README.jp's own
  // historical prose is deliberately left untouched -- see CLAUDE.md)
  std::string raw = "_" "\x08" "t" "_" "\x08" "c" "_" "\x08" "b" "\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "tcb");
  REQUIRE(buf.lines()[0].attributes.size() == 3);
  for (int i = 0; i < 3; i++) {
    CHECK(buf.lines()[0].attributes[i].start == i);
    CHECK(buf.lines()[0].attributes[i].end == i + 1);
    CHECK(buf.lines()[0].attributes[i].attribute == Display::Attribute::Underline);
  }
}

TEST_CASE("UTF-8 overstrike: same kanji-bold source, converted through euc_to_utf8 first") {
  // Confirms the UTF-8-aware width fix from CLAUDE.md history item 8
  // (A_md2 assuming a fixed 2-byte repeat, corrupting 3-byte UTF-8
  // CJK characters) -- reuses Charset::euc_to_utf8 (already tested on
  // its own) to build a real UTF-8 overstrike source rather than
  // hand-computing UTF-8 bytes.
  unsigned char euc[] = {0xb3, 0xb5, 0x08, 0x08, 0xb3, 0xb5, '\n'};  // 概 BS BS 概
  auto utf8Bytes = Charset::euc_to_utf8(euc, sizeof(euc));
  std::string raw(utf8Bytes.begin(), utf8Bytes.end());

  PipeBuffer buf(Charset::Encoding::Utf8, 80, 1 << 20);
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  REQUIRE(buf.lines()[0].attributes.size() == 1);
  CHECK(buf.lines()[0].attributes[0].start == 0);
  CHECK(buf.lines()[0].attributes[0].end == 3);  // 概 is 3 bytes in UTF-8
  CHECK(buf.lines()[0].attributes[0].attribute == Display::Attribute::Bold);
  // "概" itself (3 UTF-8 bytes), kept once; no trailing newline byte.
  CHECK(buf.lines()[0].text.size() == 3);
}

TEST_CASE("control bytes pass through as literal bytes (ContentView highlights them, not PipeBuffer)") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  std::string raw = "a";
  raw.push_back(1);  // Ctrl-A
  raw += "b";
  buf.feed(raw + "\n");
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == raw);
  CHECK(buf.lines()[0].attributes.empty());
}

// 2026-09-04追加、直接のユーザー依頼("行頭にパディングする全角
// スペースの個数を指定するだけのusilic特化マークアップ書式"、
// EPUBの<div class="indent_1em">相当): expandHelpMarkup()
// (help_text.hpp/cpp)が生成する\x01+数字1桁の制御バイトの、
// PipeBuffer::wrapLine()側の実消費テスト。expandHelpMarkup()自身の
// 変換ロジックはtest_help_text.cppで別途確認済み――ここではその
// 制御バイト列を直接手で構成し、折り返し継続行が実際に全角スペース
// で字下げされるかどうかだけを見る。
TEST_CASE("\\x01<N> indent marker: a wrapped CONTINUATION line is padded with N ideographic "
          "spaces (U+3000); text already mid-line when the marker arrives is untouched "
          "(padding only ever applies at an actual line START, see seedIndentIfAtLineStart())") {
  PipeBuffer buf(Charset::Encoding::Ascii, 6, 1 << 20);
  std::string raw = "AB";
  raw.push_back(1);
  raw.push_back('1');  // \x01 1 -- begin indent of 1 ideographic space (2 columns)
  raw += "CDEFGH\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 2);
  // First line: "AB" was already mid-line (wrapCol_ != 0) when the marker
  // arrived, so no padding retroactively applies to it.
  CHECK(buf.lines()[0].text == "ABCDEF");
  CHECK(buf.lines()[0].endsWithNewline == false);
  // Second (wrapped continuation) line: padded with one U+3000
  // (0xE3 0x80 0x80 in UTF-8) before the remaining "GH".
  CHECK(buf.lines()[1].text == "\xE3\x80\x80GH");
  CHECK(buf.lines()[1].endsWithNewline == true);
}

// 2026-09-04追加、実機での実際の再現(初回のsrc/HELP.txt全面改稿を
// 実際に流し込んで発覚、pipe_buffer.hpp自身のcurrentIndent_の訂正
// コメント参照): 当初は「タグ自身の1行目には適用しない」つもり
// だったが、実際の著者の書き方("<indentN>本文...</indent>"、
// タグの直後に本文、手動の字下げ文字を一切伴わない)には合わない
// ――マーカーが「新しい行の最初の1文字」の直前に来る場合(=直前に
// finalize()があった、または入力の本当の先頭)は、タグ自身の1行目
// にも同じパディングが適用されるべきで、実際そうなる。
TEST_CASE("\\x01<N> indent marker: when the marker arrives right at a fresh line start "
          "(nothing placed yet since the last finalize, or the very start of input), the "
          "padding applies to THAT first line too -- not just later wrap-continuations") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  std::string raw;
  raw.push_back(1);
  raw.push_back('2');  // \x01 2 -- right at the very start, nothing placed yet
  raw += "AB\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "\xE3\x80\x80\xE3\x80\x80"
                                "AB");  // 2 ideographic spaces, then "AB"
}

TEST_CASE("\\x01<N> indent marker: \\x010 (N=0) turns the indent back off for any LATER wrap "
          "in the same feed") {
  PipeBuffer buf(Charset::Encoding::Ascii, 6, 1 << 20);
  std::string raw = "AB";
  raw.push_back(1);
  raw.push_back('1');   // begin indent
  raw += "CD";
  raw.push_back(1);
  raw.push_back('0');   // </indent> -- back to no indent
  raw += "EFGHIJ\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 2);
  CHECK(buf.lines()[0].text == "ABCDEF");
  // No leading ideographic space this time -- the indent was already
  // turned off before this wrap boundary was reached.
  CHECK(buf.lines()[1].text == "GHIJ");
}

// 2026-09-04追加、直接のユーザー依頼("禁則処理（句読点が行の冒頭に
// 来ない）を実施"): 行頭禁則の実wrapLine()消費テスト。
TEST_CASE("kinsoku: a forbidden line-start character (U+3002 IDEOGRAPHIC FULL STOP, \"\xe3\x80\x82\") "
          "is pushed onto the end of the PREVIOUS line instead of starting a new one, even "
          "though that overflows the configured width by one character") {
  PipeBuffer buf(Charset::Encoding::Utf8, 3, 1 << 20);
  buf.feed("AB\xe3\x80\x82"
           "CD\n");  // "AB。CD"
  REQUIRE(buf.lines().size() == 2);
  CHECK(buf.lines()[0].text == "AB\xe3\x80\x82");  // "。" pushed here despite overflowing width 3
  CHECK(buf.lines()[1].text == "CD");
}

// 2026-09-04追加、実機での実際の再現(初回のsrc/HELP.txt全面改稿を
// 実際に流し込んで発覚): 禁則の「追い出し」によって、段落の最後の
// 文字がちょうど行の残り幅を使い切ってしまうと、その直後に続く本物の
// 改行が、既に幅超過finalizeで空になったwrapText_に対してさらに
// finalize(true)を起こし、見せかけの空行を生んでいた――ここでは
// width=3、"AB。"(=禁則で追い出された"。"を含めてちょうど幅4分、
// つまり幅超過ぴったり)の直後に本物の改行が続くケースを直接構成
// して確認する。
TEST_CASE("kinsoku: when the pushed-onto line is IMMEDIATELY followed by a real newline, "
          "that newline is absorbed into the SAME line (endsWithNewline=true) instead of "
          "producing a spurious extra blank line") {
  PipeBuffer buf(Charset::Encoding::Utf8, 3, 1 << 20);
  buf.feed("AB\xe3\x80\x82"
           "\nCD\n");  // "AB。" then a REAL newline, then "CD"
  REQUIRE(buf.lines().size() == 2);  // NOT 3 -- no spurious blank line in between
  CHECK(buf.lines()[0].text == "AB\xe3\x80\x82");
  CHECK(buf.lines()[0].endsWithNewline == true);  // the real newline was absorbed here
  CHECK(buf.lines()[1].text == "CD");
}

TEST_CASE("kinsoku: the same newline-absorption still works when a zero-width \\x01/\\x02 "
          "marker (e.g. an </indent> close tag) sits between the pushed-onto character and "
          "the real newline -- the exact shape src/HELP.txt's own \"...できます。</indent>\" "
          "produces, and the marker's own state update (currentIndent_) still applies") {
  PipeBuffer buf(Charset::Encoding::Utf8, 3, 1 << 20);
  std::string raw = "AB\xe3\x80\x82";
  raw.push_back(1);
  raw.push_back('0');  // \x01 0 -- an </indent> close, right where HELP.txt's own shape has it
  raw += "\nCD\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 2);  // still NOT 3
  CHECK(buf.lines()[0].text == "AB\xe3\x80\x82");
  CHECK(buf.lines()[0].endsWithNewline == true);
  CHECK(buf.lines()[1].text == "CD");
}

TEST_CASE("kinsoku: an ordinary (non-forbidden) wide character wraps normally -- confirms the "
          "kinsoku table isn't matching too broadly") {
  PipeBuffer buf(Charset::Encoding::Utf8, 3, 1 << 20);
  buf.feed("AB\xe3\x81\x82"
           "CD\n");  // "ABあCD" -- U+3042 HIRAGANA A is not a forbidden-at-line-start character
  REQUIRE(buf.lines().size() == 3);
  CHECK(buf.lines()[0].text == "AB");
  CHECK(buf.lines()[1].text == "\xe3\x81\x82"
                                "C");  // "あ" legitimately starts this line
  CHECK(buf.lines()[2].text == "D");
}

// 2026-09-04追加、直接のユーザー依頼("今後は<i>プログラム名</i>で
// 記載したいので対応よろしく"): expandHelpMarkup()が<i>...</i>から
// 生成する制御バイト\x02+数字1桁の、PipeBuffer::wrapLine()側の実
// 消費テスト。expandHelpMarkup()自身の変換ロジックはtest_help_text.cpp
// で別途確認済み。
TEST_CASE("\\x02<N> italic marker: characters between \\x021 and \\x020 each get their own "
          "Display::Attribute::Italic AttributeRun, matching Bold/Underline's own "
          "per-character granularity; characters outside the span do not") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  std::string raw = "A";
  raw.push_back(2);
  raw.push_back('1');  // \x02 1 -- begin italic
  raw += "bc";
  raw.push_back(2);
  raw.push_back('0');  // \x02 0 -- end italic
  raw += "D\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "AbcD");
  const auto &attrs = buf.lines()[0].attributes;
  REQUIRE(attrs.size() == 2);
  CHECK(attrs[0].start == 1);  // 'b'
  CHECK(attrs[0].end == 2);
  CHECK(attrs[0].attribute == Display::Attribute::Italic);
  CHECK(attrs[1].start == 2);  // 'c'
  CHECK(attrs[1].end == 3);
  CHECK(attrs[1].attribute == Display::Attribute::Italic);
}

// 2026-09-04追加、直接のユーザー報告("-cでは斜体にならない"=fk)を
// 調査して発見した実際のバグの修正。上のテストが確認する「素の」
// \x021...\x020は正しくItalicのAttributeRunを積んでいたが、その
// 経路(このファイルの「plain char path」)とは別に、太字/下線の
// オーバーストライク検出(char+BS+char、上の「keepStart」分岐)は
// これまでcurrentItalic_を一切見ていなかった――HELP.txt自身に実在
// する"<i><b>fgrep</b>(1)</i>"のような入れ子(<i>の中に<b>がある)で、
// 太字部分のItalic AttributeRunが丸ごと欠落し、実機の生バイト列
// キャプチャで確認済みの通り"fgrep"の直前に本来あるべき斜体ONの
// エスケープが送出されなくなっていた。この文字については、Bold用
// と全く同じ[start,end)範囲にItalic用のAttributeRunも「もう1つ」
// 積まれるべきである(複数のAttributeRunが同じ範囲に重なること
// 自体はContentView::renderLine()自身の消費ループが元々想定済み)。
TEST_CASE("\\x02<N> italic marker: a bold overstrike character (char+BS+char) INSIDE an "
          "\\x021...\\x020 span gets BOTH a Bold AttributeRun and an Italic AttributeRun over "
          "the same byte range, not just Bold") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  std::string raw;
  raw.push_back(2);
  raw.push_back('1');  // \x02 1 -- begin italic
  raw += "f";
  raw.push_back(8);  // backspace
  raw += "f";         // bold overstrike: "f" repeated via char+BS+char
  raw.push_back(2);
  raw.push_back('0');  // \x02 0 -- end italic
  raw += "\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "f");
  const auto &attrs = buf.lines()[0].attributes;
  REQUIRE(attrs.size() == 2);
  bool sawBold = false, sawItalic = false;
  for (const auto &a : attrs) {
    CHECK(a.start == 0);
    CHECK(a.end == 1);
    if (a.attribute == Display::Attribute::Bold) sawBold = true;
    if (a.attribute == Display::Attribute::Italic) sawItalic = true;
  }
  CHECK(sawBold);
  CHECK(sawItalic);
}
