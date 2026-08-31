#pragma once

#include <string>
#include <vector>

#include "content_view.hpp"

namespace fm {

// COPY/PASTE(M_copy/^O^C + M_paste/^O^Y): 画面上にあったcontent
// areaの内容を、プレーンテキスト+属性のスナップショットとして凍結し、
// ユーザーがその上で実際の(行、列)キャレットを動かし同一行内の
// 部分文字列を選択できるようにし、その結果を、後の任意のPASTEが
// 再利用する1つの共有「ヤンクバッファ」に保持する。code.cの
// make_map()から精神を移植――legacy自身のローカル変数
// (row/col/ci/col0/c0/f_copy、COPYセッション中1回の関数呼び出し
// スタックだけに閉じている)は、代わりにこのクラスのメンバになり、
// Applicationはこの状態を一切直接持たない。これはまさに同じ
// セッションのプロンプト履歴再設計(CLAUDE.mdの2026-08-28/29の項目と
// .steering/20260828-prompt-history-redesign/参照)からの教訓其のもの
// である: ある機能自身のサブループが動いている間だけ意味を持つ状態は、
// Applicationへ場当たり的なメンバとしてばら撒くのではなく、その機能
// 自身のクラスの内側に属するべきである。
//
// スナップショットに格納する前に色/ハイライトのエスケープバイトを
// 全て取り除くmake_map()とは異なり、このクラスは各Lineの
// AttributeRunデータ(Bold/Underline)を届いた通りそのまま「保持」
// する――C++のAttributeRunは既に意味のある構造化データ(nroff
// オーバーストライク検出)であり、legacyのK_hのような描画専用の
// エスケープシーケンスではないため、画面表示のために取り除くべき
// ものは何も無い。とはいえ、「抽出された」クリップボードの部分
// 文字列(clipboardText())は常に属性データの付かない素の
// std::stringである: この移植全体で唯一のPASTE先
// (LineEditor::paste()、単一行の素のテキストフィールド)には、
// 装飾されたテキストを表現・表示する手段が全く無いため、クリップ
// ボードの境界を越えて属性を運んでも行き場が無い(requirements.mdの
// 「装飾文字の扱い」参照)。
//
// 意図的に除外(Applicationの仕事、この移植の他の全`*Mode`クラスが
// 引いているのと同じ境界): このモードへいつ入る/いつ抜けるかの決定
// (^O^C/^O^Yのキー処理、どのApplicationModeへ戻るかの記憶)、および
// Application::activeContentView().lines()からの最初のスナップショット
// の取得。
class CopyMode {
 public:
  explicit CopyMode(ContentView &view);

  // 新しいスナップショット(通常はApplication::
  // activeContentView().lines())の所有権を受け取る: 入力の各Line
  // からは`text`と`attributes`だけが生き残る――それ以外の全Line
  // フィールド(highlightStartColumn、shellCommand、ellipsisEligible、
  // neverHighlighted)は「元の」モードに固有の表示メタデータであり、
  // ここで凍結された時点で意味を持たない(make_map()自身のPP→素の
  // テキストへの縮約と同じ)。`initialRow`(既定0)は代わりにカーソルを
  // そこから開始させる――実際のユーザー報告(「^Oc→ブロックカーソル
  // 位置が...正しくは直前のハイライト行位置」)により、legacy自身の
  // make_map()も同じことをしている(`row = rvp->row`、スナップショット
  // を取った直後: カーソルは、元のビューで既に選択/ハイライトされて
  // いた行から始まり、必ずしも先頭からではない)と確認できたため、
  // Application::startCopy()がここへ元のContentView自身の
  // cursorIndex()を渡す。有効な行の範囲にクランプする。進行中の
  // マークがあれば解除する。clipboardText()は「クリアしない」
  // (legacyのrvp->copyもCOPYセッションをまたいで同じように持続する)。
  // `view`を即座に(再)描画する。
  void loadSnapshot(std::vector<ContentView::Line> lines, int initialRow = 0);

  // -- カーソル移動、境界チェック付き(スナップショットのどちらかの
  // 端を超える移動は何もしない) -- どれも`view`を再描画する。
  //
  // moveUp()/moveDown(): markActive()がtrueの間は何もしない――code.c
  // 自身のM_up/M_dn(`if (!f_copy && row) vt_move(...)`)からの移植:
  // legacyのrow変数はマーク中「でない」時にしか更新されないため、
  // マークを行をまたいで拡張することはそもそもできない(このクラスは
  // 「マークを解除してから移動する」のではなく、その条件と全く同じく
  // 移動そのものを完全にブロックする)。それが許される時は、legacy
  // 自身の列を保持するcount_i()/count_h()往復と同様に、行の変更を
  // またいで現在の表示カラムを保持する。
  void moveUp();
  void moveDown();
  // moveLeft()/moveRight()/moveToLineStart()/moveToLineEnd():
  // markActive()の値に関わらず常に許可される(legacyのM_kprev/
  // M_knext/^A/^Eのケースはf_copyを一切見ない)――マークが一度
  // 始まった後、実際に拡張/縮小する手段はこれである。
  void moveLeft();
  void moveRight();
  void moveToLineStart();
  void moveToLineEnd();

  // 2026-08-31、直接のユーザー依頼("本体で変更が必要なのは^Space...
  // emacsに合わせたい。^@でマーク開始トグル、^[wで確定"): 以前は
  // toggleMark()という1つのメソッドが、1回目の呼び出しでは選択の
  // アンカーを置き、2回目の呼び出しではそれを終える(結果を保存する)
  // という2つの役割を兼ねていた。^Spaceが日本語変換エンジン(ATOK等)の
  // 入力モード切り替えキーと衝突するため、emacsのset-mark-command(^@)
  // /kill-ring-save(^[w)に合わせてこの2つの役割を別々のキー/
  // メソッドへ分離した――application.cpp自身のfeedCopyByte()参照。
  // ただし'.'キーは(ATOKと無関係、衝突の実害が無いため)後方互換の
  // トグル別名として、この2メソッドを内部で使い分ける形で残っている。

  // 現在のカーソル位置に選択のアンカーを置く。既にマーク中の場合は
  // アンカーを現在位置へ置き直す(emacsのC-@を繰り返し押した時と同じ、
  // 何度でも安全に呼べる)。
  void startMark();
  // アンカーと現在のカーソル位置の間の範囲を確定する。空でない範囲
  // (hi > lo)ならclipboardText()を上書きする。空の範囲(マーク開始の
  // 直後、同じ列で確定)なら既存のclipboardText()に触れない。legacy
  // 自身の`if (*p) strcpy(rvp->copy, p)`というガードと同じ。マーク中で
  // なければ何もしない。
  void confirmMark();
  bool markActive() const;

  // 共有「ヤンクバッファ」(legacy: RV::copy) -- 常に素のテキスト、
  // 理由はこのクラス自身のコメント参照。最初の空でない選択が完了
  // するまでは空。
  const std::string &clipboardText() const;

 private:
  void cancelMark();
  void render();  // view_のLine群を再構築する(あれば、ライブ選択
                   // 自身のStandout AttributeRunを織り込む)、および
                   // view_の実キャレットを再配置する。

  ContentView &view_;
  std::vector<ContentView::Line> lines_;  // textとattributesのみ
  int cursorRow_ = 0;
  int cursorByte_ = 0;  // lines_[cursorRow_].textへのバイトオフセット、
                        // 常にUTF-8シーケンス境界に位置する
  bool markActive_ = false;
  int markAnchorByte_ = 0;  // markActive_の間のみ有効
  std::string clipboardText_;
};

}  // namespace fm
