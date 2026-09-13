#include "markup_to_roff.hpp"

#include "doctest.h"

using fm::convertMarkupToLegacyHelpText;

// -- convertMarkupToLegacyHelpText() (2026-09-20) --
//
// Direct user request ("usilicに裏オプションを作ってほしい。具体的
// には、`usilic -m src/QUICK.txt` > `~/.local/lib/usilic/usilic.man`
// でusilicのmanファイルが作成される"), then corrected the same day
// ("-mオプションだが、roffソースを出力する必要はない。...src/doc/
// HELP.jpのような形式を打ち出してほしい"): convert usilic's own
// QUICK.txt-style markup (<b>/<u>/<i>/<indentN>) into the SAME literal
// nroff-overstrike plain text legacy's real src/doc/HELP.jp already
// uses ("X\bX" per bold character, "_\bX" per underlined/italic
// character) plus literal leading spaces for <indentN> -- NOT real
// roff/troff macros (usilic's own tags are not an official format, so
// there is no groff step to satisfy here). This is what
// PipeBuffer::wrapLine() already detects unconditionally for ANY plain
// file (file_mode.cpp's own overstrike test), so the generated file
// displays correctly through ordinary FileMode with no per-filename
// markup-expansion special case needed.

TEST_CASE("convertMarkupToLegacyHelpText: <b>X</b> becomes real nroff overstrike (\"X\\bX\" per "
          "character), matching legacy's own src/doc/HELP.jp byte format") {
  std::string out = convertMarkupToLegacyHelpText("<b>AB</b>");
  CHECK(out == "A\bAB\bB");
}

TEST_CASE("convertMarkupToLegacyHelpText: <u>X</u> becomes underline overstrike (\"_\\bX\" per "
          "character)") {
  std::string out = convertMarkupToLegacyHelpText("<u>AB</u>");
  CHECK(out == "_\bA_\bB");
}

TEST_CASE("convertMarkupToLegacyHelpText: <i>X</i> is treated exactly like <u> (same \"_\\bX\" "
          "overstrike) -- legacy's own nroff convention has no italic equivalent to fall back to, "
          "matching src/usilic.1's own established convention of unifying the two") {
  std::string out = convertMarkupToLegacyHelpText("<i>AB</i>");
  CHECK(out == "_\bA_\bB");
}

TEST_CASE("convertMarkupToLegacyHelpText: a multi-byte UTF-8 character is overstruck as ONE whole "
          "character, not byte-by-byte (which would corrupt it)") {
  std::string out = convertMarkupToLegacyHelpText("<b>\xE6\x97\xA5</b>");  // "日" in bold
  CHECK(out == "\xE6\x97\xA5\b\xE6\x97\xA5");
}

TEST_CASE("convertMarkupToLegacyHelpText: <indentN> becomes N literal leading half-width spaces on "
          "each line of that block, not usilic's internal \\x01 control byte") {
  std::string out = convertMarkupToLegacyHelpText("<indent3>hello</indent>");
  CHECK(out == "   hello");
  CHECK(out.find('\x01') == std::string::npos);
}

TEST_CASE("convertMarkupToLegacyHelpText: the indent applies to EVERY line inside the block, not "
          "just the first") {
  std::string out = convertMarkupToLegacyHelpText("<indent2>one\ntwo</indent>");
  CHECK(out == "  one\n  two");
}

TEST_CASE("convertMarkupToLegacyHelpText: the newline immediately after an opening <indentN> tag "
          "and immediately before a closing </indent> tag are both readability-only in the source "
          "and get swallowed (matching expandHelpMarkup()'s own identical convention)") {
  std::string out = convertMarkupToLegacyHelpText("<indent2>\nhello\n</indent>");
  CHECK(out == "  hello");
}

TEST_CASE("convertMarkupToLegacyHelpText: plain characters that have no roff/troff significance at "
          "all here (^, ~, full-width：, -, a leading '.') pass through completely unescaped -- "
          "there is no parser downstream that would misinterpret them, unlike the abandoned "
          "roff-source approach") {
  std::string out = convertMarkupToLegacyHelpText("^H ~/.usilic C\xEF\xBC\x9Ax a-b .gitignore");
  CHECK(out == "^H ~/.usilic C\xEF\xBC\x9Ax a-b .gitignore");
}

TEST_CASE("convertMarkupToLegacyHelpText: a real QUICK.txt-shaped \"label：description\" entry "
          "round-trips with the label bolded and the colon literally intact, ready for a literal "
          "CUT search on \"：\"") {
  std::string out = convertMarkupToLegacyHelpText("<indent6>\xE3\x82\xA2\xEF\xBC\x9A[CUT] desc</indent>");
  CHECK(out == "      \xE3\x82\xA2\xEF\xBC\x9A[CUT] desc");
}

TEST_CASE("convertMarkupToLegacyHelpText: a closing tag with no matching opener is passed through "
          "literally rather than corrupting or hanging (malformed-markup safety net, matching "
          "expandHelpMarkup()'s own identical fallback)") {
  std::string out = convertMarkupToLegacyHelpText("<b>unterminated");
  CHECK(out == "<b>unterminated");
}
