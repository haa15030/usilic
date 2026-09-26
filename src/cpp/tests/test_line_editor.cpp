#include <vector>

#include "doctest.h"
#include "line_editor.hpp"
#include "recording_display.hpp"

using fm::Charset;
using fm::Display;
using fm::LineEditor;
using fm::test::RecordingDisplay;

namespace {

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

TEST_CASE("selectAll: typing after a seeded prompt replaces the seed instead of appending") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 20);
  e.setText(".txt");
  CHECK(e.cursorBytePos() == 4);  // cursor lands at the end, as usual
  e.selectAll();
  typeAscii(e, ".md");
  CHECK(e.text() == ".md");  // not ".txt.md"
  CHECK(e.cursorBytePos() == 3);
}

TEST_CASE("selectAll: backspace/deleteForward on a pending selection clears the whole field") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 20);
  e.setText("preset");
  e.selectAll();
  e.backspace();
  CHECK(e.text() == "");
  CHECK(e.cursorBytePos() == 0);

  e.setText("preset");
  e.selectAll();
  e.deleteForward();
  CHECK(e.text() == "");
}

TEST_CASE("selectAll: paste also replaces the seed, matching typed insertion") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 20);
  e.setText(".txt");
  e.selectAll();
  e.paste(".md");
  CHECK(e.text() == ".md");
}

TEST_CASE("selectAll: navigation cancels the pending selection without deleting anything") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 20);
  e.setText(".txt");
  e.selectAll();
  e.moveHome();  // deliberately editing the seed, not replacing it
  typeAscii(e, "x");
  CHECK(e.text() == "x.txt");  // ordinary insert-at-cursor, seed left intact
}

TEST_CASE("selectAll: setText() (e.g. re-seeding for the next prompt) clears any pending selection") {
  RecordingDisplay d(3, 20);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 20);
  e.setText(".txt");
  e.selectAll();
  e.setText("fresh");
  typeAscii(e, "x");
  CHECK(e.text() == "freshx");  // ordinary append, no leftover selectAll from the previous seed
}

// 2026-09-26追加: M-f/M-b/M-d/M-DEL(単語編集)とC-_(undo)。
TEST_CASE("word motion: M-b/M-f stop at word boundaries, treating symbols as separators") {
  RecordingDisplay d(3, 40);
  LineEditor e(d, Charset::Encoding::Utf8);
  e.setField(0, 0, 40);
  e.setText("less ~/git/usilic");
  e.moveWordLeft();
  CHECK(e.cursorBytePos() == 11);  // "usilic"の先頭
  e.moveWordLeft();
  CHECK(e.cursorBytePos() == 7);  // "git"の先頭
  e.moveWordRight();
  CHECK(e.cursorBytePos() == 10);  // "git"の末尾
  e.moveHome();
  e.moveWordRight();
  CHECK(e.cursorBytePos() == 4);  // "less"の末尾
}

TEST_CASE("word motion: multibyte characters count as word characters") {
  RecordingDisplay d(3, 40);
  LineEditor e(d, Charset::Encoding::Utf8);
  e.setField(0, 0, 40);
  e.setText("cat 日本語.txt");
  e.moveWordLeft();  // "txt"の先頭
  e.moveWordLeft();  // "日本語"の先頭
  CHECK(e.cursorBytePos() == 4);
  e.moveWordRight();
  CHECK(e.cursorBytePos() == 4 + 9);
}

TEST_CASE("word kill: M-DEL/M-d go to the kill buffer and can be yanked back") {
  RecordingDisplay d(3, 40);
  LineEditor e(d, Charset::Encoding::Utf8);
  e.setField(0, 0, 40);
  e.setText("cp foo/bar baz");
  e.killWordBackward();
  CHECK(e.text() == "cp foo/bar ");
  e.yank();
  CHECK(e.text() == "cp foo/bar baz");
  e.moveHome();
  e.killWordForward();
  CHECK(e.text() == " foo/bar baz");
  e.moveEnd();
  e.yank();
  CHECK(e.text() == " foo/bar bazcp");
}

TEST_CASE("undo: typed characters group, other edits undo one at a time") {
  RecordingDisplay d(3, 40);
  LineEditor e(d, Charset::Encoding::Utf8);
  e.setField(0, 0, 40);
  typeAscii(e, "hello world");
  e.killWordBackward();
  CHECK(e.text() == "hello ");
  e.undo();
  CHECK(e.text() == "hello world");
  CHECK(e.cursorBytePos() == 11);
  e.undo();  // 連続した11文字の入力は1回分
  CHECK(e.text() == "");
  e.undo();  // もう取り消す物は無い(BEL)
  CHECK(e.text() == "");
}

TEST_CASE("undo: self-insertion groups at most 20 characters, like Emacs") {
  RecordingDisplay d(3, 40);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 40);
  typeAscii(e, "abcdefghijklmnopqrstuvwxy");  // 25文字
  e.undo();
  CHECK(e.text() == "abcdefghijklmnopqrst");
  e.undo();
  CHECK(e.text() == "");
}

TEST_CASE("undo: setText(\"\") and clearUndo() drop the history; other setText() is undoable") {
  RecordingDisplay d(3, 40);
  LineEditor e(d, Charset::Encoding::Ascii);
  e.setField(0, 0, 40);
  typeAscii(e, "abc");
  e.setText("/tmp");
  e.undo();
  CHECK(e.text() == "abc");
  e.setText("");
  e.undo();
  CHECK(e.text() == "");
  typeAscii(e, "x");
  e.clearUndo();
  e.undo();
  CHECK(e.text() == "x");
}
