#include "doctest.h"
#include "charset.hpp"

using fm::Charset;
using Encoding = fm::Charset::Encoding;

TEST_CASE("detect: plain ASCII") {
  const unsigned char text[] = "Hello, world!\n";
  CHECK(Charset::detect(text, sizeof(text) - 1, Encoding::Euc) == Encoding::Ascii);
}

TEST_CASE("detect: UTF-8 wins outright under a UTF-8 terminal") {
  // "報" = e5 a0 b1
  const unsigned char text[] = {0xe5, 0xa0, 0xb1, '\n'};
  CHECK(Charset::detect(text, sizeof(text), Encoding::Utf8) == Encoding::Utf8);
}

TEST_CASE("detect: valid UTF-8 must not be misdetected as SJIS/EUC") {
  // Regression for the legacy Step-2 bug: under a UTF-8 terminal, the
  // SJIS/EUC byte-frequency heuristic must never even run on
  // well-formed UTF-8 content.
  const unsigned char text[] = {0xe3, 0x81, 0x82, 0xe3, 0x82, 0xa2, '\n'};  // "あア"
  CHECK(Charset::detect(text, sizeof(text), Encoding::Utf8) == Encoding::Utf8);
}

TEST_CASE("detect: SJIS byte pairs") {
  // "報" in Shift_JIS = 95 f1
  const unsigned char text[] = {0x95, 0xf1, 0x95, 0xf1, '\n'};
  CHECK(Charset::detect(text, sizeof(text), Encoding::Euc) == Encoding::Sjis);
}

TEST_CASE("detect: EUC-JP byte pairs") {
  // "あ" in EUC-JP = a4 a2
  const unsigned char text[] = {0xa4, 0xa2, 0xa4, 0xa2, '\n'};
  CHECK(Charset::detect(text, sizeof(text), Encoding::Euc) == Encoding::Euc);
}

TEST_CASE("detect: ISO-2022-JP escape sequence") {
  // "報" as ISO-2022-JP: ESC $ B 4a 73 ESC ( B
  const unsigned char text[] = {0x1b, 0x24, 0x42, 0x4a, 0x73, 0x1b, 0x28, 0x42, '\n'};
  CHECK(Charset::detect(text, sizeof(text), Encoding::Euc) == Encoding::Jis);
}

TEST_CASE("detect: embedded NUL is binary") {
  const unsigned char text[] = {'a', 'b', 0x00, 'c', 'd'};
  CHECK(Charset::detect(text, sizeof(text), Encoding::Euc) == Encoding::Binary);
}

TEST_CASE("detect: disallowed control byte is binary") {
  const unsigned char text[] = {'a', 0x01, 'b'};
  CHECK(Charset::detect(text, sizeof(text), Encoding::Euc) == Encoding::Binary);
}

TEST_CASE("detect: allowed control bytes (tab/CR) are not binary") {
  const unsigned char text[] = {'a', '\t', '\r', 'b', '\n'};
  CHECK(Charset::detect(text, sizeof(text) - 1, Encoding::Euc) == Encoding::Ascii);
}

TEST_CASE("detect: backspace (nroff overstrike-bold) doesn't get flagged as binary, "
          "and real EUC content around it still converts (2026-08-01 user report)") {
  // "報" in EUC-JP (b0 f1) bold-overstruck around a plain ASCII "1\b1"
  // heading digit -- same shape as doc/HELP.jp's own section numbers.
  // Previously, backspace wasn't in the default control_whitelist, so
  // ANY buffer containing one (e.g. this exact HELP.jp convention) was
  // unconditionally flagged f_bin=true and fell back to Binary --
  // silently defeating EUC->UTF-8 conversion for the whole file
  // (confirmed via a real PTY run: HELP.jp rendered as a garbled mix
  // of raw EUC-JP and UTF-8 bytes under USILICCHARSET=utf8).
  const unsigned char text[] = {'1', 0x08, '1', 0xb0, 0xf1, '\n'};
  CHECK(Charset::detect(text, sizeof(text) - 1, Encoding::Euc) == Encoding::Euc);
}

TEST_CASE("detect: Ascii terminal never auto-switches to Sjis/Euc") {
  const unsigned char text[] = {0xa4, 0xa2, 0xa4, 0xa2, '\n'};  // EUC "あ" pairs
  CHECK(Charset::detect(text, sizeof(text), Encoding::Ascii) == Encoding::Ascii);
}

TEST_CASE("splits_character: UTF-8 mid-sequence vs boundary") {
  // "報" = e5 a0 b1
  const unsigned char text[] = {0xe5, 0xa0, 0xb1, 'x'};
  CHECK(Charset::splits_character(text, 1, Encoding::Utf8) == true);
  CHECK(Charset::splits_character(text, 2, Encoding::Utf8) == true);
  CHECK(Charset::splits_character(text, 3, Encoding::Utf8) == false);
}

TEST_CASE("splits_character: EUC mid-pair vs boundary") {
  // "あ" = a4 a2 (EUC-JP)
  const unsigned char text[] = {0xa4, 0xa2, 'x'};
  CHECK(Charset::splits_character(text, 1, Encoding::Euc) == true);
  CHECK(Charset::splits_character(text, 2, Encoding::Euc) == false);
}

TEST_CASE("splits_character: Ascii never splits") {
  const unsigned char text[] = {'a', 'b', 'c'};
  CHECK(Charset::splits_character(text, 1, Encoding::Ascii) == false);
  CHECK(Charset::splits_character(text, 2, Encoding::Ascii) == false);
}

TEST_CASE("sjis_to_euc: kanji byte pair") {
  // "報": SJIS 95 f1 -> EUC ca f3
  const unsigned char sjis[] = {0x95, 0xf1};
  auto euc = Charset::sjis_to_euc(sjis, sizeof(sjis));
  REQUIRE(euc.size() == 2);
  CHECK(euc[0] == 0xca);
  CHECK(euc[1] == 0xf3);
}

TEST_CASE("sjis_to_euc: half-width kana gets SS2 prefix") {
  const unsigned char sjis[] = {0xb1};  // halfwidth ｱ in SJIS/JIS X0201
  auto euc = Charset::sjis_to_euc(sjis, sizeof(sjis));
  REQUIRE(euc.size() == 2);
  CHECK(euc[0] == 0x8e);
  CHECK(euc[1] == 0xb1);
}

TEST_CASE("sjis_to_euc: ASCII passthrough") {
  const unsigned char sjis[] = {'h', 'i'};
  auto euc = Charset::sjis_to_euc(sjis, sizeof(sjis));
  REQUIRE(euc.size() == 2);
  CHECK(euc[0] == 'h');
  CHECK(euc[1] == 'i');
}

TEST_CASE("jis_to_euc: ISO-2022-JP kanji round-trips to EUC") {
  // "報" as ISO-2022-JP: ESC $ B 4a 73 ESC ( B -> EUC ca f3
  const unsigned char jis[] = {0x1b, 0x24, 0x42, 0x4a, 0x73, 0x1b, 0x28, 0x42};
  auto euc = Charset::jis_to_euc(jis, sizeof(jis));
  REQUIRE(euc.size() == 2);
  CHECK(euc[0] == 0xca);
  CHECK(euc[1] == 0xf3);
}

TEST_CASE("jis_to_euc: plain ASCII outside any escape sequence passes through") {
  const unsigned char jis[] = {'h', 'i'};
  auto euc = Charset::jis_to_euc(jis, sizeof(jis));
  REQUIRE(euc.size() == 2);
  CHECK(euc[0] == 'h');
  CHECK(euc[1] == 'i');
}

TEST_CASE("jis_to_euc: ASCII before and after a JIS run") {
  // "A" ESC $ B 4a 73 ESC ( B "B" -> 'A' ca f3 'B'
  const unsigned char jis[] = {'A', 0x1b, 0x24, 0x42, 0x4a, 0x73, 0x1b, 0x28, 0x42, 'B'};
  auto euc = Charset::jis_to_euc(jis, sizeof(jis));
  REQUIRE(euc.size() == 4);
  CHECK(euc[0] == 'A');
  CHECK(euc[1] == 0xca);
  CHECK(euc[2] == 0xf3);
  CHECK(euc[3] == 'B');
}

TEST_CASE("euc_to_utf8: kanji") {
  // "報": EUC ca f3 -> UTF-8 e5 a0 b1
  const unsigned char euc[] = {0xca, 0xf3};
  auto utf8 = Charset::euc_to_utf8(euc, sizeof(euc));
  REQUIRE(utf8.size() == 3);
  CHECK(utf8[0] == 0xe5);
  CHECK(utf8[1] == 0xa0);
  CHECK(utf8[2] == 0xb1);
}

TEST_CASE("euc_to_utf8: half-width kana via SS2") {
  // halfwidth ｱ: EUC 8e b1 -> UTF-8 ef bd b1
  const unsigned char euc[] = {0x8e, 0xb1};
  auto utf8 = Charset::euc_to_utf8(euc, sizeof(euc));
  REQUIRE(utf8.size() == 3);
  CHECK(utf8[0] == 0xef);
  CHECK(utf8[1] == 0xbd);
  CHECK(utf8[2] == 0xb1);
}

TEST_CASE("euc_to_utf8: ASCII passthrough") {
  const unsigned char euc[] = {'h', 'i'};
  auto utf8 = Charset::euc_to_utf8(euc, sizeof(euc));
  REQUIRE(utf8.size() == 2);
  CHECK(utf8[0] == 'h');
  CHECK(utf8[1] == 'i');
}
