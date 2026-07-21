#pragma once

namespace tcb {

// UTF-8 byte-length / display-width helpers.
//
// Ported from src/code.c's utf8_seqlen()/utf8_width(), which were added
// during the C source's UTF-8 support work (see CLAUDE.md). That project
// hit the same bug class repeatedly: code that conflated "byte count" and
// "display column count" for multi-byte characters. Keeping both concepts
// as clearly separate, explicitly named functions (rather than a single
// "count()" that guesses) is intentional -- callers must pick the one they
// actually need.
class Utf8 {
 public:
  // Byte length (1-4) of the UTF-8 sequence starting with lead byte c.
  // A stray/invalid continuation byte returns 1 so callers never
  // desync/run past a buffer even on malformed input.
  static int sequence_length(unsigned char c);

  // Display width (0, 1, or 2 columns) of the codepoint encoded by the
  // len-byte UTF-8 sequence at cp. Pragmatic subset of the East Asian
  // Wide/Fullwidth ranges (enough for Japanese/CJK text, tcb's primary
  // use case) plus a few zero-width combining-mark ranges; not a
  // complete Unicode East-Asian-Width table.
  static int display_width(const unsigned char *cp, int len);
};

}  // namespace tcb
