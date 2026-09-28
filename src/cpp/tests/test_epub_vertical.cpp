#include "doctest.h"
#include "epub_vertical.hpp"

// 2026-09-28追加: EPUBの縦書きの組版(epub_vertical.cpp)の単体テスト。

using fm::EpubVerticalSourceLine;

namespace {
std::vector<std::string> window(const std::vector<EpubVerticalSourceLine> &lines, int rows, int columns, int first) {
  auto cols = fm::layoutEpubVerticalColumns(lines, rows);
  std::vector<std::string> out;
  for (const auto &line : fm::renderEpubVerticalWindow(cols, first, rows, columns)) out.push_back(line.text);
  return out;
}
}  // namespace

TEST_CASE("renderEpubVerticalWindow: columns run right to left from `first`, one column per line") {
  // 幅9桁 → 3列/画面(右端の列は桁6-7で、桁8はその右の行間。次は3-4、次は0-1)
  std::vector<EpubVerticalSourceLine> lines = {{"あい", {}, 1}, {"う", {}, 2}, {"", {}, 3}, {"え", {}, 4}};
  CHECK(fm::epubVerticalColumnsPerScreen(9) == 3);
  CHECK(window(lines, 2, 9, 0) == std::vector<std::string>{"   う あ", "      い"});
  // 1列ずつ横へずらせる
  CHECK(window(lines, 2, 9, 1) == std::vector<std::string>{"え    う", ""});
  auto cols = fm::layoutEpubVerticalColumns(lines, 2);
  REQUIRE(cols.size() == 4);
  CHECK(cols[3].rawLineNumber == 4);
  CHECK(fm::renderEpubVerticalWindow(cols, 0, 2, 9)[0].neverHighlighted);
}

// 2026-09-29: -cの傍線(下線の字の右の行間に"|")と、列カーソルの反転表示(画面の高さいっぱい)
TEST_CASE("renderEpubVerticalWindow: side line to the right of underlined text, full-height cursor column") {
  std::vector<fm::AttributeRun> link = {{0, 6, fm::Display::Attribute::Underline}};
  auto cols = fm::layoutEpubVerticalColumns({{"あい", link, 1}, {"う", {}, 2}}, 3);
  auto lines = fm::renderEpubVerticalWindow(cols, 0, 3, 9, /*sideLineMarker=*/true);
  CHECK(lines[0].text == "   う あ|");
  CHECK(lines[1].text == "      い|");
  CHECK(lines[0].attributes.empty());
  auto cursor = fm::renderEpubVerticalWindow(cols, 0, 3, 9, false, /*cursorColumn=*/1);
  CHECK(cursor[2].text == "     ");  // 字の無い段も列カーソルの2桁は反転
  REQUIRE(cursor[2].attributes.size() == 1);
  CHECK(cursor[2].attributes[0].start == 3);
  CHECK(cursor[2].attributes[0].attribute == fm::Display::Attribute::Standout);
}

TEST_CASE("layoutEpubVerticalColumns: vertical forms, tate-chu-yoko for up to 2 digits, ！？ kept, chapter mark dropped") {
  CHECK(window({{"「第10章」。ー？\xE2\x81\xA0   ", {}, 1}}, 9, 3, 0) ==
        std::vector<std::string>{"﹁", "第", "10", "章", "﹂", "︒", "｜", "？", ""});
}

TEST_CASE("layoutEpubVerticalColumns: a column never starts with 。 (the previous character moves along)") {
  CHECK(window({{"あい。", {}, 1}}, 2, 6, 0) == std::vector<std::string>{"い あ", "︒"});
}

TEST_CASE("epubVerticalColumnAt: maps a screen column to the column counted from the right") {
  CHECK(fm::epubVerticalColumnAt(8, 9) == -1);  // 右端の列の右の行間
  CHECK(fm::epubVerticalColumnAt(7, 9) == 0);
  CHECK(fm::epubVerticalColumnAt(6, 9) == 0);
  CHECK(fm::epubVerticalColumnAt(5, 9) == -1);
  CHECK(fm::epubVerticalColumnAt(4, 9) == 1);
  CHECK(fm::epubVerticalColumnAt(0, 9) == 2);
}
