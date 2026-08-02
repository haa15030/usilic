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
// backspace overstrike, and the underlined word is "tcb".

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
  // "_" BS "t", "_" BS "c", "_" BS "b" -- underlined "tcb"
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
