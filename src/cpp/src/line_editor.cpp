#include "line_editor.hpp"

#include <algorithm>

namespace fm {

namespace {
const unsigned char *bytes(const std::string &s) {
  return reinterpret_cast<const unsigned char *>(s.data());
}
}  // namespace

LineEditor::LineEditor(Display &display, Charset::Encoding encoding)
    : display_(display), encoding_(encoding) {}

void LineEditor::setEncoding(Charset::Encoding encoding) {
  encoding_ = encoding;
  // A charset change mid-typing is an edge case legacy didn't handle
  // either (there was no way to change charset via a key while a
  // partial UTF-8 sequence was buffered) -- abandon any partial
  // sequence rather than risk misinterpreting it under the new
  // encoding.
  pendingUtf8_.clear();
}

void LineEditor::setField(int row, int col0, int width) {
  row_ = row;
  col0_ = col0;
  width_ = width;
}

void LineEditor::setText(const std::string &text) {
  buffer_ = text;
  if (static_cast<int>(buffer_.size()) > kMaxLength - 1)
    buffer_.resize(kMaxLength - 1);
  cursorPos_ = static_cast<int>(buffer_.size());
  shift_ = 0;
  pendingUtf8_.clear();
  redraw();
}

const std::string &LineEditor::text() const { return buffer_; }

int LineEditor::cursorBytePos() const { return cursorPos_; }

// -- UTF-8 / EUC byte<->column helpers --
// Ported from key.c's char_bytes_before()/b2col()/utf8_total_width()/
// utf8_right_shift() (see the class comment on why they're each
// written once here instead of once per call site as in legacy).

int LineEditor::charBytesBefore(int pos) const {
  if (encoding_ != Charset::Encoding::Utf8)
    return Charset::splits_character(bytes(buffer_), pos, encoding_) ? 2 : 1;
  if (pos <= 0) return 0;
  int end = pos - 1, start = end;
  while (start > 0 && (static_cast<unsigned char>(buffer_[start]) & 0xc0) == 0x80) start--;
  return end - start + 1;
}

int LineEditor::charBytesAt(int pos) const {
  if (pos >= static_cast<int>(buffer_.size())) return 1;
  if (encoding_ == Charset::Encoding::Utf8)
    return Charset::sequence_length(static_cast<unsigned char>(buffer_[pos]));
  return Charset::splits_character(bytes(buffer_), pos + 1, encoding_) ? 2 : 1;
}

int LineEditor::byteToColumn(int shift, int bytePos) const {
  if (encoding_ != Charset::Encoding::Utf8) return bytePos - shift;
  int col = 0;
  int n = static_cast<int>(buffer_.size());
  for (int pos = shift; pos < bytePos && pos < n; ) {
    int sl = Charset::sequence_length(static_cast<unsigned char>(buffer_[pos]));
    col += Charset::display_width(bytes(buffer_) + pos, sl);
    pos += sl;
  }
  return col;
}

int LineEditor::totalWidth(const std::string &text) const {
  if (encoding_ != Charset::Encoding::Utf8) return static_cast<int>(text.size());
  int total = 0;
  int n = static_cast<int>(text.size());
  for (int pos = 0; pos < n; ) {
    int sl = Charset::sequence_length(static_cast<unsigned char>(text[pos]));
    total += Charset::display_width(bytes(text) + pos, sl);
    pos += sl;
  }
  return total;
}

int LineEditor::rightShift(const std::string &text, int fieldWidth) const {
  int n = static_cast<int>(text.size());
  if (encoding_ != Charset::Encoding::Utf8) {
    int excess = n - fieldWidth;
    return (excess > 0) ? excess : 0;
  }
  int col = totalWidth(text) - fieldWidth;
  int pos = 0;
  while (pos < n && col > 0) {
    int sl = Charset::sequence_length(static_cast<unsigned char>(text[pos]));
    col -= Charset::display_width(bytes(text) + pos, sl);
    pos += sl;
  }
  return pos;
}

// Keeps the viewport stable unless the cursor would fall outside it,
// in which case it scrolls the minimum amount (one character at a
// time) needed to bring the cursor back into view. Every editing
// method funnels through this single rule via redraw() -- legacy
// instead had ^E/^U/^Y/paste/insert always jump to a fresh right-
// aligned view (via need_scroll()) while ^B/^F scrolled incrementally,
// two different behaviors that also happened to be reimplemented
// slightly differently at each of their nine call sites. Unifying
// them here means Home/End/kill/yank/paste/typing all converge on the
// same, single, correct answer without needing any special-casing:
// e.g. moving the cursor to 0 naturally pulls shift_ down to 0 too
// (the first branch below), and moving it to the buffer's end
// naturally grows shift_ until the tail fits (the loop) -- which
// lands on the same shift a direct rightShift() computation would,
// just derived via the general rule instead of a Home/End-specific
// one.
void LineEditor::adjustViewport() {
  if (totalWidth(buffer_) <= width_) {
    shift_ = 0;
    return;
  }
  if (cursorPos_ < shift_) shift_ = cursorPos_;
  while (byteToColumn(shift_, cursorPos_) >= width_) shift_ += charBytesAt(shift_);
}

void LineEditor::renderVisible(const std::string &visible, int startBytePos) {
  bool splitStart = (encoding_ != Charset::Encoding::Utf8) &&
                     Charset::splits_character(bytes(buffer_), startBytePos, encoding_);
  int len = static_cast<int>(visible.size());
  int checkStart = splitStart ? 1 : 0;
  bool splitEnd = (encoding_ != Charset::Encoding::Utf8) &&
                   Charset::splits_character(bytes(visible) + checkStart, len - checkStart, encoding_);
  if (splitEnd) len--;

  int i = 0;
  if (splitStart) {
    display_.putChar(' ');
    i = 1;
  }
  for (; i < len; i++) {
    unsigned char c = static_cast<unsigned char>(visible[i]);
    // K_dl (0x7f, DEL) and K_at (0x80) are legacy sentinel values
    // repurposed to mean "show this byte reverse-video". K_at
    // collides with the single most common UTF-8 continuation byte
    // (see CLAUDE.md history item 15, the print_buff() bugfix this
    // is ported from) -- only special-case 0x80 outside UTF-8 mode,
    // carrying that fix forward rather than reintroducing the bug.
    if (c && (c < ' ' || c == 0x7f || (encoding_ != Charset::Encoding::Utf8 && c == 0x80))) {
      int shown = (c == 0x7f) ? c - 0x80 : (c == 0x80 ? 0 : c);
      display_.setAttribute(Display::Attribute::Standout);
      display_.putChar(shown + '@');
      display_.setAttribute(Display::Attribute::StandoutOff);
    } else {
      display_.putChar(c);
    }
  }
  if (splitEnd) display_.putChar(' ');
}

void LineEditor::redraw() {
  adjustViewport();

  std::string visible;
  int n = static_cast<int>(buffer_.size());
  if (encoding_ != Charset::Encoding::Utf8) {
    int avail = std::min(width_, n - shift_);
    if (avail < 0) avail = 0;
    visible = buffer_.substr(shift_, avail);
  } else {
    int pos = shift_, col = 0;
    while (pos < n) {
      int sl = Charset::sequence_length(static_cast<unsigned char>(buffer_[pos]));
      int w = Charset::display_width(bytes(buffer_) + pos, sl);
      if (col + w > width_) break;
      col += w;
      pos += sl;
    }
    visible = buffer_.substr(shift_, pos - shift_);
  }

  display_.setAttribute(Display::Attribute::StandoutOff);  // ported from key_eeol()'s vt_endstand()
  display_.moveCursor(row_, col0_);
  display_.write(std::string(width_, ' '));
  display_.moveCursor(row_, col0_);
  renderVisible(visible, shift_);

  // setVisibleCursor(), not moveCursor(): this IS the meaningful
  // editing caret (as opposed to every OTHER moveCursor() call in this
  // method, which only positions where the next glyph gets written) --
  // see Display::setVisibleCursor()'s own comment for why -g needs
  // this distinction to keep the cursor from getting silently dragged
  // elsewhere by a later, unrelated redraw.
  display_.setVisibleCursor(row_, col0_ + byteToColumn(shift_, cursorPos_));
  display_.flush();
}

void LineEditor::insertText(const std::string &bytesIn) {
  if (bytesIn.empty()) return;
  std::string toInsert = bytesIn;
  int overflow = static_cast<int>(buffer_.size() + toInsert.size()) - (kMaxLength - 1);
  if (overflow > 0) {
    display_.putChar(7);  // BEL, ported from vt_putchar(K_g)
    if (overflow >= static_cast<int>(toInsert.size())) return;
    toInsert.resize(toInsert.size() - overflow);
  }
  buffer_.insert(cursorPos_, toInsert);
  cursorPos_ += static_cast<int>(toInsert.size());
  redraw();
}

void LineEditor::insertByte(unsigned char byte) {
  if (encoding_ != Charset::Encoding::Utf8) {
    insertText(std::string(1, static_cast<char>(byte)));
    return;
  }
  if (pendingUtf8_.empty()) {
    // First byte of a (possibly multi-byte) sequence; reserve so the
    // length check below has something to compare against.
  }
  pendingUtf8_.push_back(static_cast<char>(byte));
  int want = Charset::sequence_length(static_cast<unsigned char>(pendingUtf8_[0]));
  if (static_cast<int>(pendingUtf8_.size()) < want) return;  // wait for more bytes
  std::string complete = pendingUtf8_;
  pendingUtf8_.clear();
  insertText(complete);
}

void LineEditor::backspace() {
  if (cursorPos_ <= 0) {
    display_.putChar(7);
    return;
  }
  int n = charBytesBefore(cursorPos_);
  buffer_.erase(cursorPos_ - n, n);
  cursorPos_ -= n;
  redraw();
}

void LineEditor::deleteForward() {
  if (cursorPos_ >= static_cast<int>(buffer_.size())) {
    display_.putChar(7);
    return;
  }
  int n = charBytesAt(cursorPos_);
  buffer_.erase(cursorPos_, n);
  redraw();
}

void LineEditor::moveHome() {
  cursorPos_ = 0;
  redraw();
}

void LineEditor::moveEnd() {
  cursorPos_ = static_cast<int>(buffer_.size());
  redraw();
}

void LineEditor::moveLeft() {
  if (cursorPos_ <= 0) {
    display_.putChar(7);
    return;
  }
  cursorPos_ -= charBytesBefore(cursorPos_);
  redraw();
}

void LineEditor::moveRight() {
  if (cursorPos_ >= static_cast<int>(buffer_.size())) {
    display_.putChar(7);
    return;
  }
  cursorPos_ += charBytesAt(cursorPos_);
  redraw();
}

void LineEditor::killToStart() {
  killBuffer_ = buffer_.substr(0, cursorPos_);
  buffer_.erase(0, cursorPos_);
  cursorPos_ = 0;
  redraw();
}

void LineEditor::killToEnd() {
  killBuffer_ = buffer_.substr(cursorPos_);
  buffer_.erase(cursorPos_);
  redraw();
}

void LineEditor::yank() { insertText(killBuffer_); }

void LineEditor::paste(const std::string &text) { insertText(text); }

void LineEditor::toggleQuotedInsert() { quotedInsertPending_ = !quotedInsertPending_; }

bool LineEditor::quotedInsertPending() const { return quotedInsertPending_; }

void LineEditor::clearQuotedInsert() { quotedInsertPending_ = false; }

}  // namespace fm
