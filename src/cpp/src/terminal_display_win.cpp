// Windowsのネイティブ(MinGW-w64、windows.mk、.steering/20261015-windows-native-gui/)のTerminalDisplay。
// Windowsのusilic.exeは常に-g(SDLの窓)で、main.cppが作るTerminalDisplayはDelegatingDisplayの控えに
// すぎない(画面へは何も出さない)。cursesも端末も使わないので、大きさ等の値を持つだけの実装にする。
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "terminal_display.hpp"

namespace fm {

TerminalDisplay::TerminalDisplay(TerminalCapabilities caps, int rows, int columns)
    : caps_(std::move(caps)), rows_(rows), columns_(columns) {}

std::unique_ptr<TerminalDisplay> TerminalDisplay::fromEnvironment(bool exitOnFailure) {
  return std::make_unique<TerminalDisplay>(TerminalCapabilities{}, 24, 80);
}

void TerminalDisplay::write(const std::string &text) {}
void TerminalDisplay::putChar(int c) {}
void TerminalDisplay::flush() {}
void TerminalDisplay::moveCursor(int row, int col) {}
void TerminalDisplay::cursorLeft() {}
void TerminalDisplay::saveCursor() {}
void TerminalDisplay::restoreCursor() {}
void TerminalDisplay::setVisibleCursor(int row, int col) {}
void TerminalDisplay::hideVisibleCursor() {}
void TerminalDisplay::clearScreen() {}
void TerminalDisplay::clearToEndOfLine() {}
void TerminalDisplay::scrollForward() {}
void TerminalDisplay::scrollReverse() {}
void TerminalDisplay::setAttribute(Attribute attr) {}
std::string TerminalDisplay::attributeEscape(Attribute attr) const { return ""; }
void TerminalDisplay::setColor(int fg, int bg) {}
int TerminalDisplay::rows() const { return rows_; }
int TerminalDisplay::columns() const { return columns_; }
void TerminalDisplay::setSize(int rows, int columns) {
  rows_ = rows;
  columns_ = columns;
}
std::string TerminalDisplay::keySequence(Key key) const { return ""; }
bool TerminalDisplay::outputEnabled() const { return outputEnabled_; }
void TerminalDisplay::setOutputEnabled(bool enabled) { outputEnabled_ = enabled; }
bool TerminalDisplay::smoothScrollEnabled() const { return smoothScroll_; }
void TerminalDisplay::setSmoothScrollEnabled(bool enabled) { smoothScroll_ = enabled; }
bool TerminalDisplay::modeLineVisible() const { return modeLineVisible_; }
void TerminalDisplay::setModeLineVisible(bool visible) { modeLineVisible_ = visible; }
void TerminalDisplay::open() {}
void TerminalDisplay::close() {}
void TerminalDisplay::leaveForExternalProgram() {}
void TerminalDisplay::reenterFromExternalProgram() {}
void TerminalDisplay::leaveForSuspend() {}
void TerminalDisplay::renderScrolled(const std::vector<ScrolledLine> &lines, int topIndex, int pixelOffsetY,
                                     const std::vector<Display::ModeLineSegment> &modeLineSegments) {}
std::pair<int, int> TerminalDisplay::scrollRegionArgs() const { return {0, rows_ - 1}; }

}  // namespace fm
