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
  if ((cpt >= 0x0300 && cpt <= 0x036f)
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

  return 1;
}

}  // namespace fm
