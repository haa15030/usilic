#include "utf8.hpp"

namespace fm {

int Utf8::sequence_length(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c & 0xe0) == 0xc0) return 2;
  if ((c & 0xf0) == 0xe0) return 3;
  if ((c & 0xf8) == 0xf0) return 4;
  return 1;
}

int Utf8::display_width(const unsigned char *cp, int len) {
  unsigned int cpt;

  if (len <= 1) return 1;
  else if (len == 2) cpt = ((cp[0] & 0x1f) << 6) | (cp[1] & 0x3f);
  else if (len == 3) cpt = ((cp[0] & 0x0f) << 12) | ((cp[1] & 0x3f) << 6)
                          | (cp[2] & 0x3f);
  else cpt = ((cp[0] & 0x07) << 18) | ((cp[1] & 0x3f) << 12)
           | ((cp[2] & 0x3f) << 6) | (cp[3] & 0x3f);

  // zero-width combining marks
  //
  // U+3099/U+309A (2026-08-20 fix, root-caused via a real
  // USILICGUIDEBUG grid-dump capture: "ワート゛ニュース" -- the
  // combining dakuten/handakuten mark rendering as its own separate
  // glyph instead of merging with the preceding kana, in -g's ARC
  // content listing): these two codepoints ARE the actual combining
  // dakuten/handakuten marks macOS's NFD-decomposed filenames use
  // (e.g. "ワールド" stored on disk as "ワールト"+U+3099), but they
  // live in the Hiragana block (0x3040-0x309F), NOT the generic
  // "Combining Diacritical Marks" block (0x0300-0x036F) the range
  // above already covers -- so they fell all the way through to the
  // "East Asian Wide" bucket below (0x2e80-0xa4cf swallows this whole
  // block too) and got rendered as an independent width-2 glyph next
  // to their base character instead of being merged via
  // GraphicalDisplay::commitGlyph()'s own width==0 special case
  // (already correct once display_width() actually reports 0 here --
  // see that function's own 2026-08-20 comment, added the same day
  // this was tracked down, for the OTHER half of this fix). `-c`
  // never showed this because it just passes raw UTF-8 bytes through
  // to a real terminal emulator, which does its own independent
  // Unicode text shaping regardless of what this port's own width
  // math thinks; `-g` renders every glyph itself from this port's own
  // Cell grid, so an incorrect classification here directly corrupts
  // the visible output. NOT ported back to legacy's code.c (which has
  // this exact same gap in its own, separately-maintained
  // utf8_width()) -- unreported there, and legacy has no -g/pixel
  // rendering path where this class of bug can surface the same way
  // (see the class of `-c`-masks-it reasoning just above); left as a
  // known related finding rather than an unrequested legacy edit.
  if ((cpt >= 0x0300 && cpt <= 0x036f)
      || (cpt == 0x3099 || cpt == 0x309a)
      || (cpt >= 0x1ab0 && cpt <= 0x1aff)
      || (cpt >= 0x1dc0 && cpt <= 0x1dff)
      || (cpt >= 0x20d0 && cpt <= 0x20ff)
      || (cpt >= 0xfe20 && cpt <= 0xfe2f))
    return 0;

  // East Asian Wide / Fullwidth (CJK, Hangul, fullwidth forms,
  // common symbols/emoji)
  if ((cpt >= 0x1100 && cpt <= 0x115f)
      || (cpt >= 0x2e80 && cpt <= 0xa4cf && cpt != 0x303f)
      || (cpt >= 0xac00 && cpt <= 0xd7a3)
      || (cpt >= 0xf900 && cpt <= 0xfaff)
      || (cpt >= 0xfe30 && cpt <= 0xfe4f)
      || (cpt >= 0xff00 && cpt <= 0xff60)
      || (cpt >= 0xffe0 && cpt <= 0xffe6)
      || (cpt >= 0x20000 && cpt <= 0x3fffd)
      || (cpt >= 0x1f300 && cpt <= 0x1faff)
      || (cpt >= 0x2600 && cpt <= 0x27bf))
    return 2;

  // "Ambiguous" East Asian Width (Unicode's own category A): neither
  // clearly narrow nor clearly wide by the standard alone, but
  // conventionally rendered WIDE when set alongside CJK text, which
  // is the only context this whole class exists for. Added
  // 2026-07-30 after a real report of GraphicalDisplay progressively
  // misaligning a Japanese filename's characters starting exactly at
  // a "▽" (U+25BD) -- confirmed (byte-for-byte, via a standalone
  // decode check) that the filename's UTF-8 was completely valid, so
  // the corruption traced back to THIS function under-counting a
  // character actually used in real content, not to any decoding bug.
  // Narrowed to just the two ranges that were actually present in the
  // reported filenames (the Geometric Shapes block -- ▽ △ ○ ● etc.,
  // common as bullet/marker glyphs in Japanese text -- and the CJK-
  // style curly quotation marks) rather than the full Unicode
  // "Ambiguous" set (which also includes things like Latin-1
  // punctuation and Cyrillic that should stay narrow even in this
  // context) -- deliberately conservative rather than reclassifying
  // everything Unicode calls "Ambiguous".
  if ((cpt >= 0x2018 && cpt <= 0x201f)   // ‘ ’ ‚ ‛ “ ” „ ‟
      || (cpt >= 0x25a0 && cpt <= 0x25ff))  // Geometric Shapes (■ □ ▲ △ ▽ ● ○ ★ ☆ ...)
    return 2;

  return 1;
}

}  // namespace fm
