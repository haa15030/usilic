#pragma once

#include <optional>
#include <string>
#include <vector>

#include "content_view.hpp"

namespace fm {

// EPUBモードの縦書き表示(2026-09-28追加、直接のユーザー依頼"縦書きもよろしく"、
// docs/ideas/IDEAS.mdの「EPUBビューア」段階2)の組版。本文の論理行(横書きの
// FileModeの行を元の行番号でつなぎ直したもの)を、1行=1列(全角2セル幅+行間
// 1セル)にして右から左へ並べる。
//
// 2026-09-28改訂、直接のユーザー要望("Bibiのように横方向へもスクロールが
// できればいいけどね"→"まずは1列ずつから始めよう"): 当初はページ(画面1枚分)を
// 縦に積んでContentViewの縦スクロールで送っていたが、それでは1列ずつ横へ
// ずらせない。列の並びを組んでおき(layoutEpubVerticalColumns())、「右端に
// どの列を置くか」を決めて画面1枚分だけ描く(renderEpubVerticalWindow())形に
// した。
//
//   - 句読点・括弧・ダッシュ・三点リーダー等は、Unicodeの縦書き用字形
//     (U+FE10〜、U+FE30〜)へ置き換える。長音「ー」は「｜」。！？：；は
//     置き換えない(縦書き用の字形を持たないフォントがあり、-gで「・」の
//     ような代わりの字になった――ユーザー報告)。回転が要る「：」等は-gの
//     描画側で回す(GraphicalDisplay::setVerticalTextHint()参照)。
//   - 半角の英数は2字までを1マスに横に並べる(縦中横)。他の半角文字は1字1マス(-gでは
//     3字以上続けば横倒し、layoutEpubVerticalColumns()参照)。
//   - 行頭禁則(、。」等で列を始めない)と行末禁則(「（等で列を終えない)は、
//     前の列の最後の字を次の列へ送る(追い出し)。
struct EpubVerticalSourceLine {
  std::string text;
  std::vector<AttributeRun> attributes;
  int rawLineNumber = 0;
};

struct EpubVerticalCell {
  std::string glyph;  // 画面上2セル分(全角1字、または半角2字/半角+空白)
  char32_t original = 0;
  std::optional<Display::Attribute> attribute;
};

struct EpubVerticalColumn {
  std::vector<EpubVerticalCell> cells;  // 上から。空行は空
  int rawLineNumber = 0;
};

// 1列の高さ`rows`で、論理行を列の並びへ組む。
// sideways(-g用、2026-09-29、直接のユーザー依頼"2文字以下の半角英数を縦中横は
// いいとしてそれ以上の文字数では横倒しにしないと文字幅が半分しかなくハイライトも
// 左半分となって不自然。CUTでは無理でもGUIではよろしく"): 3字以上続く半角(空白以外)
// は2字ずつ1マスに詰める。GraphicalDisplay::renderCell()が、上下にも半角が続く
// 半角2字のマスを90度回して描く。-cは従来どおり1字1マス。
std::vector<EpubVerticalColumn> layoutEpubVerticalColumns(const std::vector<EpubVerticalSourceLine> &lines, int rows,
                                                         bool sideways = false);

// 画面の幅`columns`に並ぶ列の数。
int epubVerticalColumnsPerScreen(int columns);

// columns[first]を右端に置いて、画面1枚分(rows行)を描く。行はカーソル行の
// ハイライトの対象外(neverHighlighted、横一列がハイライトされる――ユーザー報告)。
// 列の並び(2026-09-29改訂): 右からk番目の列は桁[screenColumns-3-3k, screenColumns-2-3k]、
// その右の桁screenColumns-1-3kが行間(傍線の置き場所)。
// sideLineMarker(-c用、2026-09-29): 下線(リンク)の字は下線を外し、その列の右の
// 行間の桁に"|"を置いて傍線にする(端末の下線は字ごとの横線で細切れになるため。
// Kindle/Kinoppy/ブックと同じく日本語の傍線は右側)。-gは下線のまま渡し、
// GraphicalDisplay側が縦線として描く。highlightColumn: 字を反転表示にする列(列
// カーソルがリンク/画像の列にある時、列の番号、-1で無し)。
std::vector<ContentView::Line> renderEpubVerticalWindow(const std::vector<EpubVerticalColumn> &columns, int first,
                                                        int rows, int screenColumns, bool sideLineMarker = false,
                                                        int highlightColumn = -1);

// 右からk番目の列の左の桁(画面に入らなければ負)。
int epubVerticalScreenColumn(int k, int screenColumns);

// 画面の桁(0始まり)が、右から何番目の列か。列の上でなければ-1。
int epubVerticalColumnAt(int screenColumn, int screenColumns);

}  // namespace fm
