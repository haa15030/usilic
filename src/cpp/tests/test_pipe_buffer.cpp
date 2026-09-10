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

// 2026-09-10更新、直接のユーザー提案: タブは「U+2060(WORD JOINER、
// 幅0)を1つ差し込んでから、従来通りの個数のスペースを展開する」形に
// 変わった。U+2060はUtf8::display_width()で幅0と定義されている
// (2026-09-10にU+200Bから差し替え済み)ため、見た目の桁位置は今まで
// 通りのスペース展開と完全に同じまま――違いは、GREP/CUT(いずれも
// このtext自体を検索対象にする)が、生のU+2060バイトを介してタブの
// あった位置を見つけられるようになったこと(grep_mode.cpp/cut_mode.cpp
// 自身のconvertTabsForSearch()参照)。
TEST_CASE("tab advances to the next 8-column stop, expanded to a zero-width U+2060 marker "
          "followed by spaces") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  buf.feed("a\tb\n");
  REQUIRE(buf.lines().size() == 1);
  // The tab byte itself is never kept raw -- ported from disp.c's own
  // "convert '\t' to ' '" comment. Unlike a real terminal, ContentView
  // renders each Line's bytes glyph-by-glyph itself rather than
  // forwarding raw control bytes to an actual terminal, so a surviving
  // raw tab byte would hit its control-code (K_at/K_dl) reverse-video
  // path instead of expanding. The U+2060 (e2 81 a0) marks where the
  // tab was, invisibly, alongside the usual space padding.
  CHECK(buf.lines()[0].text == "a\xE2\x81\xA0       b");
}

TEST_CASE("tab that reaches the wrap width forces a wrap") {
  PipeBuffer buf(Charset::Encoding::Ascii, 8, 1 << 20);
  buf.feed("ab\tcd\n");  // "ab" (col 2) + tab -> col 8 (the field width) -> wrap
  REQUIRE(buf.lines().size() == 2);
  CHECK(buf.lines()[0].text == "ab\xE2\x81\xA0      ");
  CHECK(buf.lines()[0].endsWithNewline == false);
  CHECK(buf.lines()[1].text == "cd");
}

// 2026-09-05に追加していた「行末のタブだけは生バイトのまま残す」と
// いう特例(直接のユーザー報告"^Q^Iでは入ってもgrepでヒットせず、
// 一覧にもハイライトの「^I」とは表示されない"への場当たり的な対処)
// は、2026-09-10、全位置のタブが同じU+2060方式に統一されたことで
// 不要になったため削除した。行末のタブも、行途中のタブ(上の2つの
// テスト)と全く同じ扱いを受ける。
TEST_CASE("a trailing tab (immediately before the newline) gets the same U+2060+spaces "
          "treatment as a mid-line tab -- no longer kept as a raw 0x09 byte") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  buf.feed("ab\t\n");
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "ab\xE2\x81\xA0      ");
}

TEST_CASE("a trailing tab at the very end of input with no newline at all (flush(), an "
          "unterminated final line) gets the same treatment") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  buf.feed("ab\t");
  buf.flush();
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "ab\xE2\x81\xA0      ");
  CHECK(buf.lines()[0].endsWithNewline == false);
}

TEST_CASE("a trailing tab still forces a wrap when it lands exactly at the field width, same "
          "as any other single-width character") {
  PipeBuffer buf(Charset::Encoding::Ascii, 3, 1 << 20);
  buf.feed("ab\t\n");  // "ab" (col 2) + trailing tab (col 3, == width) -> wraps
  REQUIRE(buf.lines().size() == 1);  // the trailing newline is absorbed into the same finalize
  CHECK(buf.lines()[0].text == "ab\xE2\x81\xA0 ");
  CHECK(buf.lines()[0].endsWithNewline == true);
}

// 2026-09-10追加、直接のユーザー提案の核心部分: GREP/CUTが検索対象に
// するのはこのtext自身なので、タブがまさに検索可能になったことを
// ここで直接確認しておく(実際の検索側の変換はgrep_mode.cpp/
// cut_mode.cppのconvertTabsForSearch()、統合テストはtest_grep_mode.cpp/
// test_cut_mode.cpp参照)。
TEST_CASE("the U+2060 marker left behind by a tab is a real, searchable byte sequence in text") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  buf.feed("a\tb\n");
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text.find("\xE2\x81\xA0") != std::string::npos);
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
//
// 2026-09-05訂正、実機での実際の再現(QUICK.txt由来のexplanationを
// 実際に表示して発覚、直接のユーザー報告"禁則処理が利いていないの
// と、一文字欠落がある")を受けて、以下のテスト群の期待値を作り直した。
// 以前の実装は「たとえ行が予約列幅を1文字分超えることになっても
// 押し込む」(幅超過を許容)方式だったが、-g(GraphicalDisplay)は
// columns()ぴったりの大きさのセル格子しか持たず、その超過分のセルへ
// は本質的に一切描画できない(境界チェックで黙って切り捨てられる)と
// 判明したため、本来の日本語組版の「追い出し」――直前の「普通の」
// 文字を今の行から取り除いて次の行へ戻し、その分だけ空いた幅へ禁則
// 対象の文字を収める――に作り直した(popLastCharForKinsoku()参照)。
// これにより行がwidth_を超えることはもう無い。
TEST_CASE("kinsoku: a forbidden line-start character (U+3002 IDEOGRAPHIC FULL STOP, \"\xe3\x80\x82\") "
          "is kept off the start of a line by giving up the PRECEDING line's own last character "
          "to the next line (proper oshidashi/reflow), never letting any line exceed the "
          "configured width") {
  PipeBuffer buf(Charset::Encoding::Utf8, 3, 1 << 20);
  buf.feed("AB\xe3\x80\x82"
           "CD\n");  // "AB。CD"
  REQUIRE(buf.lines().size() == 3);
  CHECK(buf.lines()[0].text == "A");
  CHECK(buf.lines()[1].text == "B\xe3\x80\x82");  // "B" reflowed here to make room for "。"
  CHECK(buf.lines()[2].text == "CD");
}

// 2026-09-04追加、実機での実際の再現(初回のsrc/HELP.txt全面改稿を
// 実際に流し込んで発覚): 禁則の「追い出し」によって、段落の最後の
// 文字がちょうど行の残り幅を使い切ってしまうと、その直後に続く本物の
// 改行が、既に確定済みのwrapText_に対してさらにfinalize(true)を起こし、
// 見せかけの空行を生んでいた――ここでは width=3、"B。"(=作り直した
// 「追い出し」で反映された行、ちょうど幅ぴったり)の直後に本物の改行が
// 続くケースを直接構成して確認する。
TEST_CASE("kinsoku: when the reflowed line is IMMEDIATELY followed by a real newline, "
          "that newline is absorbed into the SAME line (endsWithNewline=true) instead of "
          "producing a spurious extra blank line") {
  PipeBuffer buf(Charset::Encoding::Utf8, 3, 1 << 20);
  buf.feed("AB\xe3\x80\x82"
           "\nCD\n");  // "AB。" then a REAL newline, then "CD"
  REQUIRE(buf.lines().size() == 3);
  CHECK(buf.lines()[0].text == "A");
  CHECK(buf.lines()[1].text == "B\xe3\x80\x82");
  CHECK(buf.lines()[1].endsWithNewline == true);  // the real newline was absorbed here
  CHECK(buf.lines()[2].text == "CD");
}

TEST_CASE("kinsoku: the same newline-absorption still works when a zero-width \\x01/\\x02 "
          "marker (e.g. an </indent> close tag) sits between the reflowed line and "
          "the real newline -- the exact shape src/HELP.txt's own \"...できます。</indent>\" "
          "produces, and the marker's own state update (currentIndent_) still applies") {
  PipeBuffer buf(Charset::Encoding::Utf8, 3, 1 << 20);
  std::string raw = "AB\xe3\x80\x82";
  raw.push_back(1);
  raw.push_back('0');  // \x01 0 -- an </indent> close, right where HELP.txt's own shape has it
  raw += "\nCD\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 3);
  CHECK(buf.lines()[0].text == "A");
  CHECK(buf.lines()[1].text == "B\xe3\x80\x82");
  CHECK(buf.lines()[1].endsWithNewline == true);
  CHECK(buf.lines()[2].text == "CD");
}

// 2026-09-05追加、直接のユーザー報告(実際のQUICK.txt由来のexplanation
// を流し込んで発覚、スクリーンショット――「。」が単独の行として
// 残っていた): 上のテスト群は、禁則対象の文字「自身」が幅超過を
// 引き起こす場合(isForbiddenAtLineStart()の「追い出し」分岐が直接
// 発動する)だけを確認していたが、もっとありふれた実際のケースを
// 見落としていた――直前の文字がちょうど残り幅を「ぴったり」使い
// 切ってfinalize()され、次の(禁則対象の)文字が真新しい行の先頭
// (wrapCol_==0)から始まる場合。この時はどの文字を置いても
// 「幅を超える」ことには一度もならない(0+2>widthは通常false)ので、
// 上のどちらのkinsoku分岐も一度も発動せず、「。」が新しい行の先頭に
// 単独で残っていた。
TEST_CASE("kinsoku: a forbidden character is ALSO reflowed when the PRECEDING character "
          "happened to fill the line to EXACTLY the configured width (not overflow it) -- the "
          "forbidden character never itself triggers the width-overflow branch in this shape") {
  PipeBuffer buf(Charset::Encoding::Utf8, 4, 1 << 20);
  // "AB" (2 cols) + "あ" (2 cols) lands at EXACTLY width=4 -- not an overflow.
  // "。" (forbidden) immediately follows; without the fix it would start a
  // fresh line entirely alone (wrapCol_==0, 0+2>4 is false, no kinsoku check
  // ever fires).
  buf.feed("AB\xe3\x81\x82"
           "\xe3\x80\x82"
           "CD\n");  // "ABあ。CD"
  REQUIRE(buf.lines().size() == 3);
  CHECK(buf.lines()[0].text == "AB");
  CHECK(buf.lines()[1].text == "\xe3\x81\x82\xe3\x80\x82");  // "あ。" -- "あ" reflowed here
  CHECK(buf.lines()[2].text == "CD");
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
