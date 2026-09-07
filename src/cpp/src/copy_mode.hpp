#pragma once

#include <optional>
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
// ドラッグ、placeCursorAt()参照)を許すようにした。
//
// (同日、直接のユーザー訂正: ハイライト表示のある画面向けに一度実装
// した「選択可能範囲(ファイル名)だけを取り出す」仕組みは、既存の
// dired方式マーキング('.'/'['/']'、DirectoryMode::toggleMark())が
// Application::startProgramPrompt()への複数ファイル渡しを既に
// カバーしていると分かったため撤回・削除した――同じ用途に2つの
// 仕組みを持つ必要は無い。)
//
// 同日さらに、直接のユーザー要望("①emacsには矩形範囲指定の機能が
// あるのであれを移植してほしい"): emacsのrectangle-mark-mode(^X
// SPC)を移植した――startOrUpgradeRectangleMark()参照。通常のマーク
// (startMark())が「アンカー〜カーソルの間の、読み順での連続した
// テキスト」を選ぶのに対し、矩形マークは「アンカー行〜カーソル行の
// 各行について、アンカー〜カーソルの表示カラム範囲だけ」を選ぶ
// ――emacs自身のkill-rectangle/copy-rectangle-as-killと同じ意味論。
//
// 2026-09-07さらに追記、直接のユーザー報告⑭("マウスドラッグで範囲
// 指定はできるけど、あらたな範囲指定開始点を定められない...^@押す
// たびに離れ小島なハイライトを画面に残留させて^[wでそれらをまとめて
// クリップボードへ、ってのは？"): 複数の「離れ小島」(islands_)の
// 選択を蓄積できるようにした。既に実質的な範囲を持つマーク(アンカー
// ≠カーソル)の最中にstartMark()/startOrUpgradeRectangleMark()/
// beginOrExtendFromMouse()のいずれかが呼ばれると、その時点の選択を
// 1つの島としてislands_へ確定・画面上に残したまま、新しいアンカーを
// そこから始める。まだ実質的な範囲を持たない(アンカー==カーソル、
// 例えば^X SPC直後でまだ一度も動かしていない)場合は何もバンクせず、
// 続くカーソル移動/マウスがそのマークの自由端を定義する――emacsの
// rectangle-mark-modeを終えた直後にマウスで矩形の対角を指定する、
// という自然な流れを壊さないため。confirmMark()はislands_全てと
// 現在進行中のマークをまとめてクリップボードへ書き出す。
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
  //
  // `initialTopIndex`(2026-09-07追加、直接のユーザー報告⑤:
  // "CLAUDE.md→^V（ページ送り）→^O^C→コピーモード発動前と後とで
  // ブロックカーソル位置が違うため画面が変化してしまいコピー開始
  // 位置を見失う"): 省略時(nullopt)はview_のcursorIndex_/lines_から
  // clampViewport()が独自に導出する(以前からの、境界に強制的に寄せる
  // 挙動)。Application::startCopy()は代わりに起点のtopIndex()を渡す
  // ――起点で既に成立していたスクロール位置をそのまま引き継ぐため。
  // この値は今回の(最初の)render()呼び出し1回だけに適用され、以後の
  // moveUp()/moveDown()等の呼び出しでは通常通りview_自身の現在の
  // topIndex()が基準になる(render()自身のコメント参照)。
  void loadSnapshot(std::vector<ContentView::Line> lines, int initialRow = 0,
                     std::optional<int> initialTopIndex = std::nullopt);

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
  // ドラッグ、および矩形マーク)を可能にする。legacy自身の列を保持する
  // count_i()/count_h()往復と同様、行の変更をまたいで現在の表示
  // カラムを保持する点は変わらない。
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
  // 伸縮させる(通常のマーク/矩形マークどちらの最中でも同じ)。
  void placeCursorAt(int row, int column);

  // GUIのマウスボタン押下(copyMouseDown())専用(2026-09-07追加、直接の
  // ユーザー報告⑭自身のコメント参照、クラス自身のコメント参照)。
  // placeCursorAt()と違い、押下の「瞬間」だけに使う――既に実質的な
  // 範囲を持つマークの最中であれば、それを1つの島として確定・画面に
  // 残したまま、この新しい押下位置から改めてマーク(常に通常マーク、
  // 矩形ではない――矩形は^X SPCで明示的に選ぶ)を開始する。まだ実質的
  // な範囲を持たない場合(例えば^X SPC直後の最初のクリック)は、
  // アンカーはそのまま、カーソルだけをこの位置へ動かす――既存のマーク
  // の自由端を定義する。
  void beginOrExtendFromMouse(int row, int column);

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

  // 現在のカーソル位置に選択のアンカーを置く(通常の、矩形でない
  // マーク)。既にマーク中で、かつそのマークがまだ実質的な範囲を
  // 持たない(アンカー==カーソル)場合は、単にアンカーを現在位置へ
  // 置き直す(emacsのC-@を繰り返し押した時と同じ、何度でも安全に
  // 呼べる)。既に実質的な範囲を持つ場合は、bankCurrentMarkIfExtended()
  // 自身のコメント参照(2026-09-07追加、直接のユーザー報告⑭)――
  // それを1つの島として確定・画面に残したまま、新しいアンカーを
  // ここから始める。矩形マーク中だった場合は矩形指定を解除し、
  // 通常マークに戻す。
  void startMark();

  // emacsのrectangle-mark-mode(^X SPC)の移植(2026-09-07追加、直接の
  // ユーザー要望"①emacsには矩形範囲指定の機能があるのであれを移植
  // してほしい")。既にマーク中で、かつそのマークがまだ実質的な範囲を
  // 持たない場合はアンカーはそのまま、選択の解釈だけを矩形に切り替
  // える――キーボードでstartMark()した直後や、GUIのマウスボタン
  // 押下(beginOrExtendFromMouse())直後に呼んでも自然に働く。既に
  // 実質的な範囲を持つ場合は、bankCurrentMarkIfExtended()自身の
  // コメント参照(直接のユーザー報告⑭)――それを1つの島として確定・
  // 画面に残したまま、現在位置を新しいアンカーにして矩形マークを
  // 開始する。マーク自体が全く無ければ、現在のカーソル位置をアンカー
  // にして新規に矩形マークを開始する。
  void startOrUpgradeRectangleMark();

  // islands_全て(直接のユーザー報告⑭、クラス自身のコメント参照)と、
  // 現在進行中のマーク(アンカーと現在のカーソル位置の間の範囲)を
  // まとめて確定する。マーク中でなければ何もしない。islands_は
  // 消費後にクリアされる。
  //
  // 2026-09-07、直接のユーザー要望("次はCOPYだな。旧の行指定は
  // 忘れていい")を受けて複数行対応に書き直した: アンカー行〜カーソル
  // 行の全ての行(読み順に正規化)を対象にする。
  //   - 通常マーク(rectangleMarkActive()がfalse): 従来通りアンカー/
  //     カーソルbyteそのものが定める、行をまたいでも連続した1つの
  //     テキスト範囲を取り出し、改行で連結する(legacy自身の`if (*p)
  //     strcpy(rvp->copy, p)`というガードに倣い、空の範囲(単一行かつ
  //     hi==lo)ならclipboardText()を上書きしない)。
  //   - 矩形マーク(rectangleMarkActive()がtrue、2026-09-07追加):
  //     アンカー/カーソルそれぞれの表示カラム(displayColumnFor())の
  //     min/maxを列範囲とし、各行についてその列範囲だけ(その行自身の
  //     byteOffsetForDisplayColumn()で求め直す――行ごとに文字幅が
  //     異なり得るため、バイトオフセットをそのまま使い回さない)を
  //     取り出して改行で連結する――emacsのcopy-rectangle-as-kill()と
  //     同じ意味論。全行が空(列範囲が実質ゼロ幅)でない限り上書きする。
  void confirmMark();
  bool markActive() const;
  // startOrUpgradeRectangleMark()自身のコメント参照。markActive()が
  // falseの間は常にfalse。
  bool rectangleMarkActive() const;

  // 共有「ヤンクバッファ」(legacy: RV::copy) -- 常に素のテキスト、
  // 理由はこのクラス自身のコメント参照。最初の空でない選択が完了
  // するまでは空。
  const std::string &clipboardText() const;

 private:
  void cancelMark();
  void render();  // view_のLine群を再構築する(islands_全て+進行中の
                   // マークがあれば、それぞれのStandout AttributeRun
                   // を織り込む)、および view_の実キャレットを再配置
                   // する。加えて、rectangleMarkActive()の間はモード
                   // ラインの位置フィールドへ"Rectangle-Mark"を表示
                   // する(直接のユーザー要望"モードラインのメッセージ
                   // エリアに...Rectangle-Markと表示"――下線装飾は、
                   // モードライン描画パイプライン自体を拡張する必要
                   // がある大掛かりな変更になるため今回は見送り、
                   // 通常の位置フィールドと同じ描画のまま)。

  // islands_自身の宣言コメント参照(2026-09-07追加)。1つの確定済み、
  // または進行中の選択範囲――通常マーク(読み順の連続範囲)か矩形
  // マーク(行×列範囲)かで解釈が変わる。
  struct Island {
    bool rectangle = false;
    int loRow = 0, hiRow = 0;
    int loByte = 0, hiByte = 0;  // rectangle==falseの間だけ有効
    int loCol = 0, hiCol = 0;    // rectangle==trueの間だけ有効
  };
  // 現在の(markAnchorRow_/Byte_, cursorRow_/Byte_, rectangleMark_)から
  // Islandを組み立てる――confirmMark()/render()両方で使う共通ロジック。
  // markActive_がfalseの間は呼ばないこと(未定義の値になる)。
  Island currentMarkAsIsland() const;
  // 1つのIslandが指すテキストを行ごとの断片として取り出す
  // (confirmMark()専用)。
  std::vector<std::string> extractIslandParts(const Island &island) const;
  // `rendered`(view_.setLines()へ渡す直前の可変コピー)へ、1つの
  // IslandぶんのStandout AttributeRunを書き込む(render()専用)。
  static void highlightIsland(std::vector<ContentView::Line> &rendered, const Island &island);
  // 実質的な範囲を持つ(アンカー≠カーソル)マークが進行中であれば、
  // それをislands_へ確定して画面に残し、アンカーを現在のカーソル
  // 位置へ置き直す(=空の新しい島の起点にする)――startMark()/
  // startOrUpgradeRectangleMark()/beginOrExtendFromMouse()自身の
  // コメント参照。実質的な範囲がまだ無ければ何もしない(no-op)。
  void bankCurrentMarkIfExtended();

  ContentView &view_;
  std::vector<ContentView::Line> lines_;  // textとattributesのみ
  int cursorRow_ = 0;
  int cursorByte_ = 0;  // lines_[cursorRow_].textへのバイトオフセット、
                        // 常にUTF-8シーケンス境界に位置する
  bool markActive_ = false;
  bool rectangleMark_ = false;  // markActive_の間のみ有効(2026-09-07追加)
  int markAnchorRow_ = 0;       // markActive_の間のみ有効(2026-09-07追加、複数行対応)
  int markAnchorByte_ = 0;      // markActive_の間のみ有効
  // islands_自身の宣言コメント参照(2026-09-07追加)――確定済みの
  // (現在進行中ではない)選択範囲の集まり。confirmMark()で消費され
  // クリアされる。loadSnapshot()でも(新しいCOPYセッションの開始時に)
  // クリアされる。
  std::vector<Island> islands_;
  std::string clipboardText_;
  // loadSnapshot()自身の`initialTopIndex`引数のコメント参照
  // (2026-09-07追加)――loadSnapshot()直後の最初のrender()呼び出しに
  // だけ適用される、one-shotのオーバーライド。
  std::optional<int> pendingTopIndexOverride_;
};

}  // namespace fm
