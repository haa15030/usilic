#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "display.hpp"

namespace fm {

// SDL2/SDL2_ttf駆動のDisplay: TerminalDisplayが本物の端末へ書く
// のと全く同じ文字セルのグリッドを、代わりに本物のGUIウィンドウへ
// ピクセル単位で描画する。legacyのusilicには移植元が全く無い(そこには
// GUIモードが全く存在しない)――これは純粋にdocs/ideas/IDEAS.mdの
// 「猫の手」隠しGUIモード要望を実現するために存在し、ユーザーとの
// 間で、スコープを絞った一切れずつ構築することが確認済み:
// 「この」クラスは一切れ目(同じ内容を表示する本物のウィンドウ、
// それ以上ではない)――慣性の効く「猫の手」スクロール物理演算と
// マウス右クリックのPROGRAM一覧メニューは明示的にまだここには
// 無い(この一切れで^Og/handleMouseClick()が正確に何をカバーする
// かについてはApplication自身のクラスコメント参照)。
//
// これは移植元が全く存在しないので何かの逐語的な移植ではない――
// しかし、既に移植済みの仕組みをできる限り多く意図的に再利用して
// いる: fm::Utf8::sequence_length()/display_width()(まさにこの
// 「マルチバイト文字は複数バイトにまたがるが必ずしも複数列にまた
// がるとは限らない」という問題のために既に構築されている、自身の
// クラスコメント参照)は、write()/putChar()が他所で受け取るのと
// 同じUTF-8バイトストリームをデコードする。TerminalDisplay(生の
// バイトを、それ自身でUTF-8をデコードする本物の端末へ単に転送する
// だけ)とは異なり、このクラスはピクセル描画を直接所有し、どの
// グリフを描くべきか、それが何セル幅かを知るためにUTF-8を自分で
// デコードしなければならないため。
//
// KeyInput/Applicationについては意図的に無知のままにしてある
// (#includeも依存も無い): keyEvents()は、KeyInput::SpecialKeyでは
// なく、このクラス「自身」の小さなKeyEvent形(バイト、または
// Display::Keyの既存の6つの値のどれか――既にこのクラス自身の
// 継承されたインターフェースの一部であり、新しいenumは不要)を
// 返すので、それを実際のApplication::feedByte()呼び出しへ変換
// するのはapp/main.cppの仕事である(それは既にKeyInputとDisplayの
// 両方に依存している)――この移植全体が他の場所でどこでも描いて
// いるのと同じ境界(DisplayはKeyInputに決して依存せず、KeyInputは
// Applicationに決して依存しない)。
//
// 意図的に除外(将来の課題、この一切れではない):
//   - 慣性の効く「猫の手」ドラッグスクロールの物理演算とカーソル
//     形状の変更: このクラスは常に静的な文字グリッドを描画する
//     だけ: セル未満/行の一部だけのピクセルスクロールは無い、
//     マウスドラッグの追跡は無い、カスタムカーソルのビットマップ
//     は無い。この1つの上に構築される将来のクラス(まだ公開され
//     ていない生のピクセルアクセス)がそれを追加するだろう。
//   - マウス右クリックのPROGRAM一覧コンテキストメニュー:
//     右クリックは検出される(MouseClickのrightButton)が、
//     Applicationはそれに対してまだ何も配線されていない
//     (自身のクラスコメント参照)。
//   - ライブなOSレベルのウィンドウリサイズ(ウィンドウ自身の
//     リサイズハンドルをドラッグする): setSize()は存在し、
//     呼ばれればウィンドウ/グリッドを正しくリサイズするが、
//     SDL_WINDOWEVENTに応答してそれを呼ぶものはまだ何も無い
//     ――この一切れでは、ウィンドウのサイズは、それが構築された
//     時のrows/columnsに固定される(^Og切り替えの瞬間の端末
//     セッション自身のサイズを継承し、「シームレスな」継続性に
//     合わせている)。
//   - コピー/ペースト、IME入力、そして本物の端末エミュレータが
//     いずれ育てる他の何でも: 基本的な見た目と使い心地を実証する
//     ことに焦点を当てた最初の一切れの範囲外。
class GraphicalDisplay : public Display {
 public:
  // fontPathは本物の、読み取り可能なTTF/TTCフォントファイル(等幅
  // フォント――このクラスはそれを検証せず、プロポーショナル
  // フォントは単に列がずれて描画される)でなければならない。実際の
  // システムフォントパスを解決するのは呼び出し元(main.cpp)の仕事
  // であり、Configuration/History/HELPテキストに既に適用されている
  // この移植の恒久的な「隠れたグローバル状態は無し」というルールに
  // 合わせている――このクラスは自力でファイルシステムを検索しない。
  // fontPointSizeはデフォルトで16(このクラス自身の長年ハード
  // コードされてきた値、合わせるべき本物の端末フォントサイズが
  // 無い^Og起動の手動GUI入場に対するデフォルトとして維持される)。
  // 「実はすでにGUI」の起動時の入場(main.cppのqueryTerminalAppColors()
  // 参照)は代わりに呼び出し元の端末自身が問い合わせたサイズを渡す
  // ――サイズの不一致こそが、GUIウィンドウを、それが置き換えていた
  // 端末より目に見えて「大きく」してしまった原因である
  // (CLAUDE.mdの2026-07-30の記録参照)。行/列数だけではウィンドウの
  // ピクセルサイズは決まらず、セルサイズも決めるため。SDL2/
  // SDL2_ttfの初期化、ウィンドウ/レンダラーの作成、またはフォントの
  // 読み込みが失敗した場合(例外を投げたり終了したりするのではなく)
  // nullptrを返すので、呼び出し元はクラッシュする代わりに端末
  // モードに留まることへフォールバックできる。
  static std::unique_ptr<GraphicalDisplay> create(int rows, int columns, const std::string &fontPath,
                                                    int fontPointSize = 16);

  ~GraphicalDisplay() override;

  struct Color {
    unsigned char r = 0, g = 0, b = 0;
  };
  // デフォルトの前景色/背景色(黒地に220,220,220)を上書きする――
  // 「呼び出し元の端末のように既に見える状態で起動する」機能
  // (docs/ideas/IDEAS.mdの「実はすでにGUI」要望、CLAUDE.mdの
  // 2026-07-30の記録参照)のために追加された: 本物の端末自身の色を
  // 問い合わせできる場合(現在はAppleScript経由のmacOSの
  // Terminal.appだけ――main.cpp参照)、呼び出し元は、
  // GraphicalDisplay自体を端末を認識するようにしようとするのでは
  // なく、このクラス自身のハードコードされたデフォルトを使う
  // 代わりに、最初の描画の前にここでそれらを適用する。次の描画
  // (flush()/renderScrolled())で反映される。それ自体では即座には
  // 再描画しない。
  void setColors(Color foreground, Color background);

  // 読み戻しアクセサ(2026-08-24、docs/CHANGES.md項目427)――呼び
  // 出し元が、(setColors()/「実はすでにGUI」の端末色一致、または
  // 生きたCmd+K/Cmd+=/Cmd+-の調整、項目426経由で)ユーザーが実際に
  // 落ち着いたものが何であれ、このクラスがHistoryやusilicrc自体に
  // ついて何も知る必要無く、セッション終了時にHistory::
  // GuiPreferencesへ永続化できるように追加された。
  Color foregroundColor() const;
  Color backgroundColor() const;
  int fontPointSize() const;

  // ウィンドウを絶対的な「画面」位置(左上原点のピクセル座標、
  // SDL2自身のクロスプラットフォームの慣習と、AppleScriptの
  // 「bounds of front window」が本物の端末アプリに対して報告する
  // ものの両方に合わせている――main.cppのqueryTerminalAppWindowBounds()
  // 参照)へ移動する。「実はすでにGUI」の起動機能(docs/ideas/IDEAS.md)
  // のために追加された: GUIウィンドウを、呼び出し元の端末自身の
  // ウィンドウが既に座っているまさにその場所に重ねることで、
  // バックエンドの切り替えがウィンドウをSDL_WINDOWPOS_CENTEREDが
  // たまたま置く場所へ飛ばすのではなく、ユーザーには見えないように
  // する(実際のユーザー依頼、2026-08-03、マージン修正への追加依頼
  // として"テキスト表示ウィンドウに重ねる形でGUIウィンドウ版を
  // 表示したい")。ウィンドウが作成されていなければ何もしない。
  void setWindowPosition(int x, int y);

  // `width, height`は、setWindowPosition()が取るのと「同じ」
  // 「外枠、タイトルバー込み」の慣習である――SDL自身のネイティブな
  // コンテンツ領域の慣習では「ない」。内部で同じ方法で変換される。
  // 理由(「実はすでにGUI」オーバーレイ起動のために、本物の端末
  // ウィンドウ自身の実測されたピクセルサイズに正確に合わせる)に
  // ついては.cpp自身のコメント参照。このクラス自身の
  // cellWidth*columns+マージン+スクロールバー幅の式を上書きする。
  // ウィンドウが作成されていなければ何もしない。
  void setWindowSize(int width, int height);

  // setWindowSize()と同様だが、「さらに」cellWidth/cellHeightを
  // (create()自身のフォントメトリクスに基づく計算を上書きして)
  // 再計算し、既知のrows/columns数が与えられた外側の寸法にちょうど
  // 収まるようにする――理由(本物の端末の行間隔は、異なるフォント/
  // サイズにわたってフォントメトリクスだけからは信頼できる形で
  // 導出できないが、「実はすでにGUI」オーバーレイ起動のように、
  // 本物の端末自身の外側のピクセルサイズが既に分かっている場合、
  // 算術的に正確に復元「できる」)については.cpp自身のコメント
  // 参照。ウィンドウが作成されていなければ何もしない。
  void matchWindowSize(int outerWidth, int outerHeight);

  // このウィンドウを破棄せずに隠す/表示する(SDL_HideWindow()/
  // SDL_ShowWindow())――起動元端末内でのPROGRAMモード機能
  // (main.cppのrunProgramInLaunchingTerminal())が、委譲された
  // PROGRAMコマンド(`less`、`vi`、...)の実行中このウィンドウを
  // 邪魔にならないようにし、その本物の端末コマンドが終了したら
  // 戻すために使う。ウィンドウが作成されていなければ何もしない。
  void setVisible(bool visible);

  // このウィンドウを前面に持ってきて入力フォーカスを与える
  // (SDL_RaiseWindow())――委譲されたPROGRAMコマンドが終了した後、
  // 制御が、本物の端末が前面化された後OSがたまたまフォーカスを
  // 置く場所ではなく、目に見える形でこのウィンドウへ戻るように、
  // 同じ機能によって使われる。ウィンドウが作成されていなければ
  // 何もしない。
  void raise();

  void write(const std::string &text) override;
  void putChar(int c) override;
  void flush() override;

  void moveCursor(int row, int col) override;
  void cursorLeft() override;
  void saveCursor() override;
  void restoreCursor() override;
  void setVisibleCursor(int row, int col) override;
  void hideVisibleCursor() override;

  void clearScreen() override;
  void clearToEndOfLine() override;
  void scrollForward() override;
  void scrollReverse() override;

  void setAttribute(Attribute attr) override;
  std::string attributeEscape(Attribute attr) const override;

  int rows() const override;
  int columns() const override;
  void setSize(int rows, int columns) override;

  // Display::isGraphical()自身のコメント参照。
  bool isGraphical() const override { return true; }

  // 常に""を返す――GraphicalDisplayはKeyInput::readEscapeSequence()
  // の対象には決してならない(本物の端末capabilityシーケンスの
  // マッチングは、本物のttyバイトストリームにだけ当てはまる)。
  // GUIモードのキーボード入力は、このメソッドを全く経由せず、
  // 代わりにkeyEvents()を経由する(クラスコメント参照)。
  std::string keySequence(Key key) const override;

  bool outputEnabled() const override;
  void setOutputEnabled(bool enabled) override;

  // TerminalDisplay自身のものと同じように保存/返却される
  // (デフォルトfalse)。有効な場合、scrollForward()/
  // scrollReverse()は、完全なredraw()を要求するのではなく、
  // 文字グリッドの本物の1行シフトを行う――「この」一切れ自身の
  // 静的な内容の用途には必要ないにも関わらず、グリッドが既に行
  // アドレス可能である以上、追加コストが全くかからないので、
  // きちんと実装されている。
  bool smoothScrollEnabled() const override;
  void setSmoothScrollEnabled(bool enabled) override;

  void open() override;
  void close() override;

  // 何もしない: -gにはスクロール領域という概念が全く無い
  // (Display::leaveForExternalProgram()自身のコメント参照)――
  // それ自身のPROGRAM起動経路(runProgramInLaunchingTerminal())は、
  // このクラスが決して触れない、全く別の本物の端末ウィンドウを
  // 使う。
  void leaveForExternalProgram() override {}
  void reenterFromExternalProgram() override {}

  // -- GUI固有、Displayの抽象インターフェースの一部ではない --

  // このウィンドウの保留中のSDLイベントをポンプし、届いたマウス
  // クリックとキー入力を何であれキューに入れる(consumeMouseClick()/
  // consumeKeyEvent()経由で排出される)。ウィンドウ自身のクローズ
  // ボタンがクリックされたら(SDL_QUIT)falseを返す――端末モードへ
  // フォールバックせよという実行ループへの合図であり、
  // Application::consumeGuiModeToggleRequest()が既に使っているのと
  // 同じ「実行ループがポーリングするフラグ」の形。
  bool pumpEvents();

  // pumpEvents()が前回の呼び出し以降にSDL_WINDOWEVENT_EXPOSEDを
  // 見たかどうかを返してクリアする(2026-08-24、docs/CHANGES.md
  // 項目422、新しい`make app`の.appバンドルに対する実際のユーザー
  // 報告: "アプリ起動時のイントロ動作だけで以後は何も起きない"
  // ――永遠に黒いままの真っ黒なウィンドウ、直接再現済み:
  // `sample`は、実行ループがpumpEvents()/usleep()で通常通り
  // アイドル状態であり、ハングしていないことを示し、同じバイナリを
  // (バンドル経由ではなく)裸の`./usilic -g`として再実行すると毎回
  // 正しく描画された一方、`open`/ダブルクリック経由でバンドルを
  // 起動すると断続的だった)。根本原因: runGuiPhase()自身の
  // `anyEventThisFrame`最適化(アイドル中にredraw()が60回/秒発火
  // するのを止めるために追加された、自身のコメント参照)は、何も
  // 「消費」されなかったフレームでは常にredraw()を丸ごとスキップ
  // する――しかし、まさに最初のフレーム自身の初期redraw()は、
  // ウィンドウが実際にWindowServerによってマップ/合成されることと
  // 競合し得る。この移植自身の最初の見た目は、裸の実行ファイルの
  // 起動(適切な.appバンドルでは無い)は、本物のバンドルの
  // (RunningBoard登録、ViewBridge等――全て、詰まったインスタンスの
  // 実際の`log show`キャプチャで見える)よりもはるかに単純で速い
  // LaunchServicesのブートストラップを持つため、この競合を扱う
  // 必要が一度も無かった。その競合に負けると、以前は一度も必要
  // なかったので、後続のredraw()を合成するものが「何も」無い
  // ――SDL_WINDOWEVENT_EXPOSEDは、まさにこの状況のための
  // AppKit/WindowServer自身の「このウィンドウは(再)描画される
  // 必要がある」という合図であり、この修正の前は、pumpEvents()
  // 内の何もSDL_WINDOWEVENTを全く見ていなかった。
  bool consumeNeedsRedraw();

  // pumpEvents()が、ユーザー(またはOS)が実際にウィンドウを
  // リサイズしたことで前回の呼び出し以降にrows()/columns()を
  // 再計算したかどうかを返してクリアする(2026-08-24、
  // docs/CHANGES.md項目425、直接のユーザー依頼――ウィンドウは
  // 今はSDL_WINDOW_RESIZABLEで作成される、create()自身のコメント
  // 参照)。呼び出し元はこれを本物の端末のSIGWINCHと全く同じように
  // 扱うべきである: Application::handleResize()を呼んで、新しい
  // 幾何形状に対して内容を再折り返しする――runGuiPhase()
  // (app/main.cpp)はまさにそれを行う。runTerminalPhase()自身の
  // keyInput.consumeResizeSignal()チェックが既に持っているのと
  // 同じ形。
  bool consumeSizeChanged();

  struct MouseClick {
    int row;
    int column;
    bool rightButton = false;  // 今は捕捉されているが、将来の一切れがコンテキストメニューを配線するまで未使用
  };
  // 次にキューに入っているマウスクリック(ピクセル位置は既に、
  // moveCursor()が使うのと同じ表示の行/列座標空間へ変換済み)を
  // 返してクリアする。何も保留していなければnullopt。
  std::optional<MouseClick> consumeMouseClick();

  struct HoverEvent {
    int row;
    int column;
  };
  // 次にキューに入っている、単なるマウス移動のサンプル(ボタンが
  // 押されていない――pumpEvents()自身のSDL_MOUSEMOTIONケース参照)
  // を返してクリアする。何も保留していなければnullopt。実際の
  // ユーザー報告(2026-08-02、main.cpp内の本物の端末のマウス追跡
  // 経路自身のapplyHoverRowAndRedraw()/motionOnly処理と並べて比較
  // して)の後に追加された: 単なるホバー追従(クリック不要で、
  // ポインタが留まっている行を何であれハイライトする)は、本物の
  // 端末自身のSGRマウス報告では動作していたが、GUIモードでは全く
  // 配線されていなかった――SDL_MOUSEMOTIONイベントは、以前は左
  // ボタンが押されている「間だけ」(DragEvent::Motionとして)キュー
  // に入れられていたので、単なるホバーは完全に黙って捨てられて
  // いた。
  std::optional<HoverEvent> consumeHoverEvent();

  struct KeyEvent {
    int byte = 0;                    // specialがnulloptの時に有効
    std::optional<Key> special;      // これが矢印/ページキーだった場合、Display::Key自身の6つの値のどれか
    // 修飾キーの追跡は無い: 元々はキーボード駆動の慣性スクロールの
    // トリガーのために`shift`フィールド(Up/DownとShiftが一緒に
    // 押されているか)を持っていたが、そのトリガーが代わりにプレーンな
    // 'w'/'s'バイトへ移った時(2026-07-31)に取り除かれた(ユーザー
    // 自身の好み――修飾キーを押さえておくより、単一のキー入力の方が
    // 繰り返し届きやすい――CLAUDE.mdの2026-07-31の記録参照)。それは
    // 修飾キーの状態を全く必要としない。
  };
  // 次にキューに入っているキー入力を返してクリアする。何も保留して
  // いなければnullopt。通常の文字(入力方式経由で入力されたマルチ
  // バイトUTF-8テキストを含む)は、`special`が未設定の、バイト
  // ごとに1つのKeyEventとして届く。KeyInput::readByte()と
  // Application::feedByte()が、ソースに関わらず一度に1バイトずつ
  // 入力を消費することを既に期待しているのに合わせている。
  std::optional<KeyEvent> consumeKeyEvent();

  // -- ドラッグスクロールのプロトタイプサポート(2026-07-30、
  // 実現可能性サンプル) --
  // GUIモードの残りの部分へさらに踏み込む前に、「ドット単位の
  // 慣性ドラッグスクロールはそもそも構築できるのか」に答える
  // ために特別に追加された――これらを「使う」実際のドラッグ/
  // 慣性/行確定ロジックについてはapp/main.cppのrunDragScrollDemo()
  // 参照。write()/putChar()/moveCursor()(ContentViewが駆動する
  // 通常のDisplayインターフェース)とは意図的に別の、自己完結した
  // 描画経路: 実際の本番統合(適切な「アクティブなContentViewを
  // なめらかにスクロールする」API)は、このサンプルが評価された
  // 「後」の決定事項であり、ここでは想定していない。
  struct MouseMotion {
    int deltaY = 0;  // 前回キューに入れられたイベント以降に動いたピクセル数(SDL自身の相対移動)
  };
  // 次にキューに入っている左ボタンの押下/解放の遷移、または移動
  // サンプルを返してクリアする。移動はボタンが押されている間だけ
  // キューに入れられる(SDL_CaptureMouse駆動、なのでドラッグの
  // 途中でカーソルがウィンドウを離れても移動は追跡される)。
  enum class DragEventType { ButtonDown, ButtonUp, Motion };
  struct DragEvent {
    DragEventType type;
    int deltaY = 0;  // Motionに対して有効
    // row/column(2026-09-07追加、直接のユーザー要望: COPYモードでの
    // マウス範囲選択): MouseClick/HoverEventと同じ表示座標系
    // (moveCursor()の座標系)での、押下/移動時点の絶対位置。
    // ButtonDown/Motionに対してのみ有効(ButtonUpは常に0/0――解放時点
    // の位置は呼び出し元にとって意味を持たない、直前のMotion/ButtonDown
    // が既に最終位置を運んでいる)。スクロールのdeltaY駆動の物理
    // (main.cppのコースト/ドラッグ)は元々この2フィールドを一切
    // 見ないので、それらの既存の呼び出し元には影響しない。
    int row = 0;
    int column = 0;
  };
  std::optional<DragEvent> consumeDragEvent();

  // SDL_MOUSEWHEELからキューに入れられる、マウスのスクロール
  // ホイールのティック――これが何に使われるかについてはapp/main.cpp
  // 自身のホイール消費コメント参照(w/sの惰性レベルキー自身の
  // 「逆方向は即座にレベル1へリセットする」というジャンプに対する、
  // ティックごとに徐々に減速する代替。ユーザー自身の2026-08-05の
  // 依頼で追加された: "s→wだと高速度からsだと減速じゃなくて逆方向
  // へ最低速"――w/sで惰性の途中に方向を反転させると、減速ではなく
  // 新しい方向への急激な最低速へのジャンプになる)。`deltaY`は
  // SDL自身のwheel.yで、SDL_MOUSEWHEEL_FLIPPEDに対して符号補正
  // されており、その符号は、OSレベルの「ナチュラルスクロール」
  // 設定に関わらず、常に同じ「固定された」物理的なジェスチャー
  // 方向を追跡する(SDL自身が文書化しているその補正の意味)――
  // この構造体は、どちらの物理的方向が「より前を明かす」対「より
  // 後を明かす」内容を意味するかを「決めない」。そのアプリ意味上の
  // 対応付けはmain.cpp自身の判断(そのホイール消費ループ)であり、
  // 実機テストが、この移植自身のナチュラルスクロール有効なテスト
  // マシンに対して素朴な対応付けが逆であることを示した後、
  // 2026-08-06にそこで反転された――現在の符号の慣習については、
  // ここではなくそのコメント参照。
  struct WheelEvent {
    int deltaY = 0;
  };
  std::optional<WheelEvent> consumeWheelEvent();

  // 右端に予約されたスクロールバー帯の「内側で」始まった左ボタンの
  // 押下/ドラッグ/解放(drawScrollbar()自身のコメント参照)――つまみ
  // をドラッグすることが内容の選択や内容のドラッグスクロールも
  // 「同時に」引き起こさないよう、まさにその押下に対してMouseClick/
  // DragEventの代わりにキューに入れられる。2026-08-07にユーザー
  // 自身の依頼で追加された: スクロールバーが実際に見えるように
  // なった今、それは単なる受動的な指標ではなく、実際の機能(ドラッグ
  // 先の位置へジャンプする)が必要である。`y`は絶対的なウィンドウ
  // 座標のピクセル位置(DragEvent自身のMotionのような相対的な
  // deltaYではない)――スクロールバーのつまみをドラッグすることは、
  // 相対的な「これだけ動かす」ジェスチャーではなく絶対的な「ここへ
  // 行く」ジェスチャーであるため、main.cppはそれをmarginYPixels()/
  // cellHeightPixels()経由で目標のtopIndexへ直接マップする。
  enum class ScrollbarEventType { ButtonDown, Motion, ButtonUp };
  struct ScrollbarEvent {
    ScrollbarEventType type;
    int y = 0;
  };
  std::optional<ScrollbarEvent> consumeScrollbarEvent();

  // 現在のスクロール位置をキャッシュし、flush()(静的な、スクロール
  // しない描画経路)が、装飾的なプレースホルダーの代わりに、
  // drawScrollbar()経由で「本物の」常に最新のつまみを描画できる
  // ようにする――以前のバージョンは代わりにここに静的な埋め込み
  // スクリーンショットを貼っていた(scrollbar_idle_image.hpp、
  // 以来削除済み)。それは特に、flush()が自身のスクロール位置
  // パラメータを持たず(TerminalDisplayと共有されるDisplayの汎用
  // インターフェースには、そのような概念が無い)、本物の
  // ContentViewのtopIndex/totalLinesを直接それへ通すには
  // Displayインターフェースの変更が必要になる(この延期された
  // 代替案についてはこのメソッド自身のgit履歴参照)からである。
  // これはそれを回避する: 実際の報告(2026-08-09、"マウス操作ON/OFF
  // のたびに矩形つまみと張りぼて画像を切り替えるのは無意味な気が
  // する。つまみが消えてしまうので継続操作もできないし")が、
  // その切り替え自体が積極的に有害であることを発見した――装飾
  // 画像は位置を追跡しないので、つまみがドラッグの合間に現在どこ
  // にあるかを見る(そして掴む)ことが不可能だった。全ての
  // runGuiPhase()の反復(main.cpp)で呼ぶこと。syncScrollbar()が
  // 撤回される前に使っていたのと同じ「無条件に呼ぶ、安価」という
  // パターン――同じ反復で実行されるかもしれないどんなredraw()より
  // も「前に」呼ぶこと。そうすればflush()は実行される時に常に
  // 新鮮な値を見る。
  void updateScrollPosition(int topIndex, int totalLines, int contentRows);

  // ピクセル単位の固定された上端マージン(Impl::kMarginY自身の
  // コメント参照)――main.cppのScrollbarEvent処理が、
  // GraphicalDisplay自身のImplの内部を知る必要無く、絶対的な
  // ウィンドウYを「コンテンツ領域のどれだけ下か」へ変換できる
  // ように、既に公開されているcellHeightPixels()と並んで公開
  // されている。
  int marginYPixels() const;

  // 一般的な契約についてはDisplay::renderScrolled()自身のコメント
  // 参照(これはそのメソッドの実際の、ピクセル単位の実装である)。
  // 呼び出し元は、1行分が積み上がった時点で本物のtopIndexの変更を
  // 確定し、pixelOffsetYをリセットする責任を負う(main.cppの
  // commitRowCrossings()参照)。`modeLineSegments`は、呼び出しの
  // たびに固定された最下行へ新しく描画される――各セグメントは
  // それ自身の反転表示のrun(または、非standoutなギャップの
  // セグメントに対しては、プレーンな背景のままにされる)であり、
  // 1つの途切れないハイライトバーではなく、ContentView::
  // renderModeLine()自身のフィールドごとの見た目に合わせている
  // (これが修正する実際のバグについてはDisplay::ModeLineSegment
  // 自身のコメント参照)。内容自体は決してその行へスクロールして
  // 入らない(なぜ以前そうしていたかについてはこのメソッド自身の
  // .cpp内のコメント参照)。
  void renderScrolled(const std::vector<Display::ScrolledLine> &lines, int topIndex, int pixelOffsetY,
                       const std::vector<Display::ModeLineSegment> &modeLineSegments) override;

  int cellHeightPixels() const;

  // ウィンドウのカーソルを、デフォルトの矢印と、本物のmacOSの
  // 開いた手のカーソルのピクセル(mac_cursor.hpp経由でNSCursor.
  // openHandから実行時に抽出され、通常のSDL_Cursorのように
  // SDL自身のSDL_SetCursor()経由で駆動される――手描きの
  // SDL_CreateColorCursorビットマップではなく、生の[NSCursor set]
  // 呼び出しでもなく、本物のシステムカーソルのピクセルを使う理由
  // (2回却下された手描きの試みと、1回のドラッグ途中でカーソルが
  // 元に戻ってしまうバグの後)については.cpp自身のコメント参照)
  // の間で切り替える。これは、「内容を」ドラッグしている間
  // (「猫の手」スクロールジェスチャー)に使う――このメソッドが
  // 2026-08-10より前に使っていたSDLのシステムSDL_SYSTEM_CURSOR_HAND
  // (指差しの人差し指)では「ない」。それは今下のsetPointingCursor()
  // が代わりにカバーする。
  void setHandCursor(bool active);

  // SDLのシステムの指差し(人差し指)カーソル――内容を掴むという
  // よりコントロールを操作することに近いジェスチャーのために予約
  // されている: スクロールバードラッグの呼び出し箇所と、(まだ
  // 配線されていない)将来の選択可能な内容の上でのホバーについては
  // 自身の.cppコメント参照。
  void setPointingCursor(bool active);

 private:
  GraphicalDisplay();

  struct Impl;
  std::unique_ptr<Impl> impl_;

  // pumpEvents()自身のSDL_WINDOWEVENT_RESIZEDハンドラ(2026-08-24、
  // 項目425): ウィンドウ自身の新しいピクセルサイズを与えられて、
  // (逆のことを行うmatchWindowSize()とは異なり――自身のコメント
  // 参照)現在のセルサイズでrows()/columns()を再計算し、グリッドを
  // 再割り当てし、consumeSizeChanged()の裏付けとなるフラグを設定
  // する。再計算された行/列数が実際には変わらない場合(例えば、
  // 実際のグリッドに関係するサイズ変更以外の理由でリサイズ
  // イベントが発火した、またはsetSize()/matchWindowSize()自身が
  // 引き起こしたもの)は何もしない。
  void handleWindowResized(int newPixelWidth, int newPixelHeight);

  // pumpEvents()自身の、Cmd+=/Cmd+-/Cmd+0のためのSDL_KEYDOWN
  // ハンドラ(2026-08-24、docs/CHANGES.md項目426、項目425への
  // 追加依頼――同じユーザー依頼のサブパート(2)/(3)): フォント
  // ポイントサイズを`deltaPoints`だけ生きたまま調整し(妥当な
  // [6,72]範囲に制限される)、新しくサイズ変更されたフォントから
  // cellWidth/cellHeightを再計測し(フォント自身の送り幅はポイント
  // サイズに完全に線形にはスケールしないので、create()自身の
  // 「M」+CJKフォールバックグリフの計測を反映している)、
  // setSize()が既に使っているのと「同じ」式でウィンドウをリサイズ
  // する――rows/columnsは「固定」のままで、ウィンドウのピクセル
  // サイズとその中のフォント/セルサイズだけが変わる(ブラウザの
  // Cmd+Plus/Cmd+Minusのズームの慣習に合わせている: 内容の多少では
  // なく文字サイズの大小)。deltaPoints が0の場合は「起動時のサイズ
  // へリセット」を意味し(Impl::baseFontPointSize)、Cmd+0に紐付いて
  // いる。まだウィンドウ/フォントが存在しない場合、または目標
  // サイズが変わらない場合(既に制限された下限/上限にある、または
  // 既にリセットの目標値にある)は何もしない。
  void adjustFontSize(int deltaPoints);

  // pumpEvents()自身の、Cmd+KのためのSDL_KEYDOWNハンドラ
  // (2026-08-24、項目426、サブパート(2)): impl_->fg/bgを、小さな
  // 固定セットのプリセット(Impl::kColorPresets)を通じて巡回させる
  // ――「実はすでにGUI」の起動時の端末色一致経路が既に使っている
  // のと同じsetColors()で、単にAppleScriptの問い合わせの代わりに
  // キーで駆動されるだけ。setColors()自体と同様に次の描画で反映
  // される。このメソッドはneedsRedrawも設定するので、次の描画は、
  // 無関係な入力を待つのではなく実際に即座に起こる。
  void cycleColorPreset();

  void putByte(unsigned char c);
  void commitGlyph(const std::string &utf8Bytes, int width);
  void renderCellBackground(int row, int col);
  void renderCell(int row, int col);
  void renderCursor();
  // `startCol`(モードラインセグメントの書き直しのために2026-08-07に
  // 追加――renderScrolled()自身のモードラインブロック参照)は
  // 描画の開始位置をずらす。既存の呼び出し元は全て、デフォルトの
  // 0を渡し続ける(コンテンツ行全体は常に左端から始まる)。
  void renderTextRun(int y, const std::string &line, Color fg, int startCol = 0,
                      const std::vector<Display::AttributeRun> &attributes = {});
  // renderTextRun()が内部で`x`を進めるのと同じ方法(同じfm::Utf8
  // ヘルパー経由で、マルチバイトUTF-8を認識)で`text`の画面上の
  // 列幅を合計する――モードラインセグメントのループが、各
  // セグメント自身のハイライト矩形が何列にわたるべきか、そして
  // 「次の」セグメントのstartColがどこから始まるかを、
  // renderTextRun()自身のグリフの歩みを2度目に重複させることなく
  // 知るために使われる。
  int textRunDisplayWidth(const std::string &text) const;
  // renderScrolled()自身の呼び出し箇所のコメント参照。
  // `effectiveTop`は連続的な(小数の)topIndexである――pixelOffsetY/
  // cellHeightで調整されたtopIndex――つまみが、1行につき1回
  // スナップするだけではなく、行未満のスクロール位置と同期して
  // なめらかに動くようにする。
  void drawScrollbar(double effectiveTop, int totalLines, int contentRows);
  // 埋め込まれた3つの本物の-c端末のスクロールバーの断片
  // (scrollbar_idle_image.hpp自身のクラスコメント――上端キャップ、
  // 伸縮可能なプレーンな中間部、下端キャップ)を予約された帯へ
  // 合成し、その装飾が常に「現在の」プロファイル自身のコンテンツ
  // 高さにちょうど収まり、はみ出し/隙間のトレードオフが無いよう
  // にする――最初の本物のマウス操作までの間だけflush()によって
  // 呼ばれる(理由と、その後何がこれに取って代わるかについては
  // Impl::everInteracted自身のコメント参照)。
  //
  // 2026-08-10に追加、「以前の」単一画像バージョン(それ自身が
  // 撮影したプロファイルのネイティブなピクセルサイズで、スケール
  // せずに1枚の完全なスクリーンショットを貼り、他のプロファイルでは
  // 意図的にはみ出す/隙間を残していた――実際の2026-08-09の依頼が、
  // 画像全体を均一に伸縮/切り詰めする試みが試され、つまみと丸い
  // トラック端を歪めるとして却下された後、まさにそのネイティブ
  // サイズの、切り詰め無しというトレードオフを明示的に求めていた)
  // を置き換える。この3断片バージョンは同じ問題を別の方法
  // ――通常の9スライス/3スライスのUIスケーリング――で解決する。
  // 保存する価値のある細部の無い1つの断片(プレーンな中間の
  // トラック)だけを常に伸縮させる一方、2つの詳細な端の断片は
  // それら自身のネイティブサイズで歪みなく描画され続けるので、
  // 受け入れるべきはみ出し/隙間のトレードオフはもはや全く無い。
  void drawIdleScrollbarImage();
  // スクロールバー帯自身の左端は、ここの全ての呼び出し箇所が
  // 以前それぞれ独立に計算していたkMarginX + columns*cellWidthから
  // 導出するのではなく、ウィンドウの「実際の」現在のピクセル幅
  // (SDL_GetWindowSize())に固定されている――2つが食い違い得る
  // 理由(matchWindowSize()の「実はすでにGUI」オーバーレイ経路、
  // 実際のmpi報告2026-08-24)についてはこのメソッド自身の.cpp
  // コメント参照。
  int scrollbarStripLeft() const;
};

}  // namespace fm
