#include "doctest.h"
#include "epub_vertical.hpp"

// 2026-09-28追加: layoutEpubVertical()(EPUBの縦書きの組版)の単体テスト。

using fm::EpubVerticalSourceLine;
using fm::layoutEpubVertical;

TEST_CASE("layoutEpubVertical: columns run right to left, one column per line, pages are exactly `rows` tall") {
  // 幅8桁 → 列は(8+1)/3=3本/ページ、右端の列は桁6-7、次は3-4、次は0-1
  auto layout = layoutEpubVertical({{"あい", {}, 1}, {"う", {}, 2}, {"", {}, 3}, {"え", {}, 4}}, 2, 8);
  CHECK(layout.columnsPerPage == 3);
  REQUIRE(layout.rows.size() == 4);  // 2ページ×2行
  CHECK(layout.rows[0].text == "   う あ");
  CHECK(layout.rows[1].text == "      い");
  CHECK(layout.rows[2].text == "      え");
  CHECK(layout.rows[3].text == "");
  CHECK(layout.pageColumnRawLines == std::vector<std::vector<int>>{{1, 2, 3}, {4}});
  CHECK(layout.rows[0].rawLineNumber == 1);
  CHECK(layout.rows[2].rawLineNumber == 4);
}

TEST_CASE("layoutEpubVertical: vertical forms, tate-chu-yoko for up to 2 digits, chapter mark dropped") {
  auto layout = layoutEpubVertical({{"「第10章」。ー\xE2\x81\xA0   ", {}, 1}}, 8, 2);
  REQUIRE(layout.rows.size() == 8);
  CHECK(layout.rows[0].text == "﹁");
  CHECK(layout.rows[1].text == "第");
  CHECK(layout.rows[2].text == "10");
  CHECK(layout.rows[3].text == "章");
  CHECK(layout.rows[4].text == "﹂");
  CHECK(layout.rows[5].text == "︒");
  CHECK(layout.rows[6].text == "｜");
  CHECK(layout.rows[7].text == "");
}

TEST_CASE("layoutEpubVertical: a column never starts with 。 (the previous character moves along)") {
  // 高さ2: 「あい。」→ 1列目「あ」、2列目「い。」
  auto layout = layoutEpubVertical({{"あい。", {}, 1}}, 2, 5);
  REQUIRE(layout.rows.size() == 2);
  CHECK(layout.rows[0].text == "い あ");
  CHECK(layout.rows[1].text == "︒");
}

TEST_CASE("epubVerticalColumnAt: maps a screen column to the column counted from the right") {
  CHECK(fm::epubVerticalColumnAt(7, 8, 3) == 0);
  CHECK(fm::epubVerticalColumnAt(6, 8, 3) == 0);
  CHECK(fm::epubVerticalColumnAt(5, 8, 3) == -1);
  CHECK(fm::epubVerticalColumnAt(4, 8, 3) == 1);
  CHECK(fm::epubVerticalColumnAt(0, 8, 3) == 2);
}
