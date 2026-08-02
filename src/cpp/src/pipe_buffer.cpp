#include "pipe_buffer.hpp"

namespace fm {

PipeBuffer::PipeBuffer(Charset::Encoding encoding, int width, long maxBytes)
    : encoding_(encoding), width_(width), maxBytes_(maxBytes) {}

void PipeBuffer::feed(const std::string &bytes) {
  rawPending_.append(bytes);
  processPending();
}

void PipeBuffer::flush() {
  if (!rawPending_.empty()) {
    wrapLine(convertChunk(rawPending_));
    rawPending_.clear();
  }
  if (!wrapText_.empty() || !wrapAttrs_.empty()) {
    lines_.push_back({wrapText_, wrapAttrs_, false});
    usedBytes_ += static_cast<long>(wrapText_.size());
    if (usedBytes_ > maxBytes_) overBudget_ = true;
    wrapText_.clear();
    wrapAttrs_.clear();
    wrapCol_ = 0;
  }
}

const std::vector<StyledLine> &PipeBuffer::lines() const { return lines_; }

bool PipeBuffer::overBudget() const { return overBudget_; }

// Consumes complete raw lines (up to and including each '\n') out of
// rawPending_, charset-converting and width-wrapping each one.
// Anything after the last '\n' stays in rawPending_ for the next
// feed() call -- this is what lets feed() be called with arbitrarily
// small chunks (e.g. as bytes arrive from a live pipe) without ever
// charset-detecting or wrapping a line before it's fully received.
void PipeBuffer::processPending() {
  std::size_t start = 0;
  while (true) {
    std::size_t eol = rawPending_.find('\n', start);
    if (eol == std::string::npos) break;
    std::string rawLine = rawPending_.substr(start, eol - start + 1);
    wrapLine(convertChunk(rawLine));
    start = eol + 1;
  }
  rawPending_.erase(0, start);
}

// Ported from linein()'s charset detection/conversion, now built on
// the already-ported Charset class instead of linein()'s own
// init_code()/toeuc()/jistoeuc()/euctoutf8_buf() calls. MIME-encoded
// (base64/quoted-printable) content isn't handled -- Charset itself
// doesn't support it (see charset.hpp: excluded when that class was
// designed, per the user's "そんな機能すら覚えてない" call).
std::string PipeBuffer::convertChunk(const std::string &rawChunk) {
  if (encoding_ == Charset::Encoding::Ascii) return rawChunk;

  const auto *buf = reinterpret_cast<const unsigned char *>(rawChunk.data());
  Charset::Encoding detected = Charset::detect(buf, static_cast<int>(rawChunk.size()), encoding_);

  std::string euc;
  switch (detected) {
    case Charset::Encoding::Sjis: {
      auto out = Charset::sjis_to_euc(buf, static_cast<int>(rawChunk.size()));
      euc.assign(out.begin(), out.end());
      break;
    }
    case Charset::Encoding::Jis: {
      auto out = Charset::jis_to_euc(buf, static_cast<int>(rawChunk.size()));
      euc.assign(out.begin(), out.end());
      break;
    }
    case Charset::Encoding::Euc:
      euc = rawChunk;
      break;
    default:
      return rawChunk;  // Ascii/Utf8/Binary: passthrough
  }

  if (encoding_ == Charset::Encoding::Utf8) {
    auto out = Charset::euc_to_utf8(reinterpret_cast<const unsigned char *>(euc.data()),
                                     static_cast<int>(euc.size()));
    return std::string(out.begin(), out.end());
  }
  return euc;
}

int PipeBuffer::byteLengthAt(const std::string &s, int pos) const {
  if (pos >= static_cast<int>(s.size())) return 1;
  unsigned char c = static_cast<unsigned char>(s[pos]);
  if (encoding_ == Charset::Encoding::Utf8) return Charset::sequence_length(c);
  if (encoding_ == Charset::Encoding::Euc) {
    if (c == 0x8e) return 2;    // SS2 half-width kana
    if (c >= 0xa1) return 2;    // regular 2-byte EUC pair
    return 1;
  }
  return 1;
}

int PipeBuffer::widthAt(const std::string &s, int pos, int charBytes) const {
  if (encoding_ == Charset::Encoding::Utf8)
    return Charset::display_width(reinterpret_cast<const unsigned char *>(s.data()) + pos, charBytes);
  if (encoding_ == Charset::Encoding::Euc && charBytes == 2 &&
      static_cast<unsigned char>(s[pos]) == 0x8e)
    return 1;  // half-width kana: 2 bytes, but only 1 column wide
  return charBytes;  // EUC 2-byte kanji: 2 bytes = 2 columns; ASCII: 1 byte = 1 column
}

// Ported from alloc_pipe()'s per-character loop (tab/width wrapping)
// and save_attr() (overstrike detection), restructured around a
// forward-looking check ("does the *next* thing look like an
// overstruck character?") instead of legacy's reactive one ("I just
// saw a backspace, was the previous character part of one?"), which
// lets each detected run be emitted in a single step instead of
// needing to patch an already-emitted position. Ported patterns:
//   "_\bX"     -> underline (legacy A_us)
//   "__\b\bX"  -> underline, wide source variant (legacy A_us2)
//   "X\bX"     -> bold, X is a 1-byte (ASCII) character (legacy A_md)
//   "X\b\bX"   -> bold, X is a 2-column-wide character -- 2-byte EUC
//                 kanji or (in UTF-8 mode) a multi-byte CJK
//                 character (legacy A_md2, including the UTF-8 fix
//                 from CLAUDE.md history item 8)
// legacy's A_md3 ("X\bY" where Y is a single non-ASCII byte) is not
// ported -- see the class comment.
void PipeBuffer::wrapLine(const std::string &convertedLine) {
  int n = static_cast<int>(convertedLine.size());
  int i = 0;

  auto finalize = [&](bool endsWithNewline) {
    lines_.push_back({wrapText_, wrapAttrs_, endsWithNewline});
    usedBytes_ += static_cast<long>(wrapText_.size());
    if (usedBytes_ > maxBytes_) overBudget_ = true;
    wrapText_.clear();
    wrapAttrs_.clear();
    wrapCol_ = 0;
  };

  while (i < n) {
    unsigned char c = static_cast<unsigned char>(convertedLine[i]);
    if (c == '\n') {
      finalize(true);
      i++;
      continue;
    }

    if (c == '\t') {
      // Expanded to `step` literal spaces here, NOT kept as a raw tab
      // byte -- ported from disp.c's own "convert '\t' to ' '"
      // comment (its own count_h()/count_i() column-counting pass
      // does the same substitution before anything reaches the
      // display layer). ContentView::renderLine() treats any byte
      // < 0x20 as a control code needing reverse-video display (the
      // K_at/K_dl convention, see its own comment) -- a raw tab byte
      // surviving into wrapText_ used to hit that same path, showing
      // as a visible reverse-video glyph instead of whitespace. Found
      // via a real user report (2026-08-02, screenshot): plain `ls`
      // column output (which real shells tab-align, not space-align)
      // showed small reverse-video boxes between filenames in SHELL's
      // own scrollback specifically (the one place in this port that
      // feeds fully raw, uncooked real-command output through this
      // same PipeBuffer FileMode/ManMode otherwise only see
      // post-processed file text from).
      int step = 8 - (wrapCol_ % 8);
      if (wrapCol_ + step > width_) step = width_ - wrapCol_;
      step = std::max(0, step);
      wrapCol_ += step;
      wrapText_.append(static_cast<std::size_t>(step), ' ');
      i++;
      if (wrapCol_ >= width_) finalize(false);
      continue;
    }

    int keepStart = -1, keepBytes = 0;
    Display::Attribute attr = Display::Attribute::Bold;

    if (c == '_' && i + 1 < n && static_cast<unsigned char>(convertedLine[i + 1]) == 0x08) {
      if (i + 3 < n && convertedLine[i + 2] == '_' &&
          static_cast<unsigned char>(convertedLine[i + 3]) == 0x08) {
        int pos = i + 4;
        int len = byteLengthAt(convertedLine, pos);
        if (pos + len <= n) { keepStart = pos; keepBytes = len; attr = Display::Attribute::Underline; }
      } else if (i + 2 < n) {
        int pos = i + 2;
        int len = byteLengthAt(convertedLine, pos);
        if (pos + len <= n) { keepStart = pos; keepBytes = len; attr = Display::Attribute::Underline; }
      }
    } else {
      int charBytes = byteLengthAt(convertedLine, i);
      if (i + charBytes < n && static_cast<unsigned char>(convertedLine[i + charBytes]) == 0x08) {
        if (i + charBytes + 1 < n &&
            static_cast<unsigned char>(convertedLine[i + charBytes + 1]) == 0x08) {
          int pos = i + charBytes + 2;
          int len = (encoding_ == Charset::Encoding::Utf8) ? byteLengthAt(convertedLine, pos) : 2;
          if (pos + len <= n) { keepStart = pos; keepBytes = len; attr = Display::Attribute::Bold; }
        } else {
          int pos = i + charBytes + 1;
          if (pos < n && static_cast<unsigned char>(convertedLine[pos]) < 0x80) {
            keepStart = pos;
            keepBytes = 1;
            attr = Display::Attribute::Bold;
          }
        }
      }
    }

    if (keepStart >= 0) {
      int w = widthAt(convertedLine, keepStart, keepBytes);
      if (wrapCol_ + w > width_) { finalize(false); continue; }
      int start = static_cast<int>(wrapText_.size());
      wrapText_.append(convertedLine, keepStart, keepBytes);
      wrapAttrs_.push_back({start, start + keepBytes, attr});
      wrapCol_ += w;
      i = keepStart + keepBytes;
      if (wrapCol_ >= width_) finalize(false);
      continue;
    }

    int charBytes = byteLengthAt(convertedLine, i);
    if (i + charBytes > n) charBytes = n - i;  // truncated sequence at chunk end
    int w = widthAt(convertedLine, i, charBytes);
    if (wrapCol_ + w > width_) { finalize(false); continue; }
    wrapText_.append(convertedLine, i, charBytes);
    wrapCol_ += w;
    i += charBytes;
    if (wrapCol_ >= width_) finalize(false);
  }
}

}  // namespace fm
