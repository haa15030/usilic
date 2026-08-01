#pragma once

#include <vector>

namespace fm {

// SJIS/EUC/JIS/UTF-8 detection and conversion, plus the byte-length /
// display-width queries every other class needs to render text
// correctly. Ported from tcb/src/code.c.
//
// Unlike the legacy C (which reads the current terminal charset from
// global state via vt_charset(0)), every method here takes whatever
// charset context it needs as an explicit parameter. Charset has no
// hidden global dependency, so it is fully unit-testable on its own --
// this is deliberate: conflating "what encoding is this byte range"
// with "what does the terminal happen to be showing right now" is
// exactly the class of bug (K_at/0x80 collision, count_k byte-vs-width
// mixups, etc.) that repeatedly bit the C implementation.
//
// Scope note: the legacy code.c also decoded MIME quoted-printable/
// base64 encoded-word headers (`=?ISO-2022-JP?B?...?=`, old mail-
// viewing support) inline in this same detection pass. That is
// deliberately NOT ported here -- confirmed unused/unneeded when this
// class was designed (2026-07-21). The plain ISO-2022-JP escape-
// sequence detection/conversion (ESC $ B ... ESC ( B, i.e. `Jis` below)
// is unrelated to MIME and is kept.
class Charset {
 public:
  enum class Encoding {
    Ascii,
    Sjis,
    Euc,
    Jis,
    Utf8,
    Binary,
  };

  // Guesses the encoding of buf. terminal_encoding is whatever charset
  // is currently in effect (legacy vt_charset(0)): if it's Utf8 and
  // buf is well-formed UTF-8, that wins outright before the SJIS/EUC
  // byte-frequency heuristic even runs -- without this check first,
  // the heuristic can misdetect valid UTF-8 as SJIS/EUC and corrupt it
  // on "conversion" (this was a real, previously-shipped bug in the
  // legacy C, see CLAUDE.md's UTF-8 Step 2). If terminal_encoding is
  // Ascii, only Ascii or Binary are ever returned, matching legacy
  // behavior (no charset auto-switching while running in plain ASCII
  // mode).
  //
  // control_whitelist lists the control-character byte values (other
  // than '\n') that are allowed to appear in a non-binary file, e.g.
  // "\t\r\x08" -- normally sourced from the user's tcbrc CODE setting.
  // Backspace (0x08) is whitelisted here for a narrower reason than
  // legacy's own special-casing: this class was originally designed
  // (2026-07-21) treating legacy's nroff-style overstrike-bold
  // handling (see CLAUDE.md history's item 8) as purely a DISPLAY-
  // formatting concern (PipeBuffer's own A_md/A_md2 job), unrelated
  // to encoding detection -- but leaving 0x08 OFF this whitelist still
  // let it flip f_bin=true here regardless, silently defeating EUC/
  // SJIS->UTF-8 conversion for any bold-formatted EUC-JP content
  // (found via a real user report, 2026-08-01: HELP.jp -- which uses
  // this exact overstrike convention throughout for its section
  // numbers -- rendered as a garbled mix of raw EUC-JP and UTF-8
  // bytes when USILICCHARSET=utf8, i.e. the SAME class of bug
  // CLAUDE.md's item 8 already fixed once in the legacy C, quietly
  // reintroduced here by this simplification's own side effect on
  // detection rather than display). Whitelisting 0x08 by default
  // fixes detection without reintroducing legacy's own display-side
  // overstrike special-casing, which PipeBuffer already owns
  // separately and correctly.
  static Encoding detect(const unsigned char *buf, int size,
                          Encoding terminal_encoding,
                          const char *control_whitelist = "\t\r\x08");

  // Byte length (1-4) of the UTF-8 sequence starting with lead byte c.
  static int sequence_length(unsigned char lead_byte);

  // Display width (0, 1, or 2 columns) of the len-byte UTF-8 sequence
  // at cp.
  static int display_width(const unsigned char *cp, int len);

  // True if byte offset `pos` falls in the middle of a multi-byte
  // character, so cutting/truncating buf there would corrupt it.
  // Ascii/Binary/Jis are 1-byte-per-unit as far as this check is
  // concerned (Jis content is always converted to Euc before display
  // truncation ever applies); Sjis/Euc use a same-parity-toggle
  // heuristic (a run of high-bit bytes starting at buf[0] comes in
  // 2-byte pairs); Utf8 uses sequence_length().
  static bool splits_character(const unsigned char *buf, int pos,
                                Encoding encoding);

  // SJIS -> EUC. Same JIS X 0208 character repertoire as EUC/JIS, just
  // a different byte encoding, so this is pure arithmetic -- no
  // codepoint table needed. Half-width kana pass through as EUC's
  // SS2 (0x8e)-prefixed form.
  static std::vector<unsigned char> sjis_to_euc(const unsigned char *buf, int len);

  // ISO-2022-JP ("JIS") -> EUC. An ESC-sequence-driven encoding
  // (ESC $ B / ESC $ @ enters 2-byte JIS mode, ESC ( B / ESC ( J
  // returns to ASCII; the older SO/SI control codes switch to/from
  // half-width kana).
  static std::vector<unsigned char> jis_to_euc(const unsigned char *buf, int len);

  // EUC-JP -> UTF-8. Unlike sjis<->euc<->jis (one character set,
  // several byte encodings, arithmetic-convertible), EUC-JP and UTF-8
  // encode overlapping but distinct Unicode-era repertoires, so this
  // needs an actual codepoint table (euc_uni.h, generated from the
  // JIS X 0208 <-> Unicode mapping).
  static std::vector<unsigned char> euc_to_utf8(const unsigned char *buf, int len);
};

}  // namespace fm
