#pragma once

#include <string>
#include <vector>

#include "content_view.hpp"

namespace fm {

// EPUBモードの縦書き表示(2026-09-28追加、直接のユーザー依頼"縦書きもよろしく"、
// docs/ideas/IDEAS.mdの「EPUBビューア」段階2)の組版。本文の論理行(横書きの
// FileModeの行を元の行番号でつなぎ直したもの)を、1行=1列(全角2セル幅+行間
// 1セル)で右から左へ並べ、画面1枚分(rows行)ずつのページにして、ページを縦に
// 積んだ「画面の行」の列として返す――ContentViewにそのまま渡せば、既存の
// ページ送り(pageDown()はちょうど画面の高さずつ動く)・再描画・モードラインが
// そのまま使える(Application::rebuildEpubVertical()参照)。
//
//   - 句読点・括弧・ダッシュ・三点リーダー等は、Unicodeの縦書き用字形
//     (U+FE10〜、U+FE30〜)へ置き換える。長音「ー」は「｜」。
//   - 半角の数字は2桁までを1マスに横に並べる(縦中横)。他の半角文字は1字1マス。
//   - 行頭禁則(、。」等で列を始めない)と行末禁則(「（等で列を終えない)は、
//     前の列の最後の字を次の列へ送る(追い出し)。
struct EpubVerticalSourceLine {
  std::string text;
  std::vector<AttributeRun> attributes;
  int rawLineNumber = 0;
};

struct EpubVerticalLayout {
  std::vector<ContentView::Line> rows;             // ページ×rows行。rawLineNumberはそのページの最初の列の行
  std::vector<std::vector<int>> pageColumnRawLines;  // ページごと、右から数えた列ごとの元の行番号
  int rowsPerPage = 0;
  int columnsPerPage = 0;
};

EpubVerticalLayout layoutEpubVertical(const std::vector<EpubVerticalSourceLine> &lines, int rows, int columns);

// 画面の列(0始まりの桁)が、そのページの右から何番目の列か。列の上でなければ-1。
int epubVerticalColumnAt(int screenColumn, int columns, int columnsPerPage);

}  // namespace fm
