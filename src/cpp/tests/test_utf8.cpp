#include "doctest.h"
#include "utf8.hpp"

using fm::Utf8;

TEST_CASE("sequence_length: ASCII is always 1 byte") {
  CHECK(Utf8::sequence_length('A') == 1);
  CHECK(Utf8::sequence_length(0x00) == 1);
  CHECK(Utf8::sequence_length(0x7f) == 1);
}

TEST_CASE("sequence_length: lead byte ranges") {
  CHECK(Utf8::sequence_length(0xc2) == 2);  // 2-byte lead (e.g. U+00A0 nbsp)
  CHECK(Utf8::sequence_length(0xe3) == 3);  // 3-byte lead (most CJK)
  CHECK(Utf8::sequence_length(0xf0) == 4);  // 4-byte lead (astral/emoji)
}

TEST_CASE("sequence_length: stray continuation byte never desyncs") {
  // 0x80-0xbf alone (no lead byte) must return 1, not 0 or negative,
  // so callers advancing byte-by-byte can't get stuck or run backwards.
  CHECK(Utf8::sequence_length(0x80) == 1);
  CHECK(Utf8::sequence_length(0xbf) == 1);
}

TEST_CASE("display_width: plain ASCII is 1 column") {
  unsigned char a[] = {'A'};
  CHECK(Utf8::display_width(a, 1) == 1);
}

TEST_CASE("display_width: CJK ideograph is 2 columns") {
  // U+5831 "報" = e5 a0 b1
  unsigned char houd[] = {0xe5, 0xa0, 0xb1};
  CHECK(Utf8::display_width(houd, 3) == 2);
}

TEST_CASE("display_width: emoji-range codepoint (4-byte) is 2 columns") {
  // U+1F600 GRINNING FACE = f0 9f 98 80
  unsigned char emoji[] = {0xf0, 0x9f, 0x98, 0x80};
  CHECK(Utf8::display_width(emoji, 4) == 2);
}

TEST_CASE("display_width: combining mark is 0 columns") {
  // U+0301 COMBINING ACUTE ACCENT = cc 81
  unsigned char mark[] = {0xcc, 0x81};
  CHECK(Utf8::display_width(mark, 2) == 0);
}

// 2026-09-05追加、直接のユーザー依頼("半角・全角スペース・tab以外で
// 見えない文字って無い？"): U+200B ZERO WIDTH SPACEを不可視マーカー
// 候補として幅0扱いにした――utf8.cpp自身のコメント参照。
TEST_CASE("display_width: U+200B ZERO WIDTH SPACE is 0 columns") {
  // U+200B ZERO WIDTH SPACE = e2 80 8b
  unsigned char zwsp[] = {0xe2, 0x80, 0x8b};
  CHECK(Utf8::display_width(zwsp, 3) == 0);
}

// 2026-08-20 fix, root-caused via a real USILICGUIDEBUG grid-dump
// capture ("ワート゛ニュース" in -g's ARC content listing): these two
// codepoints are the actual combining dakuten/handakuten marks macOS's
// NFD-decomposed filenames use, but they live in the Hiragana block
// (0x3040-0x309F), NOT the generic Combining Diacritical Marks block
// (0x0300-0x036F) the OTHER combining-mark test above already covers
// -- before this fix they fell through into the East Asian Wide
// bucket (0x2e80-0xa4cf) instead, reporting 2 columns instead of 0.
TEST_CASE("display_width: dakuten/handakuten combining marks (U+3099/U+309A) are 0 columns, "
          "not East Asian Wide") {
  // U+3099 COMBINING KATAKANA-HIRAGANA VOICED SOUND MARK = e3 82 99
  unsigned char dakuten[] = {0xe3, 0x82, 0x99};
  CHECK(Utf8::display_width(dakuten, 3) == 0);
  // U+309A COMBINING KATAKANA-HIRAGANA SEMI-VOICED SOUND MARK = e3 82 9a
  unsigned char handakuten[] = {0xe3, 0x82, 0x9a};
  CHECK(Utf8::display_width(handakuten, 3) == 0);
}

TEST_CASE("display_width: halfwidth katakana stays 1 column") {
  // U+FF71 HALFWIDTH KATAKANA LETTER A = ef bd b1 -- just past the
  // fullwidth-forms wide range (which stops at U+FF60), so this must
  // NOT be miscounted as wide.
  unsigned char kana[] = {0xef, 0xbd, 0xb1};
  CHECK(Utf8::display_width(kana, 3) == 1);
}

TEST_CASE("display_width: fullwidth forms boundary is 2 columns") {
  // U+FF10 FULLWIDTH DIGIT ZERO = ef bc 90 -- inside the wide range.
  unsigned char zero[] = {0xef, 0xbc, 0x90};
  CHECK(Utf8::display_width(zero, 3) == 2);
}

TEST_CASE("display_width: ambiguous-width geometric shapes are 2 columns in this CJK context") {
  // U+25BD WHITE DOWN-POINTING TRIANGLE (▽) = e2 96 bd -- found via a
  // real report: a Japanese filename's GraphicalDisplay rendering
  // progressively misaligned starting exactly at this character,
  // because it was being counted as 1 column instead of the 2 it
  // renders as when set alongside the surrounding katakana.
  unsigned char triangle[] = {0xe2, 0x96, 0xbd};
  CHECK(Utf8::display_width(triangle, 3) == 2);
}

TEST_CASE("display_width: CJK-style curly quotation marks are 2 columns in this CJK context") {
  // U+201C LEFT DOUBLE QUOTATION MARK (“) = e2 80 9c
  unsigned char leftQuote[] = {0xe2, 0x80, 0x9c};
  CHECK(Utf8::display_width(leftQuote, 3) == 2);
  // U+201D RIGHT DOUBLE QUOTATION MARK (”) = e2 80 9d
  unsigned char rightQuote[] = {0xe2, 0x80, 0x9d};
  CHECK(Utf8::display_width(rightQuote, 3) == 2);
}
