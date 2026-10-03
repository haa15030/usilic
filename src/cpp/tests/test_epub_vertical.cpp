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

// 2026-09-29: -cの傍線(下線の字の右の行間に"|")と、リンクの列の反転表示
TEST_CASE("renderEpubVerticalWindow: side line to the right of underlined text, highlighted column") {
  std::vector<fm::AttributeRun> link = {{0, 6, fm::Display::Attribute::Underline}};
  auto cols = fm::layoutEpubVerticalColumns({{"あい", link, 1}, {"う", {}, 2}}, 3);
  auto lines = fm::renderEpubVerticalWindow(cols, 0, 3, 9, /*sideLineMarker=*/true);
  CHECK(lines[0].text == "   う あ|");
  CHECK(lines[1].text == "      い|");
  CHECK(lines[0].attributes.empty());
  auto highlighted = fm::renderEpubVerticalWindow(cols, 0, 3, 9, false, /*highlightColumn=*/1);
  REQUIRE(highlighted[0].attributes.size() == 2);  // 「う」(列1)は反転、「あ」(列0)は下線のまま
  CHECK(highlighted[0].attributes[0].start == 3);
  CHECK(highlighted[0].attributes[0].attribute == fm::Display::Attribute::Standout);
  CHECK(highlighted[0].attributes[1].attribute == fm::Display::Attribute::Underline);
  CHECK(fm::epubVerticalScreenColumn(1, 9) == 3);
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

// 2026-09-29: 半角英数2字までは縦中横、-g(sideways)では3字以上の半角を2字ずつ1マスに、矢印は回した向きに
TEST_CASE("layoutEpubVerticalColumns: 2 alnum tate-chu-yoko, 3+ half-width packed in pairs when sideways, arrows") {
  CHECK(window({{"あOKい", {}, 1}}, 4, 3, 0) == std::vector<std::string>{"あ", "OK", "い", ""});
  auto cols = fm::layoutEpubVerticalColumns({{"abcde", {}, 1}}, 9, /*sideways=*/true);
  REQUIRE(cols.size() == 1);
  REQUIRE(cols[0].cells.size() == 3);
  CHECK(cols[0].cells[0].glyph == "ab");
  CHECK(cols[0].cells[2].glyph == "e ");
  CHECK(fm::layoutEpubVerticalColumns({{"abcde", {}, 1}}, 9)[0].cells.size() == 5);  // -cは1字1マス
  CHECK(window({{"→←", {}, 1}}, 2, 3, 0) == std::vector<std::string>{"↓", "↑"});
}

// 2026-10-03追加(-gの画像ページ、.steering/20261003-epub-image-page/): 画像だけの行は字として組まず、
// 画像ごとに印の列(字のマスが無く、画像の名前と元の行番号を持つ)を1本ずつ出す。
TEST_CASE("layoutEpubVerticalColumns: an image-only line becomes one marker column per image") {
  EpubVerticalSourceLine imageLine{"[image: a.jpg][image: b.png]", {}, 2};
  imageLine.images = {"a.jpg", "b.png"};
  std::vector<EpubVerticalSourceLine> lines = {{"あ", {}, 1}, imageLine, {"い", {}, 3}};
  auto cols = fm::layoutEpubVerticalColumns(lines, 4);
  REQUIRE(cols.size() == 4);
  CHECK(cols[0].image.empty());
  CHECK(cols[1].image == "a.jpg");
  CHECK(cols[2].image == "b.png");
  CHECK(cols[1].cells.empty());
  CHECK(cols[1].rawLineNumber == 2);
  CHECK(cols[2].rawLineNumber == 2);
  CHECK(cols[3].image.empty());
}
