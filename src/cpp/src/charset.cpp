#include "charset.hpp"

#include <cstring>

#include "utf8.hpp"
#include "euc_uni.h"

namespace fm {

namespace {

// --- byte-class predicates, ported from tcb.h's is_sjis_1_byte() etc. ---
bool is_sjis_1_byte(unsigned char n) {
  return (n >= 0x81 && n <= 0x9f) || (n >= 0xe0 && n <= 0xea);
}
bool is_sjis_2_byte(unsigned char n) { return n >= 0x40; }
bool is_sjiskana(unsigned char n) { return n >= 0xa1 && n <= 0xdf; }
bool is_euckana_1_byte(unsigned char n) { return n == 0x8e; }
bool is_euckana_2_byte(unsigned char n) { return n >= 0xa1 && n <= 0xdf; }
bool is_euc_1_byte(unsigned char n) { return n >= 0xa1; }
bool is_euc_2_byte(unsigned char n) { return n >= 0xa1; }

constexpr unsigned char kBackspace = 0x08;
constexpr unsigned char kTab = 0x09;
constexpr unsigned char kNewline = 0x0a;
constexpr unsigned char kShiftOut = 0x0e;  // SO: half-width kana on
constexpr unsigned char kShiftIn = 0x0f;   // SI: half-width kana off
constexpr unsigned char kEsc = 0x1b;

bool is_well_formed_utf8(const unsigned char *buf, int size) {
  int f_multi = 0;
  for (int i = 0; i < size; ) {
    if (buf[i] < 0x80) { i += 1; continue; }
    if (buf[i] < 0xc2 || buf[i] > 0xf4) return false;
    int len = Utf8::sequence_length(buf[i]);
    if (i + len > size) break;  // truncated tail, tolerated
    for (int j = 1; j < len; j++)
      if ((buf[i + j] & 0xc0) != 0x80) return false;
    f_multi = 1;
    i += len;
  }
  return f_multi != 0;
}

void uni_to_utf8(unsigned int cp, std::vector<unsigned char> *out) {
  if (cp < 0x80) {
    out->push_back(static_cast<unsigned char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<unsigned char>(0xc0 | (cp >> 6)));
    out->push_back(static_cast<unsigned char>(0x80 | (cp & 0x3f)));
  } else {
    out->push_back(static_cast<unsigned char>(0xe0 | (cp >> 12)));
    out->push_back(static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3f)));
    out->push_back(static_cast<unsigned char>(0x80 | (cp & 0x3f)));
  }
}

}  // namespace

Charset::Encoding Charset::detect(const unsigned char *buf, int size,
                                   Encoding terminal_encoding,
                                   const char *control_whitelist) {
  if (terminal_encoding == Encoding::Utf8 && is_well_formed_utf8(buf, size))
    return Encoding::Utf8;

  int kn_sjis = 0, kn_euc = 0;
  int c1 = 0;
  bool saw_esc_jis = false;   // legacy c5: saw ESC $ B/@, waiting for ESC ( B/J
  bool saw_shift_out = false; // legacy c4: saw SO, waiting for SI
  bool f_bin = false;
  const unsigned char *end = buf + size;

  for (; buf < end; buf++) {
    if (!c1 && (c1 = buf[0]) == kNewline) { c1 = 0; continue; }

    if (!f_bin && (!c1 || (c1 < ' ' && !std::strchr(control_whitelist, c1))))
      f_bin = true;

    if (terminal_encoding == Encoding::Ascii && saw_esc_jis) { c1 = 0; continue; }

    if (buf == end - 1) break;
    unsigned char c2 = buf[1];

    if (is_sjis_1_byte(c1) && is_sjis_2_byte(c2)) {
      if (f_bin) return Encoding::Binary;
      kn_sjis++;
      buf++;
    } else if ((is_euckana_1_byte(c1) && is_euckana_2_byte(c2)) ||
               (is_euc_1_byte(c1) && is_euc_2_byte(c2))) {
      if (f_bin) return Encoding::Binary;
      kn_euc++;
      buf++;
    } else if (c1 == kEsc) {
      if (buf >= end - 2) break;
      unsigned char c3 = buf[2];
      if (c2 == '$' && (c3 == 'B' || c3 == '@')) saw_esc_jis = true;
      else if (c2 == '(' && (c3 == 'B' || c3 == 'J') && saw_esc_jis)
        return Encoding::Jis;
      else if (saw_esc_jis) saw_esc_jis = false;
    } else if (!f_bin) {
      if (c1 == kShiftOut) saw_shift_out = true;
      else if (c1 == kShiftIn && saw_shift_out) return Encoding::Jis;
    }
    c1 = 0;
  }

  if (saw_esc_jis) return (terminal_encoding == Encoding::Ascii) ? Encoding::Ascii : Encoding::Jis;
  if (f_bin) return Encoding::Binary;
  if (terminal_encoding == Encoding::Ascii || (!kn_sjis && !kn_euc)) return Encoding::Ascii;
  return (kn_sjis >= kn_euc) ? Encoding::Sjis : Encoding::Euc;
}

int Charset::sequence_length(unsigned char lead_byte) {
  return Utf8::sequence_length(lead_byte);
}

int Charset::display_width(const unsigned char *cp, int len) {
  return Utf8::display_width(cp, len);
}

bool Charset::splits_character(const unsigned char *buf, int pos, Encoding encoding) {
  if (encoding == Encoding::Utf8) {
    int p = 0;
    while (p < pos) {
      int len = Utf8::sequence_length(buf[p]);
      if (p + len > pos) return true;
      p += len;
    }
    return false;
  }
  if (encoding != Encoding::Sjis && encoding != Encoding::Euc) return false;
  bool in_pair = false;
  for (int i = 0; i < pos; i++)
    in_pair = (buf[i] >= 0x80) ? !in_pair : false;
  return in_pair;
}

std::vector<unsigned char> Charset::sjis_to_euc(const unsigned char *buf, int len) {
  std::vector<unsigned char> out;
  out.reserve(len * 2);
  for (int i = 0; i < len; ) {
    unsigned char c1 = buf[i];
    if (is_sjis_1_byte(c1) && i + 1 < len) {
      unsigned char c2 = buf[i + 1];
      int b1 = (c1 << 1) - ((c1 > 0x9f) ? 0x161 : 0xe1);
      int b2;
      if (c2 < 0x9f) b2 = c2 - ((c2 > 0x7f) ? 0x20 : 0x1f);
      else { b2 = c2 - 0x7e; b1++; }
      out.push_back(static_cast<unsigned char>(b1 | 0x80));
      out.push_back(static_cast<unsigned char>(b2 | 0x80));
      i += 2;
    } else if (is_sjiskana(c1)) {
      out.push_back(0x8e);
      out.push_back(c1);
      i++;
    } else {
      out.push_back(c1);
      i++;
    }
  }
  return out;
}

// ISO-2022-JP state machine states (numbered to match the legacy
// conv_jis()'s jisp->f.s, minus the quoted-printable/MIME-only
// branches which never applied without MIME decoding in the first
// place):
//   0    ASCII
//   1, 8 just saw ESC, waiting for '$' or '('
//   2    saw ESC $, waiting for 'B'/'@' (-> enter 2-byte JIS, state 3)
//   3    2-byte JIS mode (each byte gets the high bit set for EUC)
//   4    (unused entry point kept for state-number parity with the
//        legacy table; not reachable via the ESC-only paths kept here)
//   5    saw ESC (, waiting for 'B'/'J' (-> ASCII) or 'I' (-> state 7)
//   6    saw SO, waiting for the next byte to become a half-width kana
//   7    half-width kana mode (SS2-prefix each byte for EUC)
std::vector<unsigned char> Charset::jis_to_euc(const unsigned char *buf, int len) {
  std::vector<unsigned char> out;
  out.reserve(len * 2);
  int state = 0;

  for (int i = 0; i < len; i++) {
    unsigned char c = buf[i];

    switch (c) {
      case kNewline:
        if (state) state = 0;
        [[fallthrough]];
      case kTab:
      case kBackspace:
      case kShiftOut:
      case kShiftIn:
        break;
      case kEsc:
        state++;
        continue;
      default:
        if (c < 0x20) {
          if (state == 3 && !out.empty()) out.pop_back();
          continue;
        }
        break;
    }

    switch (state) {
      case 0:
        if (c == kShiftOut) { state = 6; continue; }
        out.push_back(c);
        break;
      case 1:
      case 8:
        if (c == '$') state = 2;
        else if (c == '(') state = 5;
        else if (state == 1) {
          state = 0;
          out.push_back(kEsc);
          out.push_back(c);
        }
        break;
      case 2:
        if (c == 'B' || c == '@') state = 3;
        break;
      case 3:
        out.push_back((c >= ' ') ? static_cast<unsigned char>(c | 0x80) : c);
        break;
      case 4:
        if (c == '(') state = 5;
        break;
      case 5:
        if (c == 'B' || c == 'J') state = 0;
        else if (c == 'I') state = 7;
        break;
      case 6:
        if (c == kShiftIn) { state = 0; continue; }
        [[fallthrough]];
      case 7:
        out.push_back(0x8e);
        out.push_back(static_cast<unsigned char>(c | 0x80));
        break;
    }
  }
  return out;
}

std::vector<unsigned char> Charset::euc_to_utf8(const unsigned char *buf, int len) {
  std::vector<unsigned char> out;
  out.reserve(len * 3);
  const unsigned char *end = buf + len;

  while (buf < end) {
    unsigned char c = *buf;
    unsigned int cp;
    if (c == 0x8e && buf + 1 < end && buf[1] >= 0xa1 && buf[1] <= 0xdf) {
      cp = euc_kana_uni[buf[1] - 0xa1];
      buf += 2;
    } else if (c >= 0xa1 && c <= 0xfe && buf + 1 < end &&
               buf[1] >= 0xa1 && buf[1] <= 0xfe) {
      cp = euc_jisx0208_uni[c - 0xa1][buf[1] - 0xa1];
      buf += 2;
    } else {
      cp = c;
      buf += 1;
    }
    uni_to_utf8(cp ? cp : '?', &out);
  }
  return out;
}

}  // namespace fm
