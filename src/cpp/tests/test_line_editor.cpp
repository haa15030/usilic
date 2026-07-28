#include <vector>

#include "doctest.h"
#include "line_editor.hpp"

using fm::Charset;
using fm::Display;
using fm::LineEditor;

namespace {

// A Display that behaves like a real terminal screen buffer (a grid
// of characters the cursor writes into and moves around on), so
// LineEditor's actual rendering output can be asserted on directly --
// the same idea as this project's PTY+pyte smoke tests, just in
// memory and without a real terminal.
class RecordingDisplay : public Display {
 public:
  RecordingDisplay(int rows, int cols)
      : rows_(rows), cols_(cols),
        screen_(rows, std::string(cols, ' ')),
        standout_(rows, std::vector<bool>(cols, false)) {}

  void write(const std::string &text) override {
    for (char c : text) putAdvance(static_cast<unsigned char>(c));
  }
  void putChar(int c) override { putAdvance(c); }
  void flush() override {}
  void moveCursor(int row, int col) override { cursorRow_ = row; cursorCol_ = col; }
  void cursorLeft() override { if (cursorCol_ > 0) cursorCol_--; }
  void saveCursor() override {}
  void restoreCursor() override {}
  void clearScreen() override { for (auto &r : screen_) r.assign(cols_, ' '); }
  void clearToEndOfLine() override {
    for (int c = cursorCol_; c < cols_; c++) screen_[cursorRow_][c] = ' ';
  }
  void scrollForward() override {}
  void scrollReverse() override {}
  void setAttribute(Attribute attr) override {
    standoutActive_ = (attr == Attribute::Standout) ? true
                     : (attr == Attribute::StandoutOff) ? false : standoutActive_;
  }
  std::string attributeEscape(Attribute) const override { return ""; }
  int rows() const override { return rows_; }
  int columns() const override { return cols_; }
  void setSize(int r, int c) override { rows_ = r; cols_ = c; }
  std::string keySequence(Key) const override { return ""; }
  bool outputEnabled() const override { return true; }
  void setOutputEnabled(bool) override {}
  bool smoothScrollEnabled() const override { return false; }
  void setSmoothScrollEnabled(bool) override {}
  void open() override {}
  void close() override {}

  const std::string &rowText(int row) const { return screen_[row]; }
  int cursorRow() const { return cursorRow_; }
  int cursorCol() const { return cursorCol_; }
  bool standoutAt(int row, int col) const { return standout_[row][col]; }

 private:
  void putAdvance(int c) {
    if (cursorRow_ >= 0 && cursorRow_ < rows_ && cursorCol_ >= 0 && cursorCol_ < cols_) {
      screen_[cursorRow_][cursorCol_] = static_cast<char>(c);
      standout_[cursorRow_][cursorCol_] = standoutActive_;
    }
    cursorCol_++;
  }

  int rows_, cols_;
  std::vector<std::string> screen_;
  std::vector<std::vector<bool>> standout_;
  int cursorRow_ = 0, cursorCol_ = 0;
  bool standoutActive_ = false;
};

void typeAscii(LineEditor &e, const std::string &s) {
  for (unsigned char c : s) e.insertByte(c);
}

}  // namespace

TEST_CASE("basic ASCII insert and cursor position") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(1, 2, 10);
  typeAscii(e, "hi");
  CHECK(d.rowText(1).substr(2, 2) == "hi");
  CHECK(d.cursorCol() == 4);  // col0(2) + 2 chars
  CHECK(e.cursorBytePos() == 2);
}

TEST_CASE("backspace removes the last character and moves cursor back") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 10);
  typeAscii(e, "abc");
  e.backspace();
  CHECK(e.text() == "ab");
  CHECK(e.cursorBytePos() == 2);
  CHECK(d.rowText(0)[2] == ' ');  // trailing cell cleared
}

TEST_CASE("backspace at start beeps instead of underflowing") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 10);
  e.backspace();
  CHECK(e.text() == "");
  CHECK(e.cursorBytePos() == 0);
}

TEST_CASE("UTF-8: a 3-byte character only renders once complete") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Utf8);
  e.setField(0, 0, 10);
  // "あ" = e3 81 82
  e.insertByte(0xe3);
  CHECK(e.text().empty());  // still buffering, not yet inserted
  e.insertByte(0x81);
  CHECK(e.text().empty());
  e.insertByte(0x82);
  CHECK(e.text() == "\xe3\x81\x82");
  CHECK(e.cursorBytePos() == 3);
  CHECK(d.cursorCol() == 2);  // "あ" is a 2-column-wide character
}

TEST_CASE("UTF-8: moveLeft/moveRight cross a whole character, not one byte") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Utf8);
  e.setField(0, 0, 10);
  typeAscii(e, "\xe3\x81\x82");  // "あ", fed as complete already-formed bytes
  CHECK(e.cursorBytePos() == 3);
  e.moveLeft();
  CHECK(e.cursorBytePos() == 0);  // one character back = 3 bytes, not 1
  e.moveRight();
  CHECK(e.cursorBytePos() == 3);
}

TEST_CASE("UTF-8: 0x80 continuation byte is NOT shown reverse-video") {
  // Regression for the historical print_buff() K_at(0x80) bug (see
  // CLAUDE.md history item 15): full-width space U+3000 = e3 80 80 --
  // both trailing bytes are literally 0x80, the legacy sentinel value.
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Utf8);
  e.setField(0, 0, 10);
  typeAscii(e, "\xe3\x80\x80");
  CHECK(d.rowText(0).substr(0, 3) == "\xe3\x80\x80");
  CHECK(d.standoutAt(0, 1) == false);
  CHECK(d.standoutAt(0, 2) == false);
}

TEST_CASE("control byte renders reverse-video as letter+'@'") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 10);
  e.insertByte(1);  // Ctrl-A
  CHECK(d.rowText(0)[0] == static_cast<char>(1 + '@'));  // 'A'
  CHECK(d.standoutAt(0, 0) == true);
}

TEST_CASE("ASCII: viewport scrolls to keep the cursor visible when content overflows") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 5);  // narrow field
  typeAscii(e, "abcdefgh");  // 8 chars into a 5-wide field
  CHECK(e.text() == "abcdefgh");
  // Cursor (at the end) must still be visible within the field.
  CHECK(d.cursorCol() < 5);
  CHECK(d.cursorCol() >= 0);
  // The visible window should show the tail, ending in 'h'.
  CHECK(d.rowText(0)[d.cursorCol() - 1] == 'h');
}

TEST_CASE("moveHome scrolls back to the start of a long buffer") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 5);
  typeAscii(e, "abcdefgh");
  e.moveHome();
  CHECK(e.cursorBytePos() == 0);
  CHECK(d.cursorCol() == 0);
  CHECK(d.rowText(0)[0] == 'a');
}

TEST_CASE("killToStart (^U) then yank (^Y) round-trips the killed text") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 10);
  typeAscii(e, "hello");
  e.moveLeft();
  e.moveLeft();  // cursor between "hel" and "lo"
  e.killToStart();
  CHECK(e.text() == "lo");
  CHECK(e.cursorBytePos() == 0);
  e.yank();
  CHECK(e.text() == "hello");
}

TEST_CASE("killToEnd (^K) removes from cursor to the end") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 10);
  typeAscii(e, "hello");
  e.moveLeft();
  e.moveLeft();
  e.killToEnd();
  CHECK(e.text() == "hel");
}

TEST_CASE("paste inserts external text without touching the kill buffer") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 20);
  typeAscii(e, "ab");
  e.killToStart();  // kill buffer now holds "ab"
  e.paste("XYZ");
  CHECK(e.text() == "XYZ");
  e.yank();
  CHECK(e.text() == "XYZab");  // yank still re-inserts the killed "ab", untouched by paste
}

TEST_CASE("setText loads pre-filled content with cursor at the end") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 10);
  e.setText("preset");
  CHECK(e.text() == "preset");
  CHECK(e.cursorBytePos() == 6);
}

TEST_CASE("quoted-insert pending flag toggles and clears") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  CHECK(e.quotedInsertPending() == false);
  e.toggleQuotedInsert();
  CHECK(e.quotedInsertPending() == true);
  e.clearQuotedInsert();
  CHECK(e.quotedInsertPending() == false);
}
