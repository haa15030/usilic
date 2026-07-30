#pragma once

#include <string>
#include <vector>

#include "display.hpp"

namespace fm {
namespace test {

// A Display that behaves like a real terminal screen buffer (a grid
// of characters the cursor writes into and moves around on), so a
// class's actual rendering output can be asserted on directly -- the
// same idea as this project's PTY+pyte smoke tests, just in memory
// and without a real terminal. Shared across LineEditor's and
// ContentView's tests.
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
  void scrollForward() override { scrollForwardCount_++; }
  void scrollReverse() override { scrollReverseCount_++; }
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
  bool smoothScrollEnabled() const override { return smoothScroll_; }
  void setSmoothScrollEnabled(bool enabled) override { smoothScroll_ = enabled; }
  void open() override {}
  void close() override {}

  // Same row-granular shape as TerminalDisplay's real implementation
  // (see its own comment) -- ignores pixelOffsetY, reuses this
  // class's own write()/moveCursor()/etc. so tests can assert on
  // rowText()/standoutAt() same as any other rendering path.
  void renderScrolled(const std::vector<std::string> &lines, int topIndex, int pixelOffsetY,
                       const std::string &modeLineText) override {
    (void)pixelOffsetY;
    int contentRows = rows_ > 0 ? rows_ - 1 : 0;
    for (int row = 0; row < contentRows; row++) {
      int lineIndex = topIndex + row;
      moveCursor(row, 0);
      clearToEndOfLine();
      if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) write(lines[lineIndex]);
    }
    if (rows_ > 0) {
      moveCursor(contentRows, 0);
      clearToEndOfLine();
      setAttribute(Attribute::Standout);
      write(modeLineText);
      setAttribute(Attribute::StandoutOff);
    }
  }

  const std::string &rowText(int row) const { return screen_[row]; }
  int cursorRow() const { return cursorRow_; }
  int cursorCol() const { return cursorCol_; }
  bool standoutAt(int row, int col) const { return standout_[row][col]; }
  int scrollForwardCount() const { return scrollForwardCount_; }
  int scrollReverseCount() const { return scrollReverseCount_; }

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
  bool smoothScroll_ = false;
  int scrollForwardCount_ = 0, scrollReverseCount_ = 0;
};

}  // namespace test
}  // namespace fm
