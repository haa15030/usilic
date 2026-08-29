#pragma once

#include <string>

namespace fm {

// legacyのdisp.cのprint_line()――「モードラインの現在行/総行」――の
// うち、スクロール位置の部分だけを切り出したもの。意図的にこの1点
// だけにしている: print_line()/disp_window()/disp_prev()/disp_copy()は
// 他にも、現在のパスのパンくず、前のモードの文字列、「Mark set」
// 表示、対話的な行ジャンプ入力ウィジェット、PROGRAM名の表示なども
// 担っていたが、いずれもその時点でどのモード/PPがアクティブかに
// 固有のもの――ContentView自身のクラスコメントで既にそれらを将来の
// StatusBar/Mode領域として印を付けてある。このクラスはその最初の
// 一切れ: 独立した純粋なフォーマッタとし、ContentView(全モードの
// redraw()から既に到達される唯一の場所)が、フォーマット規則自体を
// 自分で持たずに呼び出せるようにする。
class ModeLine {
 public:
  // currentLine/totalLinesは1始まり(空リストなら0)。
  // パーセンテージはcurrentLineの位置(currentLine==カーソル/
  // ビューポート先頭が実際にある場所――これを供給する2箇所、
  // ContentView::modeLineText()/scrollModeLineText()参照)を基準に
  // しており、どれだけスクロールし「終えた」かではないため、
  // 常に先頭付近では0に近く、currentLineがtotalLinesに近づくにつれ
  // 100へ向かって上がっていく。currentLine==1なら"TOP"、
  // currentLine>=totalLinesなら数値パーセントの代わりに"END"を表示
  // する(2026-07-31、ユーザーの要望によりlegacy自身の「ビューポート
  // 下端基準の%、TOPラベル無し」という形から変更――legacyにはそもそも
  // ビューポート位置とカーソル位置を分離させるGUIドラッグスクロールが
  // 無かったので、これは意図的な逸脱であり単純な移植ではない)。
  // 書式: "%s/%s %2d%%" / "%s/%s TOP" / "%s/%s END"、currentは
  // totalの桁数に合わせてゼロ埋め(例: "03/15 20%")。
  static std::string format(int currentLine, int totalLines);

  // print_line()(disp.c)自身のlen<=0分岐からの移植――legacyでは
  // SHELL専用(disp_shell()が負のlenを渡す唯一の呼び出し元):
  // format()の既定である「currentをTOTAL自身の桁数に合わせてパディング」
  // とは異なり、totalLinesの桁数に関係無く固定幅のゼロ埋め
  // ("00002/00002 END")にする。幅は5(legacy自身のCソースは実際には
  // 6桁にゼロ埋めしている――この移植が代わりに5にしている理由は
  // formatFixedWidth()自身の.cpp側コメント参照、ユーザーからの直接
  // 依頼による)。TOP/END/%の接尾辞ロジックはformat()と同じ。あるビュー
  // がこちらを使うよう選択する方法はContentView::setUseFixedWidthPosition()
  // 自身のコメント参照。
  static std::string formatFixedWidth(int currentLine, int totalLines);
};

}  // namespace fm
