#pragma once

#include <string>
#include <vector>

#include "content_view.hpp"

namespace fm {

// COPY/PASTE(M_copy/^O^C + M_paste/^O^Y): 画面上にあったcontent
// areaの内容を、プレーンテキスト+属性のスナップショットとして凍結し、
// ユーザーがその上で実際の(行、列)キャレットを動かし選択できる
// ようにし、その結果を、後の任意のPASTEが再利用する1つの共有
// 「ヤンクバッファ」に保持する。code.cのmake_map()から精神を移植
// ――legacy自身のローカル変数(row/col/ci/col0/c0/f_copy、COPY
// セッション中1回の関数呼び出しスタックだけに閉じている)は、代わり
// にこのクラスのメンバになり、Applicationはこの状態を一切直接持た
// ない。これはまさに同じセッションのプロンプト履歴再設計(CLAUDE.md
// の2026-08-28/29の項目と.steering/20260828-prompt-history-redesign/
// 参照)からの教訓其のものである: ある機能自身のサブループが動いて
// いる間だけ意味を持つ状態は、Applicationへ場当たり的なメンバとして
// ばら撒くのではなく、その機能自身のクラスの内側に属するべきである。
//
// 2026-09-07、直接のユーザー要望("次はCOPYだな。旧の行指定は忘れて
// いい")を受けて、legacy自身のmoveUp()/moveDown()の「マーク中は行を
// またげない」という単一行限定のモデル(code.cの`if (!f_copy && row)
// vt_move(...)`)を撤廃し、複数行にまたがる範囲選択(GUIのマウス
// ドラッグ、placeCursorAt()参照)を許すようにした。ただし
// 「継承したい仕様」として、ハイライト表示のある画面(Directory一覧
// 等、各行にContentView::Line::highlightStartColumnが設定されている)
// からの選択は、confirmMark()が各行の「選択可能範囲」(highlightStart
// Column以降、つまりファイル名そのもの)だけを取り出す――ちょうど
// 通常のクリックがhighlightStartColumn以降だけをスコープにするのと
// 同じ考え方(Application::handleMouseClick()自身のisColumnWithinRowText()
// 参照)。confirmMark()自身のコメント参照。
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
  // からは`text`と`attributes`に加えて`highlightStartColumn`も生き
  // 残る(2026-09-07追加、直接のユーザー要望「②」――ハイライト表示の
  // ある画面からの選択を「選択可能範囲のみ」に絞るためにconfirmMark()
  // が読む、クラス自身のコメント参照)。それ以外の全Lineフィールド
  // (shellCommand、ellipsisEligible、neverHighlighted)は引き続き
  // 「元の」モードに固有の表示メタデータであり、ここで凍結された
  // 時点で意味を持たない(make_map()自身のPP→素のテキストへの縮約と
  // 同じ)。`initialRow`(既定0)は代わりにカーソルを
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
  // moveUp()/moveDown(): 2026-09-07、直接のユーザー要望("旧の行指定は
  // 忘れていい")を受けて、markActive()中でも常に許可するよう変更した
  // ――以前はcode.c自身のM_up/M_dn(`if (!f_copy && row)
  // vt_move(...)`)を忠実に移植し、legacyのrow変数がマーク中「でない」
  // 時にしか更新されないことに倣って、マークを行をまたいで拡張する
  // こと自体をブロックしていた(単一行限定の選択モデル)。この移植は
  // 意図的にその制約を外し、複数行にまたがる範囲選択(GUIのマウス
  // ドラッグ)を可能にする。legacy自身の列を保持するcount_i()/
  // count_h()往復と同様、行の変更をまたいで現在の表示カラムを保持
  // する点は変わらない。
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

  // GUIのマウス範囲選択向け(2026-09-07追加、直接のユーザー要望:
  // "スクロールに関する画面ドラッグと同じマウス操作に関して、発動
  // し得る機能としては範囲指定がある。COPYモードの際にそれは可能
  // だろうか？")。`row`は絶対行インデックス(呼び出し元がContentView::
  // lineIndexAtRow()で画面上のマウスの行から変換済みのもの)、
  // `column`はdisplayColumnFor()と同じ表示カラム単位。単純に
  // (row, column)へキャレットを移動するだけ――markActive()の値に
  // 関わらず常に`row`も反映する(moveUp()/moveDown()自身のコメント
  // 参照、2026-09-07に単一行限定の制約を撤廃したのに合わせて、ここも
  // 「マーク中はrowを無視する」という以前の制約を撤廃した)。ボタン
  // 押下の瞬間に呼ばれれば実質的な「クリックでキャレットを置く」
  // 動作を兼ね、ドラッグ中に繰り返し呼ばれれば選択を複数行にまたいで
  // 伸縮させる。
  void placeCursorAt(int row, int column);

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
  // アンカーと現在のカーソル位置の間の範囲を確定する。マーク中で
  // なければ何もしない。
  //
  // 2026-09-07、直接のユーザー要望("次はCOPYだな。旧の行指定は
  // 忘れていい。ただし継承したい仕様もある...②ハイライト表示のある
  // 画面では、選択可能範囲のみ指定")を受けて複数行対応に書き直した:
  // アンカー行〜カーソル行の各行について、
  //   - その行にhighlightStartColumnが設定されている(=ハイライト
  //     表示のある一覧――Directory等――からの凍結スナップショット
  //     である)場合: 選択の実際のバイト範囲がどこであれ、その行の
  //     highlightStartColumn以降(=ファイル名そのもの、「選択可能
  //     範囲」)を丸ごと取り出す――通常のクリックがそこだけをスコープ
  //     にするのと同じ考え方(handleMouseClick()自身のisColumnWithinRowText()
  //     参照)。行をまたぐ複数の結果は、Application::shellQuoteArgv()と
  //     全く同じ規則でシェルクォートした上でスペース区切りに連結する
  //     (`open -a プレビュー`のような複数引数のコマンド行にそのまま
  //     ペーストできるようにする、直接の要望「①と併せ、open -A
  //     プレビュー等で複数渡せる仕様」)。
  //   - highlightStartColumnが無い(プレーンなFILE内容等)行: 従来通り
  //     アンカー/カーソルbyteそのものが定める正確なバイト範囲を取り出
  //     し、複数行の結果は改行で連結する(legacy自身の`if (*p)
  //     strcpy(rvp->copy, p)`というガードに倣い、単一行かつ空の範囲
  //     ならclipboardText()を上書きしない――ハイライト行を含む場合は
  //     ファイル名自体が空になり得ないため、このガードの対象外)。
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
  int markAnchorRow_ = 0;   // markActive_の間のみ有効(2026-09-07追加、複数行対応)
  int markAnchorByte_ = 0;  // markActive_の間のみ有効
  std::string clipboardText_;
};

}  // namespace fm
