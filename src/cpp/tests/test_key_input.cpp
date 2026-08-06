#include "doctest.h"
#include "key_input.hpp"

using fm::Display;
using fm::KeyInput;

namespace {

// Minimal Display stand-in: only keySequence() matters for KeyInput's
// own logic, so every other method is a harmless no-op/default.
class FakeDisplay : public Display {
 public:
  void write(const std::string &) override {}
  void putChar(int) override {}
  void flush() override {}
  void moveCursor(int, int) override {}
  void setVisibleCursor(int, int) override {}
  void hideVisibleCursor() override {}
  void cursorLeft() override {}
  void saveCursor() override {}
  void restoreCursor() override {}
  void clearScreen() override {}
  void clearToEndOfLine() override {}
  void scrollForward() override {}
  void scrollReverse() override {}
  void setAttribute(Attribute) override {}
  std::string attributeEscape(Attribute) const override { return ""; }
  int rows() const override { return rows_; }
  int columns() const override { return columns_; }
  void setSize(int r, int c) override { rows_ = r; columns_ = c; }
  std::string keySequence(Key key) const override {
    switch (key) {
      case Key::Up: return "\033[A";
      case Key::Down: return "\033[B";
      case Key::Right: return "\033[C";
      case Key::Left: return "\033[D";
      case Key::PageUp: return "\033[5~";
      case Key::PageDown: return "\033[6~";
    }
    return "";
  }
  bool outputEnabled() const override { return outputEnabled_; }
  void setOutputEnabled(bool e) override { outputEnabled_ = e; }
  bool smoothScrollEnabled() const override { return false; }
  void setSmoothScrollEnabled(bool) override {}
  void open() override {}
  void close() override {}
  void renderScrolled(const std::vector<std::string> &, int, int, const std::vector<ModeLineSegment> &) override {}

 private:
  int rows_ = 24, columns_ = 80;
  bool outputEnabled_ = false;
};

}  // namespace

TEST_CASE("readByte: returns queued macro bytes before falling through") {
  FakeDisplay d;
  KeyInput k(d);
  k.playRawMacro("ab");
  CHECK(k.readByte() == 'a');
  CHECK(k.readByte() == 'b');
  CHECK(k.isPlayingRawMacro() == false);
  // Queue exhausted and no tty opened -- falls through to "nothing
  // to read" (0), not a crash.
  CHECK(k.readByte() == 0);
}

TEST_CASE("isPlayingRawMacro reflects queue state") {
  FakeDisplay d;
  KeyInput k(d);
  CHECK(k.isPlayingRawMacro() == false);
  k.playRawMacro("x");
  CHECK(k.isPlayingRawMacro() == true);
  k.readByte();
  CHECK(k.isPlayingRawMacro() == false);
}

TEST_CASE("readEscapeSequence: recognizes each of the 6 keys") {
  FakeDisplay d;
  KeyInput k(d);

  k.playRawMacro("[A");
  CHECK(k.readEscapeSequence() == KeyInput::SpecialKey::Up);

  k.playRawMacro("[B");
  CHECK(k.readEscapeSequence() == KeyInput::SpecialKey::Down);

  k.playRawMacro("[C");
  CHECK(k.readEscapeSequence() == KeyInput::SpecialKey::Right);

  k.playRawMacro("[D");
  CHECK(k.readEscapeSequence() == KeyInput::SpecialKey::Left);

  // PageUp/PageDown are 4 bytes (\033[5~); readEscapeSequence() only
  // consumes 3 (matches Up's length) then discards one more trailing
  // byte itself -- feed all 3 remaining bytes.
  k.playRawMacro("[5~");
  CHECK(k.readEscapeSequence() == KeyInput::SpecialKey::PageUp);

  k.playRawMacro("[6~");
  CHECK(k.readEscapeSequence() == KeyInput::SpecialKey::PageDown);
}

TEST_CASE("readEscapeSequence: bare ESC with nothing following returns nullopt") {
  FakeDisplay d;
  KeyInput k(d);
  // No macro queued at all -- readByte() immediately returns 0
  // (timeout), matching a lone ESC keypress.
  CHECK(k.readEscapeSequence() == std::nullopt);
}

TEST_CASE("readEscapeSequence: unrecognized sequence returns nullopt, not Up") {
  // Regression for the fixed legacy ambiguity: is_esc() returning the
  // literal int 0 both for "no match" and "matched index 0 (Up)" made
  // read_esc()'s caller treat any unrecognized escape sequence as Up.
  FakeDisplay d;
  KeyInput k(d);
  k.playRawMacro("[Z");  // not one of the 6 known sequences
  CHECK(k.readEscapeSequence() == std::nullopt);
}

TEST_CASE("readEscapeSequence: no arrow-key capability at all returns nullopt") {
  class NoArrowsDisplay : public FakeDisplay {
   public:
    std::string keySequence(Key) const override { return ""; }
  };
  NoArrowsDisplay d;
  KeyInput k(d);
  k.playRawMacro("[A");
  CHECK(k.readEscapeSequence() == std::nullopt);
}

// lastEscapeBytes(): the fix for the gap Application's own class
// comment documents -- recovering an app-level ESC-leader byte (e.g.
// usilic's ^[s for SHELL) that doesn't match any of Display's known
// terminal sequences.

TEST_CASE("lastEscapeBytes: an unrecognized single-byte sequence (e.g. ^[s) is recoverable") {
  FakeDisplay d;
  KeyInput k(d);
  k.playRawMacro("s");  // ESC already consumed by the caller before calling readEscapeSequence()
  CHECK(k.readEscapeSequence() == std::nullopt);
  CHECK(k.lastEscapeBytes() == "s");
}

TEST_CASE("lastEscapeBytes: empty for a bare ESC with nothing following") {
  FakeDisplay d;
  KeyInput k(d);
  CHECK(k.readEscapeSequence() == std::nullopt);
  CHECK(k.lastEscapeBytes() == "");
}

TEST_CASE("lastEscapeBytes: empty when the terminal has no arrow-key capability at all") {
  class NoArrowsDisplay : public FakeDisplay {
   public:
    std::string keySequence(Key) const override { return ""; }
  };
  NoArrowsDisplay d;
  KeyInput k(d);
  k.playRawMacro("[A");
  CHECK(k.readEscapeSequence() == std::nullopt);
  CHECK(k.lastEscapeBytes() == "");
}

TEST_CASE("lastEscapeBytes: reflects the bytes read even when a real key WAS matched") {
  FakeDisplay d;
  KeyInput k(d);
  k.playRawMacro("[A");
  CHECK(k.readEscapeSequence() == KeyInput::SpecialKey::Up);
  CHECK(k.lastEscapeBytes() == "[A");
}

TEST_CASE("consumeResizeSignal: false with no signal pending") {
  FakeDisplay d;
  KeyInput k(d);
  CHECK(k.consumeResizeSignal() == false);
}

TEST_CASE("setErrorPending / setPidReporter accept values without crashing") {
  FakeDisplay d;
  KeyInput k(d);
  k.setErrorPending(true);
  int reported = -1;
  k.setPidReporter([&](int pid) { reported = pid; });
  k.setForegroundChildPid(1234);
  // These wire up state consumed by the process-wide signal
  // trampolines (SIGTTOU/SIGCONT), which aren't triggered here --
  // this just confirms the setters themselves are safe to call.
  CHECK(reported == -1);
}
