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
  // 幅8桁 → 3列/画面(右端の列は桁6-7、次は3-4、次は0-1)
  std::vector<EpubVerticalSourceLine> lines = {{"あい", {}, 1}, {"う", {}, 2}, {"", {}, 3}, {"え", {}, 4}};
  CHECK(fm::epubVerticalColumnsPerScreen(8) == 3);
  CHECK(window(lines, 2, 8, 0) == std::vector<std::string>{"   う あ", "      い"});
  // 1列ずつ横へずらせる
  CHECK(window(lines, 2, 8, 1) == std::vector<std::string>{"え    う", ""});
  auto cols = fm::layoutEpubVerticalColumns(lines, 2);
  REQUIRE(cols.size() == 4);
  CHECK(cols[3].rawLineNumber == 4);
  CHECK(fm::renderEpubVerticalWindow(cols, 0, 2, 8)[0].neverHighlighted);
}

TEST_CASE("layoutEpubVerticalColumns: vertical forms, tate-chu-yoko for up to 2 digits, ！？ kept, chapter mark dropped") {
  CHECK(window({{"「第10章」。ー？\xE2\x81\xA0   ", {}, 1}}, 9, 2, 0) ==
        std::vector<std::string>{"﹁", "第", "10", "章", "﹂", "︒", "｜", "？", ""});
}

TEST_CASE("layoutEpubVerticalColumns: a column never starts with 。 (the previous character moves along)") {
  CHECK(window({{"あい。", {}, 1}}, 2, 5, 0) == std::vector<std::string>{"い あ", "︒"});
}

TEST_CASE("epubVerticalColumnAt: maps a screen column to the column counted from the right") {
  CHECK(fm::epubVerticalColumnAt(7, 8) == 0);
  CHECK(fm::epubVerticalColumnAt(6, 8) == 0);
  CHECK(fm::epubVerticalColumnAt(5, 8) == -1);
  CHECK(fm::epubVerticalColumnAt(4, 8) == 1);
  CHECK(fm::epubVerticalColumnAt(0, 8) == 2);
}
