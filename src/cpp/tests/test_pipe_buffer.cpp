#include "charset.hpp"
#include "doctest.h"
#include "help_text.hpp"
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

// 2026-09-12追加、直接のユーザー報告("QUICK.txt:237 ↑↓に^Hコードが
// 混入"): expandHelpMarkup()(help_text.cpp)自身の<b>/<u>エンコード
// ロジックは、文字の実バイト幅に関わらず常に「単一の」バックスペース
// 1個で重ね打ちを表す(そちら自身の「1文字ごとに重ね打ちを生成する」
// コメント参照)――legacyのEUC規約(1バイト文字は単一BS、2バイト幅
// 文字は二重BS、すぐ上の"UTF-8 overstrike"テスト参照)とは別物。
// 修正前は、単一BSの直後を0x80未満(ASCII)限定にしていたため、UTF-8の
// マルチバイト文字(↑=U+2191、3バイト)を"↑\b↑"という単一BSの重ね
// 打ちとして書いても認識できず、生のバックスペースがそのまま素通り
// し、画面には見た目上「文字+反転表示の^Hグリフ+文字」という壊れた
// 表示になっていた。
TEST_CASE("UTF-8 overstrike: a SINGLE backspace (not doubled) between two copies of a multi-byte "
          "UTF-8 character -- the shape expandHelpMarkup()'s own <b>/<u> encoding actually produces "
          "for ANY character regardless of byte width -- is still recognized as bold, not left as "
          "literal bytes with a visible backspace") {
  std::string raw = "\xe2\x86\x91\x08\xe2\x86\x91";  // "↑" BS "↑" (U+2191, single backspace)
  PipeBuffer buf(Charset::Encoding::Utf8, 80, 1 << 20);
  buf.feed(raw + "\n");
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "\xe2\x86\x91");  // kept once, no literal \x08 surviving
  REQUIRE(buf.lines()[0].attributes.size() == 1);
  CHECK(buf.lines()[0].attributes[0].start == 0);
  CHECK(buf.lines()[0].attributes[0].end == 3);
  CHECK(buf.lines()[0].attributes[0].attribute == Display::Attribute::Bold);
}

// 同上、実際のQUICK.txt自身の生成経路(expandHelpMarkup()を通す)で
// end-to-end確認する。
TEST_CASE("UTF-8 overstrike: end-to-end through expandHelpMarkup() -- \"<b>up-arrow</b>\" renders as "
          "a single bold arrow, not the arrow followed by a literal backspace control byte") {
  std::string expanded = fm::expandHelpMarkup("<b>\xe2\x86\x91</b>");
  PipeBuffer buf(Charset::Encoding::Utf8, 80, 1 << 20);
  buf.feed(expanded + "\n");
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "\xe2\x86\x91");
  REQUIRE(buf.lines()[0].attributes.size() == 1);
  CHECK(buf.lines()[0].attributes[0].attribute == Display::Attribute::Bold);
}

// EUC(非UTF-8)エンコーディングでは、上の修正の対象外(legacyの単一BS
// =1バイト限定という意味付けが引き続きそのまま有効であるべき)である
// ことの回帰確認――単一BSの直後が0x80以上(EUCの2バイト目)なら、
// 依然として重ね打ちとして認識せず素通しする。
TEST_CASE("EUC overstrike: a single backspace followed by a non-ASCII byte is still NOT recognized "
          "as bold under EUC encoding (the widened UTF-8 handling above must not leak into legacy's "
          "own single-BS-means-1-byte-only convention)") {
  std::string raw = "\xa4\xa2";
  raw.push_back('\b');
  raw += "\xa4\xa2";
  PipeBuffer buf(Charset::Encoding::Euc, 80, 1 << 20);
  buf.feed(raw + "\n");
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].attributes.empty());  // not recognized as overstrike -- passes through as-is
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
// 制御バイト列を直接手で構成し、折り返し継続行が実際に半角スペース
// で字下げされるかどうかだけを見る。
//
// 2026-09-12修正、直接のユーザー依頼("indentタグのスペース数指定を
// 全角ではなく半角にしてほしい"): <indentN>のNは全角スペースN個
// (1個あたり表示幅2、U+3000)から半角スペースN個(1個あたり表示幅1、
// ASCII 0x20)へ変更された――pipe_buffer.cpp自身のseedIndentIfAtLineStart()
// のコメント参照。このテスト自体の期待値も合わせて更新した。
//
// 2026-09-12再修正、直接のユーザー依頼("<indentN>の設定を<indentNN>
// （二桁対応）にして"): \x01の直後は引き続きASCII数字「文字」の
// まま(本物のCtrl-A(0x01)との誤認を避けるため、help_text.cpp/
// pipe_buffer.cpp自身のコメント参照)――1〜2桁まで許容するよう
// デコーダ側(readIndentDigits())を拡張した。
TEST_CASE("\\x01<N> indent marker: a wrapped CONTINUATION line is padded with N half-width "
          "ASCII spaces; text already mid-line when the marker arrives is untouched "
          "(padding only ever applies at an actual line START, see seedIndentIfAtLineStart())") {
  PipeBuffer buf(Charset::Encoding::Ascii, 6, 1 << 20);
  std::string raw = "AB";
  raw.push_back(1);
  raw.push_back('1');  // \x01 '1' -- begin indent of 1 half-width space (1 column)
  raw += "CDEFGH\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 2);
  // First line: "AB" was already mid-line (wrapCol_ != 0) when the marker
  // arrived, so no padding retroactively applies to it.
  CHECK(buf.lines()[0].text == "ABCDEF");
  CHECK(buf.lines()[0].endsWithNewline == false);
  // Second (wrapped continuation) line: padded with one ASCII space
  // before the remaining "GH".
  CHECK(buf.lines()[1].text == " GH");
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
  raw.push_back('2');  // \x01 '2' -- right at the very start, nothing placed yet
  raw += "AB\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "  AB");  // 2 half-width spaces, then "AB"
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

// 2026-09-12追加、直接のユーザー依頼("<indentN>の設定を<indentNN>
// （二桁対応）にして"): 2桁の数字文字列("12"、readIndentDigits()自身
// のコメント参照)が正しく10進数として組み立てられ、その個数ぶんの
// 半角スペースが実際に生成されることを確認する。
TEST_CASE("\\x01<N> indent marker: a TWO-digit value (e.g. 12) is read as two ASCII digit "
          "characters and produces that many half-width spaces, not just a single-digit "
          "subset of it") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  std::string raw;
  raw.push_back(1);
  raw += "12";  // \x01 "12" -- two ASCII digit characters, value 12
  raw += "AB\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == std::string(12, ' ') + "AB");
}

// 2026-09-21追加、直接のユーザー報告("<indent?>タグにバグがある。
// 直後に数字があると正しく字下げしない。<indent2>1. Introductionとか。
// 特に<indent8>0x7fでは暴走")+help_text.cpp自身の同日修正コメント
// 参照: expandHelpMarkup()側が単一桁の値を0埋めせず転写していたため、
// このデコーダのreadIndentDigits()が本文側の先頭の数字を値の2桁目
// だと誤読んでいた(<indent2>1. → \x01"2"+"1. "を\x01"21"と誤読)。
// help_text.cpp側の修正(常に固定2桁でエンコード)により、このデコーダ
// 自身は変更不要のまま正しく動くようになったことを確認する回帰
// テスト――\x01の直後に固定2桁"02"、続けて本文側の数字"1. Introduction"
// を置き、値が2(21ではない)と正しく読まれ、かつ本文の"1"が消えずに
// 残ることを確認する。
TEST_CASE("\\x01<N> indent marker: a fixed-width TWO-digit encoding (\"02\") followed by body "
          "text that itself starts with a digit reads the value as 2 (not 21), and the body's "
          "leading digit is preserved intact") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  std::string raw;
  raw.push_back(1);
  raw += "02";  // \x01 "02" -- fixed 2-digit encoding of value 2
  raw += "1. Introduction\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "  1. Introduction");  // 2 half-width spaces, body untouched
}

// 同上、より大きな値(8)+本文側の数字("0x7f")の組み合わせ――修正前は
// 値80(画面幅を超える)に誤読まれ、折り返し処理が無限ループ(暴走)して
// いた。ここでは単にhang/crashしないこと、かつ値が8だけ(80ではない)
// として正しく適用されることを確認する。
TEST_CASE("\\x01<N> indent marker: fixed-width \"08\" followed by \"0x7f\" reads the value as 8 "
          "(not 80, which used to overflow the wrap width and hang)") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  std::string raw;
  raw.push_back(1);
  raw += "08";  // \x01 "08" -- fixed 2-digit encoding of value 8
  raw += "0x7f\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "        0x7f");  // 8 half-width spaces, body untouched
}

// 2026-09-12追加、同上: 本物のCtrl-A(0x01)の直後に非数字バイトが
// 続く場合は、依然として(safe-net、"control bytes pass through as
// literal bytes"と同じ理屈)自分のマーカーとして誤認せずそのまま
// 素通りすることを確認する――2桁対応の実装(readIndentDigits())が
// この安全網を壊していないことの回帰テスト。
TEST_CASE("\\x01<N> indent marker: \\x01 NOT followed by any digit is left as a literal "
          "control byte, not misread as a (zero-length) indent marker") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  std::string raw = "a";
  raw.push_back(1);
  raw += "b\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  std::string expected = "a";
  expected.push_back(1);
  expected += "b";
  CHECK(buf.lines()[0].text == expected);
}

// 2026-09-20追加のち同日撤回、直接のユーザー報告("indentタグは...
// すべての行頭がindent位置に揃ってないと意味が無い"): ここには当初、
// 「行頭の空白から自動的に字下げ幅を検出する」汎用ヒューリスティックの
// テストがあったが、直後のユーザー指摘("小説の下書き文書...パラグラフ
// の出だしを字下げするルールがある。今の方法だと全体が１文字下がって
// しまう")により、その方式自体が誤りと判明し撤回した――<indentN>は
// 「タグがある場所でのみ」有効という著者の意図的な制御であり、単なる
// 行頭の空白の有無だけでは、それが継続行にも及ぶべき字下げなのか、
// 段落先頭だけの字下げ(小説の慣習)なのかを区別できない。
//
// 代わりに採用したのが、直接のユーザー提案("<indentN>のNが伝われば
// いいわけだから、それをタブ用とは別の不可視コードをN個並べておく
// ってのはどうかな？　usilicはそれを受け取ってindentタグと同じ作業を
// する")による、readPortableIndentRun()自身のコメント参照の下記の
// テスト群: usilic -m生成物(usilic.man)のような\x01N制御バイトを
// 持てないポータブルな平文でも、U+FEFF(ZERO WIDTH NO-BREAK SPACE)の
// 連続による単項エンコード経由で、\x01Nと全く同じ「著者が指定した
// 場所でのみ」有効な、複数の生行/折り返し継続行にまたがる字下げを
// 実現できることを確認する。
TEST_CASE("U+FEFF portable indent marker: K consecutive U+FEFF characters (K>=1) set the indent to "
          "(K-1) -- same live, wrap-surviving effect as an explicit \\x01N marker, but usable in a "
          "plain-text file that cannot carry usilic's own raw \\x01 control byte") {
  PipeBuffer buf(Charset::Encoding::Utf8, 6, 1 << 20);
  std::string raw;
  raw += "\xEF\xBB\xBF\xEF\xBB\xBF\xEF\xBB\xBF";  // 3 copies -> indent 2
  raw += "ABCDEFGH\n";                            // too wide for width 6
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 2);
  CHECK(buf.lines()[0].text == "  ABCD");
  CHECK(buf.lines()[0].endsWithNewline == false);
  // Wrapped continuation: the same indent is reapplied automatically,
  // exactly like the \\x01N control-byte marker's own established test above.
  CHECK(buf.lines()[1].text == "  EFGH");
  CHECK(buf.lines()[1].endsWithNewline == true);
}

TEST_CASE("U+FEFF portable indent marker: a SINGLE occurrence (K=1) sets the indent back to 0 -- "
          "the portable equivalent of \\x010 (</indent>)") {
  PipeBuffer buf(Charset::Encoding::Utf8, 80, 1 << 20);
  std::string raw;
  raw += "\xEF\xBB\xBF\xEF\xBB\xBF\xEF\xBB\xBF\xEF\xBB\xBF";  // 4 copies -> indent 3
  raw += "AB\n";
  raw += "\xEF\xBB\xBF";  // 1 copy -> indent 0 (close)
  raw += "CD\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 2);
  CHECK(buf.lines()[0].text == "   AB");
  CHECK(buf.lines()[1].text == "CD");
}

TEST_CASE("U+FEFF portable indent marker: persists across MULTIPLE raw lines until explicitly "
          "closed, same as \\x01N -- a second raw line inside the same span keeps the indent") {
  PipeBuffer buf(Charset::Encoding::Utf8, 80, 1 << 20);
  std::string raw;
  raw += "\xEF\xBB\xBF\xEF\xBB\xBF\xEF\xBB\xBF\xEF\xBB\xBF";  // 4 copies -> indent 3
  raw += "AB\nCD\n";                                          // two raw lines while still active
  raw += "\xEF\xBB\xBF";                                      // close
  raw += "EF\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 3);
  CHECK(buf.lines()[0].text == "   AB");
  CHECK(buf.lines()[1].text == "   CD");  // still indented
  CHECK(buf.lines()[2].text == "EF");     // indent off again after the close marker
}

TEST_CASE("U+FEFF portable indent marker: ignored outside UTF-8 (EUC/ASCII content has no such "
          "convention -- the raw bytes pass through as ordinary content instead)") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  std::string raw = "\xEF\xBB\xBF\xEF\xBB\xBF" "AB\n";  // would be "indent 1" if this were UTF-8
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == raw.substr(0, raw.size() - 1));  // untouched, minus the trailing '\n'
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
  raw.push_back('0');  // \x01 '0' -- an </indent> close, right where HELP.txt's own shape has it
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

// 2026-09-14追加、直接のユーザー依頼("（ls）での^O^M/^[^Mを実装
// して"、QUICK.txt自身の"C-[ C-m：<(ls)> 制御コードをそのまま可視化
// （ls）"): detectOverstrike=falseのコンストラクタ引数を検証する。
TEST_CASE("detectOverstrike=false: a bold-looking \"X\\bX\" byte sequence is left completely raw "
          "(the backspace byte itself passes through as an ordinary character), not interpreted "
          "as an overstrike run") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20, /*detectOverstrike=*/false);
  std::string raw = "f";
  raw.push_back(8);  // backspace
  raw += "f\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  std::string expected = "f";
  expected.push_back(8);
  expected += "f";
  CHECK(buf.lines()[0].text == expected);
  CHECK(buf.lines()[0].attributes.empty());
}

TEST_CASE("detectOverstrike=false: an underline-looking \"_\\bX\" byte sequence is also left raw") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20, /*detectOverstrike=*/false);
  std::string raw = "_";
  raw.push_back(8);
  raw += "f\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  std::string expected = "_";
  expected.push_back(8);
  expected += "f";
  CHECK(buf.lines()[0].text == expected);
  CHECK(buf.lines()[0].attributes.empty());
}

TEST_CASE("detectOverstrike defaults to true when the constructor argument is omitted (backward "
          "compatible with every other existing call site)") {
  PipeBuffer buf(Charset::Encoding::Ascii, 80, 1 << 20);
  std::string raw = "f";
  raw.push_back(8);
  raw += "f\n";
  buf.feed(raw);
  REQUIRE(buf.lines().size() == 1);
  CHECK(buf.lines()[0].text == "f");  // overstrike collapsed, as before
  REQUIRE(buf.lines()[0].attributes.size() == 1);
  CHECK(buf.lines()[0].attributes[0].attribute == Display::Attribute::Bold);
}

// PipeBuffer::wrapLine()のapplyMarkersUntil()自身のコメント参照(2026-09-25、
// HISTORY.mdの引用行の後ろが最後まで斜体になっていた件)。
TEST_CASE("wrapLine: a styled run that ends exactly at the wrap width doesn't leak its style onto later lines") {
  for (const char *tag : {"i", "b", "u"}) {
    CAPTURE(tag);
    std::string body;
    for (int i = 0; i < 49; i++) body += "\xE3\x81\x82";  // あ x49 = 98桁、字下げ2と合わせてちょうど100桁
    std::string markup = std::string("<indent02><") + tag + ">" + body + "</" + tag + "></indent>\nafter\n";
    fm::PipeBuffer pipe(fm::Charset::Encoding::Utf8, 100, 1 << 20);
    pipe.feed(fm::expandHelpMarkup(markup));
    pipe.flush();
    REQUIRE(pipe.lines().size() >= 2);
    CHECK(pipe.lines().back().text == "after");
    CHECK(pipe.lines().back().attributes.empty());
  }
}
