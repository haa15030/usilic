#include "doctest.h"
#include "terminal_display.hpp"

using fm::Display;
using fm::TerminalCapabilities;
using fm::TerminalDisplay;

namespace {

TerminalCapabilities fakeCaps() {
  TerminalCapabilities c;
  c.cursorAddress = "\033[%i%p1%d;%p2%dH";
  c.standoutOn = "\033[7m";
  c.standoutOff = "\033[27m";
  c.boldOn = "\033[1m";
  c.boldOff = "\033[0m";
  // deliberately no underlineOn/underlineOff -- exercise the
  // fall-back-to-standout path.
  c.changeScrollRegion = "\033[%i%p1%d;%p2%dr";
  return c;
}

}  // namespace

// TerminalDisplay's I/O methods (write/moveCursor/clearScreen/etc.)
// all guard on outputEnabled() before touching any real terminal
// state, and the constructor used here leaves outputEnabled() false
// by default -- so every test below is guaranteed to perform zero
// real I/O (no tputs/setupterm calls), matching the coverage boundary
// documented in terminal_display.hpp.

TEST_CASE("attributeEscape: direct capabilities") {
  TerminalDisplay d(fakeCaps(), 24, 80);
  CHECK(d.attributeEscape(Display::Attribute::Bold) == "\033[1m");
  CHECK(d.attributeEscape(Display::Attribute::BoldOff) == "\033[0m");
  CHECK(d.attributeEscape(Display::Attribute::Standout) == "\033[7m");
  CHECK(d.attributeEscape(Display::Attribute::StandoutOff) == "\033[27m");
}

TEST_CASE("attributeEscape: underline falls back to standout when unsupported") {
  TerminalDisplay d(fakeCaps(), 24, 80);
  CHECK(d.attributeEscape(Display::Attribute::Underline) == "\033[7m");
  CHECK(d.attributeEscape(Display::Attribute::UnderlineOff) == "\033[27m");
}

TEST_CASE("attributeEscape: missing capability returns empty, not null/crash") {
  TerminalCapabilities c;  // everything empty
  TerminalDisplay d(c, 24, 80);
  CHECK(d.attributeEscape(Display::Attribute::Bold) == "");
  CHECK(d.attributeEscape(Display::Attribute::Underline) == "");
}

TEST_CASE("keySequence: direct lookups") {
  TerminalCapabilities c = fakeCaps();
  c.keyUp = "\033OA";
  c.keyPageDown = "\033[6~";
  TerminalDisplay d(c, 24, 80);
  CHECK(d.keySequence(Display::Key::Up) == "\033OA");
  CHECK(d.keySequence(Display::Key::PageDown) == "\033[6~");
  CHECK(d.keySequence(Display::Key::Down) == "");  // unset in fakeCaps()
}

TEST_CASE("rows/columns/setSize") {
  // 2026-09-13修正、項目459フィードバック("スクロールバー...それ用の
  // エリアが必要...幅は倍(二文字分)")を受けて、columns()自身の.cpp側
  // コメント参照――公開columns()は生の端末幅から常に
  // kScrollbarStripWidth(2)を差し引いて報告する。
  TerminalDisplay d(fakeCaps(), 24, 80);
  CHECK(d.rows() == 24);
  CHECK(d.columns() == 78);  // 80 - 2 (常時予約されるスクロールバー用の2列)
  d.setSize(50, 120);
  CHECK(d.rows() == 50);
  CHECK(d.columns() == 118);  // 120 - 2
}

TEST_CASE("outputEnabled defaults to false and is settable") {
  TerminalDisplay d(fakeCaps(), 24, 80);
  CHECK(d.outputEnabled() == false);
  d.setOutputEnabled(true);
  CHECK(d.outputEnabled() == true);
}

TEST_CASE("smoothScrollEnabled defaults to false and is settable") {
  TerminalDisplay d(fakeCaps(), 24, 80);
  CHECK(d.smoothScrollEnabled() == false);
  d.setSmoothScrollEnabled(true);
  CHECK(d.smoothScrollEnabled() == true);
}

TEST_CASE("scrollRegionArgs: rows-2 by default, rows-1 with smooth scroll") {
  TerminalDisplay d(fakeCaps(), 24, 80);
  auto [top, bottom] = d.scrollRegionArgs();
  CHECK(top == 22);   // 24 - 2
  CHECK(bottom == 0);

  d.setSmoothScrollEnabled(true);
  auto [top2, bottom2] = d.scrollRegionArgs();
  CHECK(top2 == 23);  // 24 - 1
  CHECK(bottom2 == 0);
}

TEST_CASE("scrollRegionArgs follows setSize") {
  TerminalDisplay d(fakeCaps(), 24, 80);
  d.setSize(40, 100);
  auto [top, bottom] = d.scrollRegionArgs();
  CHECK(top == 38);  // 40 - 2
  CHECK(bottom == 0);
}

TEST_CASE("modeLineVisible defaults to true and is settable") {
  TerminalDisplay d(fakeCaps(), 24, 80);
  CHECK(d.modeLineVisible() == true);
  d.setModeLineVisible(false);
  CHECK(d.modeLineVisible() == false);
}

TEST_CASE("hasNativeScrollbar is always false -- a real terminal has no scrollbar of its own") {
  TerminalDisplay d(fakeCaps(), 24, 80);
  CHECK(d.hasNativeScrollbar() == false);
}

TEST_CASE("scrollRegionArgs: mode line hidden reserves zero rows regardless of smooth scroll") {
  // 24 - 1(smoothScrollEnabled()有効時と同じ値、自身の宣言コメント
  // 参照)であって24そのものでは「ない」ことに注意: 実際のPTY
  // キャプチャで確認済みの、applyScrollRegion()自身のtgoto()呼び出し
  // 経由の2整数引数の入れ替え+%iによる1加算を踏まえた値。
  TerminalDisplay d(fakeCaps(), 24, 80);
  d.setModeLineVisible(false);
  auto [top, bottom] = d.scrollRegionArgs();
  CHECK(top == 23);  // 24 - 1, full screen, no row reserved for the mode line
  CHECK(bottom == 0);

  d.setSmoothScrollEnabled(true);
  auto [top2, bottom2] = d.scrollRegionArgs();
  CHECK(top2 == 23);  // smooth scroll's own 1-vs-2 distinction is moot once nothing is reserved

  d.setModeLineVisible(true);
  auto [top3, bottom3] = d.scrollRegionArgs();
  CHECK(top3 == 23);  // 24 - 1, back to the smooth-scroll-enabled case
}
