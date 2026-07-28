#pragma once

#include <string>

#include "charset.hpp"
#include "display.hpp"

namespace fm {

// Single-line editable text field: insert/delete/cursor movement,
// kill/yank, quoted-insert, and redraw. Ported from key.c's
// edit_line()/add_buff() and the UTF-8 helpers next to them
// (char_bytes_before/b2col/utf8_total_width/utf8_right_shift/
// need_scroll), plus sub.c's print_buff() for rendering.
//
// This is NOT a literal 1:1 port. Legacy edit_line() made every
// caller (^U, ^K, ^Y, paste, backspace, delete-forward, ^B/^F cursor
// movement, add_buff's own insert path -- nine call sites in total)
// independently recompute and hand-mutate three byte-offset tracking
// variables (col/cole/shift) to stay in sync with what was on screen.
// Most of the UTF-8 bugs fixed earlier in this project's history
// (K_e, K_u, K_y, paste, backspace, delete-forward, cursor movement,
// TAB-completion scrolling, add_buff's own increment logic) trace
// back to exactly that pattern: nine slightly-different reimplementa-
// tions of the same byte-vs-column arithmetic, each capable of
// getting it wrong independently.
//
// Here, none of that tracking state is exposed. Every editing method
// only ever touches (buffer content, cursor byte offset) -- what's
// visible and where the cursor lands on screen is entirely
// recomputed from scratch by redraw(), in one place. This also
// unifies two behaviors legacy kept separate (and inconsistent): ^B/
// ^F scrolled the viewport by minimal increments as the cursor
// crossed its edge, while ^E/^U/^Y/paste/insert always jumped to a
// fresh right-aligned view regardless of where the cursor had been.
// redraw() here always does the former (keep the viewport stable,
// scroll the minimum needed to keep the cursor visible) -- a
// deliberate, and strictly more predictable, behavior change.
//
// Deliberately excluded (belongs to future classes, not LineEditor):
//   - input_key() (per-field-type meaning of Enter, path/tilde
//     expansion, TAB-triggers-completion wiring): Mode/Application's
//     job. LineEditor only edits text.
//   - make_list()/make_cmd() (TAB-completion candidate generation,
//     filesystem/history lookups): a future PathCompleter-type
//     class's job.
class LineEditor {
 public:
  LineEditor(Display &display, Charset::Encoding encoding);

  void setEncoding(Charset::Encoding encoding);

  // Where on screen this field lives and how wide it is (columns).
  void setField(int row, int col0, int width);

  // -- content --
  void setText(const std::string &text);
  const std::string &text() const;
  int cursorBytePos() const;

  // -- editing --
  // Feed one raw byte at a time (from KeyInput::readByte()). UTF-8
  // continuation bytes are buffered internally until a complete
  // character is available, exactly like legacy add_buff() -- calling
  // redraw() (or any Display write) with a torn multi-byte sequence
  // is what caused this project's very first LineEditor-era UTF-8 bug
  // (see CLAUDE.md history: the terminal renders an interrupted
  // sequence as a replacement character even though every byte's
  // value was correct).
  void insertByte(unsigned char byte);
  void backspace();
  void deleteForward();
  void moveHome();
  void moveEnd();
  void moveLeft();
  void moveRight();

  // ^U: kill from field start to cursor, into the kill buffer.
  void killToStart();
  // ^K: kill from cursor to field end, into the kill buffer.
  void killToEnd();
  // ^Y: re-insert the kill buffer at the cursor.
  void yank();
  // M_paste: insert external clipboard text at the cursor (does not
  // touch the ^U/^K kill buffer -- ported as a separate source, same
  // as legacy's `copy` parameter vs. its static `yank[]`).
  void paste(const std::string &text);

  // ^Q: the next byte handed to insertByte() is inserted literally
  // (rendered reverse-video by redraw() if it's a control byte)
  // instead of being interpreted as an editing command by whatever
  // calls this class (that dispatch itself is Mode/Application's
  // job -- this class only remembers that the toggle happened).
  void toggleQuotedInsert();
  bool quotedInsertPending() const;
  // Clears the pending flag once the caller has consumed it (fed the
  // following byte to insertByte()). Separate from toggleQuotedInsert()
  // so callers don't have to reason about "toggle twice to turn off".
  void clearQuotedInsert();

  // -- rendering --
  // Recomputes the visible window and cursor position from scratch
  // and redraws the field. Call after any edit, or to force a
  // redraw (e.g. after an external screen clear).
  void redraw();

 private:
  static constexpr int kMaxLength = 1024;  // N_line in the legacy C

  void insertText(const std::string &bytes);
  int charBytesBefore(int pos) const;  // bytes of the character ENDING at pos
  int charBytesAt(int pos) const;      // bytes of the character STARTING at pos
  int byteToColumn(int shift, int bytePos) const;
  int totalWidth(const std::string &text) const;
  int rightShift(const std::string &text, int fieldWidth) const;
  void adjustViewport();
  void renderVisible(const std::string &visible, int startBytePos);

  Display &display_;
  Charset::Encoding encoding_;

  std::string buffer_;
  int cursorPos_ = 0;   // byte offset into buffer_
  int shift_ = 0;       // byte offset of the leftmost visible byte

  int row_ = 0, col0_ = 0, width_ = 0;

  std::string killBuffer_;
  bool quotedInsertPending_ = false;

  // UTF-8 partial-sequence accumulator for insertByte(), across calls.
  std::string pendingUtf8_;
};

}  // namespace fm
