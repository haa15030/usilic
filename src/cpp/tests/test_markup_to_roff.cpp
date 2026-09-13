#include "markup_to_roff.hpp"

#include "doctest.h"

using fm::convertMarkupToLegacyHelpText;

namespace {
// pipe_buffer.cpp自身のreadPortableIndentRun()と全く同じエンコード
// (2026-09-20、直接のユーザー提案"<indentN>のNが伝わればいいわけ
// だから...不可視コードをN個並べておく"): U+FEFF(UTF-8で"\xEF\xBB\xBF")
// のK個(K>=1)の連続で字下げ幅(K-1)を表す単項エンコード。
const std::string kFeff = "\xEF\xBB\xBF";
std::string portableIndentOpen(int n) {
  std::string out;
  for (int k = 0; k <= n; k++) out += kFeff;  // n+1個
  return out;
}
std::string portableIndentClose() { return kFeff; }  // 1個 -> 字下げ0
}  // namespace

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

// 2026-09-20更新、直接のユーザー報告2件を経ての再設計: 当初はここで
// <indentN>をN個のリテラルな半角スペースへ物理行ごとに焼き込んでいた
// (旧テストはこの形を検証していた)が、(1)端末幅を超えて折り返された
// 継続行にはその字下げを再現する手段が無く先頭が揃わなくなる、
// (2)代替のPipeBuffer側ヒューリスティック(行頭の空白から自動検出)も
// 小説の段落先頭字下げ等、継続行には適用すべきでない字下げと区別
// できず誤りだった――という2つの実際の不具合により撤回した。今は
// pipe_buffer.cpp自身のreadPortableIndentRun()が解釈する、U+FEFFの
// 連続による不可視の単項エンコードへ変換するだけで、字下げの実現
// (継続行への引き継ぎ含む)自体はPipeBuffer::wrapLine()の通常の動的
// 折り返し処理に委ねる――usilic内部専用markup経由の\x01N制御バイト
// 方式と全く同じ実行時の仕組みを、生の制御バイトを持てないポータブル
// な平文からも起動できるようにする、というのがこの関数の役割。
TEST_CASE("convertMarkupToLegacyHelpText: <indentN> becomes the U+FEFF portable indent marker "
          "(N+1 repetitions, readPortableIndentRun()'s own unary encoding), not literal leading "
          "spaces and not usilic's internal \\x01 control byte -- the actual indentation (and its "
          "carry-over to wrapped continuation lines) happens live in PipeBuffer::wrapLine() at "
          "display time, not baked in here") {
  std::string out = convertMarkupToLegacyHelpText("<indent3>hello</indent>");
  CHECK(out == portableIndentOpen(3) + "hello" + portableIndentClose());
  CHECK(out.find('\x01') == std::string::npos);
}

TEST_CASE("convertMarkupToLegacyHelpText: a hard newline INSIDE the block is passed through as an "
          "ordinary '\\n' -- no per-line re-emission of the marker, since PipeBuffer's own "
          "currentIndent_ persists across raw lines until the closing U+FEFF marker is reached "
          "(exactly like the internal \\x01N convention)") {
  std::string out = convertMarkupToLegacyHelpText("<indent2>one\ntwo</indent>");
  CHECK(out == portableIndentOpen(2) + "one\ntwo" + portableIndentClose());
}

TEST_CASE("convertMarkupToLegacyHelpText: the newline immediately after an opening <indentN> tag "
          "and immediately before a closing </indent> tag are both readability-only in the source "
          "and get swallowed (matching expandHelpMarkup()'s own identical convention)") {
  std::string out = convertMarkupToLegacyHelpText("<indent2>\nhello\n</indent>");
  CHECK(out == portableIndentOpen(2) + "hello" + portableIndentClose());
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
  CHECK(out == portableIndentOpen(6) + "\xE3\x82\xA2\xEF\xBC\x9A[CUT] desc" + portableIndentClose());
}

TEST_CASE("convertMarkupToLegacyHelpText: a closing tag with no matching opener is passed through "
          "literally rather than corrupting or hanging (malformed-markup safety net, matching "
          "expandHelpMarkup()'s own identical fallback)") {
  std::string out = convertMarkupToLegacyHelpText("<b>unterminated");
  CHECK(out == "<b>unterminated");
}
