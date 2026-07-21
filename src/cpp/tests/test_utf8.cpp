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
